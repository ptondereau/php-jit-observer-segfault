# php-jit-observer-segfault

Minimal reproducer for a PHP 8.4 crash: with `opcache.jit` tracing enabled and an
extension that uses the fcall Observer API, a JIT-compiled **megamorphic** call to an
observed user function dispatches the observer "begin" handler through the wrong
`run_time_cache` slot and calls a NULL pointer.

```
#0  0x0000000000000000 in ?? ()
#1  zend_observer_fcall_begin_prechecked ()
#2  <tracing JIT code>
```

Full write-up (root cause, disassembly, suggested fix, upstream links): [`REPORT.md`](./REPORT.md).

## Conditions (all three required)

1. tracing JIT compiles the call (`opcache.jit=1254`),
2. `zend_observer_fcall_op_array_extension != zend_observer_fcall_internal_function_extension`,
   which happens when an extension reserves an op_array extension handle
   (`zend_get_op_array_extension_handle()`) before registering the observer,
3. a megamorphic call site to the observed user function.

## Run it (CLI, against a PHP 8.4 build)

```bash
gcc -shared -fPIC $(php-config --includes) repro_observer.c \
    -o "$(php-config --extension-dir)/repro_observer.so"

php -n \
  -d zend_extension=opcache.so -d opcache.enable=1 -d opcache.enable_cli=1 \
  -d opcache.jit=1254 -d opcache.jit_buffer_size=32M \
  -d extension=repro_observer.so app/index.php
# Segmentation fault
```

## Run it (Docker, php:8.4-fpm)

```bash
docker compose up -d --build
curl -s "http://localhost:8081/index.php?n=3000000"   # HTTP 502
docker compose logs fpm | grep SIGSEGV
# WARNING: [pool www] child N exited on signal 11 (SIGSEGV - core dumped)
```

## Files

- `repro_observer.c` minimal extension (reserves one op_array handle, registers one fcall observer)
- `app/index.php` megamorphic call site to an observed method
- `99-jit-observer.ini` opcache + tracing JIT settings
- `Dockerfile`, `docker-compose.yml`, `nginx.conf` self-contained FPM setup
- `REPORT.md` the upstream bug report

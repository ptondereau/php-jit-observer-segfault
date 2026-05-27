# Tracing JIT can dispatch the observer "begin" handler through the wrong run_time_cache slot (NULL deref) on megamorphic calls

### Description

When `opcache.jit` tracing is enabled and an extension uses the fcall Observer API, a JIT-compiled **megamorphic** call to an observed user function can call a NULL (or garbage) begin handler and crash the process.

I have a small reproducer that crashes deterministically (below). I also tried to trace where this comes from inside the JIT. I have a guess at the exact spot, but I am not confident about the IR backend's PHI/merge operand convention, so please read the "Where it might come from" section as a lead, not a conclusion. The reproducer and the observed behaviour are solid; the source attribution is the uncertain part.

Expected: a JIT-compiled call to an observed user function reads the begin handler from the slot `zend_observer_fcall_install` wrote it to, `run_time_cache[zend_observer_fcall_op_array_extension]`, and calls a valid handler. Actual: it reads `run_time_cache[0]` instead, which holds `0`, and calls a NULL pointer (SIGSEGV at `ip=0`).

I cannot provide a 3v4l.org link: reproduction requires a small C extension that uses the fcall Observer API, plus a JIT and a megamorphic call site.

#### What is observed (this part is reproducible)

The faulting frame is the dispatch loop in [`zend_observer_fcall_begin_prechecked`](https://github.com/php/php-src/blob/php-8.4.21/Zend/zend_observer.c#L278-L280): `(*handler)(execute_data)` with `*handler == 0`.

```
#0  0x0000000000000000 in ?? ()
#1  zend_observer_fcall_begin_prechecked ()
#2  <tracing JIT code for the megamorphic call>
```

The `handler` pointer the JIT passes points at `run_time_cache[0]`, but for a user function the begin handler is correctly installed at `run_time_cache[zend_observer_fcall_op_array_extension]` (which is `1` in the reproducer) by [`zend_observer_fcall_install`](https://github.com/php/php-src/blob/php-8.4.21/Zend/zend_observer.c#L116). So the install is fine; the JIT just reads a different slot.

Disassembly of the generated observer-begin sequence (run_time_cache base in `rsi`, function pointer in `rbx`):

```asm
mov   (%rax,%rsi,1),%rsi    ; rsi = run_time_cache (offset 0)
testb $0x1,(%rbx)           ; func->type & ZEND_INTERNAL_FUNCTION
je    .skip                 ; user types are even -> branch taken -> stays at offset 0
lea   0x8(%rsi),%rsi        ; +1 slot, only reached when type is odd (internal)
.skip:
cmpq  $0x3,(%rsi)           ; NONE_OBSERVED check, then call zend_observer_fcall_begin_prechecked(ex, rsi)
```

For a user function (`type` is even, see [`ZEND_USER_CODE`](https://github.com/php/php-src/blob/php-8.4.21/Zend/zend_compile.h#L1061)) the `+8` is skipped, so the handler pointer is `run_time_cache + 0`, i.e. the `internal_function_extension` slot, instead of the `op_array_extension` slot (see [`ZEND_OBSERVER_HANDLE`](https://github.com/php/php-src/blob/php-8.4.21/Zend/zend_observer.h#L36-L37)). This only diverges when the two handle indices differ.

#### Conditions

All three are needed; I confirmed each:

1. tracing JIT is compiling the call (no JIT, no crash, because the VM uses the `ZEND_OBSERVER_HANDLE` macro directly);
2. `zend_observer_fcall_op_array_extension != zend_observer_fcall_internal_function_extension`. These are reserved in independent spaces, so they differ as soon as some extension reserves an op_array handle (`zend_get_op_array_extension_handle()`) before the observer is registered (then the observer's op_array handle is `1` while its internal handle is `0`);
3. a megamorphic call site to the observed user function, so the JIT cannot resolve the callee statically and emits the runtime handle selection rather than baking it.

I isolated condition (2) with an A/B run (same script, with `-n`):

| observer extension | `op_array_extension` vs `internal_function_extension` | result |
|---|---|---|
| does **not** reserve an op_array handle | equal (both `0`) | exits cleanly |
| reserves an op_array handle first | differ (`1` vs `0`) | SIGSEGV (exit 139) |

#### Reproducer

Minimal extension `obs.c` (reserves one op_array handle, then registers one fcall observer with trivial handlers; it never writes its own slot, so `run_time_cache[0]` stays `0` and the wrong dispatch hits `ip=0`):

```c
#include "php.h"
#include "zend_observer.h"
#include "zend_extensions.h"

static int obs_op_array_handle = 0;

static void obs_begin(zend_execute_data *ex) { (void)ex; }
static void obs_end(zend_execute_data *ex, zval *rv) { (void)ex; (void)rv; }

static zend_observer_fcall_handlers obs_init(zend_execute_data *ex) {
    zend_observer_fcall_handlers h = {NULL, NULL};
    if (ex->func && ZEND_USER_CODE(ex->func->type)) { h.begin = obs_begin; h.end = obs_end; }
    return h;
}

static PHP_MINIT_FUNCTION(obs) {
    /* reserve an op_array handle BEFORE registering the observer */
    obs_op_array_handle = zend_get_op_array_extension_handle("obs");
    zend_observer_fcall_register(obs_init);
    return SUCCESS;
}

zend_module_entry obs_module_entry = {
    STANDARD_MODULE_HEADER, "obs", NULL, PHP_MINIT(obs),
    NULL, NULL, NULL, NULL, "0.1", STANDARD_MODULE_PROPERTIES
};
ZEND_GET_MODULE(obs)
```

Script `t.php` (megamorphic call site: 5 receiver types, default `jit_max_polymorphic_calls=2`):

```php
<?php
interface S { public function f(string $n): int; }
final class A implements S { private array $s=[]; public function f(string $n): int { $this->s[$n]=($this->s[$n]??0)+1; return count($this->s);} }
final class B implements S { private array $s=[]; public function f(string $n): int { $this->s[$n]=($this->s[$n]??0)+2; return count($this->s);} }
final class C implements S { private array $s=[]; public function f(string $n): int { $this->s[$n]=($this->s[$n]??0)+3; return count($this->s);} }
final class D implements S { private array $s=[]; public function f(string $n): int { $this->s[$n]=($this->s[$n]??0)+4; return count($this->s);} }
final class E implements S { private array $s=[]; public function f(string $n): int { $this->s[$n]=($this->s[$n]??0)+5; return count($this->s);} }

$o = [new A,new B,new C,new D,new E]; $m = count($o); $t = 0;
for ($i = 0; $i < 5_000_000; $i++) { $t += $o[$i % $m]->f('s'.($i & 7)); }
echo $t, "\n";
```

Build and run:

```bash
gcc -shared -fPIC $(php-config --includes) obs.c -o "$(php-config --extension-dir)/obs.so"

php -n \
  -d zend_extension=opcache.so -d opcache.enable=1 -d opcache.enable_cli=1 \
  -d opcache.jit=1254 -d opcache.jit_buffer_size=32M \
  -d extension=obs.so t.php
# Segmentation fault
```

(A `php:8.4-fpm` Dockerfile + compose wrapping the same thing is available if it helps; under FPM it crashes on the first request with a 502.)

#### Where it might come from (uncertain)

This looks like it comes from the `func == NULL` (callee not statically known) branch of [`jit_observer_fcall_is_unobserved_start`](https://github.com/php/php-src/blob/php-8.4.21/ext/opcache/jit/zend_jit_ir.c#L4802-L4818). When the callee is known it bakes the correct handle at [L4803](https://github.com/php/php-src/blob/php-8.4.21/ext/opcache/jit/zend_jit_ir.c#L4803). Otherwise it builds the handler with a runtime branch and a PHI ([L4806-L4818](https://github.com/php/php-src/blob/php-8.4.21/ext/opcache/jit/zend_jit_ir.c#L4806-L4818)):

```c
ir_IF_TRUE(if_internal_func);          // internal
ir_ref observer_handler_internal = ir_ADD_OFFSET(run_time_cache, zend_observer_fcall_internal_function_extension * sizeof(void *));
ir_ref if_internal_func_end = ir_END();
ir_IF_FALSE(if_internal_func);         // user (this is the current block)
ir_ref observer_handler_user = ir_ADD_OFFSET(run_time_cache, zend_observer_fcall_op_array_extension * sizeof(void *));
ir_MERGE_WITH(if_internal_func_end);
*observer_handler = ir_PHI_2(IR_ADDR, observer_handler_internal, observer_handler_user);
```

If I read the IR convention correctly (PHI operand N matches the `ir_MERGE_WITH` predecessor N, and the current block at the merge is the `ir_IF_FALSE` / user path), then the operands at [L4818](https://github.com/php/php-src/blob/php-8.4.21/ext/opcache/jit/zend_jit_ir.c#L4818) look reversed relative to the merge predecessor order, which would make the user path pick `observer_handler_internal`. That matches the disassembly above. But I am not sure about that convention, so I may be misreading it and the real cause may be elsewhere.

#### Possible fix (please verify)

If the reading above is right, swapping the PHI operands lines them up with the merge predecessors:

```c
*observer_handler = ir_PHI_2(IR_ADDR, observer_handler_user, observer_handler_internal);
```

I have not validated this against the test suite, and I would not trust it without a maintainer confirming the PHI/merge operand correspondence in the IR backend.

#### Workarounds

- set `opcache.jit=disable`, or
- do not register an fcall observer in the extension.

Both stop the crash.

### PHP Version

PHP 8.4.21

### Operating System

Linux x86-64 (Debian/Ubuntu)

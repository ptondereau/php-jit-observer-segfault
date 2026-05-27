PHP_ARG_ENABLE([repro_observer],
  [whether to enable the repro_observer extension],
  [AS_HELP_STRING([--enable-repro_observer],
    [Enable repro_observer: PHP 8.4 tracing-JIT observer segfault reproducer])],
  [no])

if test "$PHP_REPRO_OBSERVER" != "no"; then
  PHP_NEW_EXTENSION(repro_observer, repro_observer.c, $ext_shared)
fi

/* Minimal reproducer extension for the PHP 8.4 tracing-JIT observer-handle issue.
 * It only needs to:
 *   (1) reserve ONE op_array run_time_cache extension handle BEFORE the observer is
 *       registered, so that zend_observer_fcall_op_array_extension (1) ends up
 *       different from zend_observer_fcall_internal_function_extension (0), and
 *   (2) register one fcall observer returning {begin, end} for user functions.
 * It never writes its own slot, so run_time_cache[0] stays 0 and the JIT's wrong
 * dispatch through slot 0 calls NULL (ip=0).
 *
 * Build (standard PHP extension toolchain):
 *   phpize
 *   ./configure --enable-repro_observer
 *   make
 * The result is modules/repro_observer.so.
 */
#include "php.h"
#include "zend_observer.h"
#include "zend_extensions.h"

static int repro_observer_op_array_handle = 0;

static void repro_observer_begin(zend_execute_data *ex) { (void)ex; }
static void repro_observer_end(zend_execute_data *ex, zval *retval) { (void)ex; (void)retval; }

static zend_observer_fcall_handlers repro_observer_init(zend_execute_data *ex)
{
    zend_observer_fcall_handlers h = {NULL, NULL};
    if (ex->func && ZEND_USER_CODE(ex->func->type)) {
        h.begin = repro_observer_begin;
        h.end = repro_observer_end;
    }
    return h;
}

static PHP_MINIT_FUNCTION(repro_observer)
{
    /* reserve our op_array handle before registering the observer */
    repro_observer_op_array_handle = zend_get_op_array_extension_handle("repro_observer");
    zend_observer_fcall_register(repro_observer_init);
    return SUCCESS;
}

zend_module_entry repro_observer_module_entry = {
    STANDARD_MODULE_HEADER,
    "repro_observer",
    NULL,
    PHP_MINIT(repro_observer),
    NULL, NULL, NULL, NULL,
    "0.1",
    STANDARD_MODULE_PROPERTIES
};

ZEND_GET_MODULE(repro_observer)

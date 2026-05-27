/* Minimal Blackfire-free reproducer for the PHP 8.4 tracing-JIT observer-handle
 * miscompilation. The extension only needs to:
 *   (1) reserve ONE op_array run_time_cache extension handle BEFORE the observer is
 *       registered, so that zend_observer_fcall_op_array_extension (1) ends up
 *       different from zend_observer_fcall_internal_function_extension (0), and
 *   (2) register one fcall observer returning {begin,end} for user functions.
 * It never touches its own slot, so run_time_cache[0] stays 0 and the JIT's wrong
 * dispatch through slot 0 calls NULL (ip=0), matching the production crash.
 *
 * Build:
 *   gcc -shared -fPIC $(php-config --includes) repro_observer.c \
 *       -o "$(php-config --extension-dir)/repro_observer.so"
 */
#include "php.h"
#include "zend_observer.h"
#include "zend_extensions.h"

static int repro_op_array_extension = 0;

static void repro_begin(zend_execute_data *ex) { (void)ex; }
static void repro_end(zend_execute_data *ex, zval *retval) { (void)ex; (void)retval; }

static zend_observer_fcall_handlers repro_init(zend_execute_data *ex)
{
    zend_observer_fcall_handlers h = {NULL, NULL};
    if (ex->func && ZEND_USER_CODE(ex->func->type)) {
        h.begin = repro_begin;
        h.end = repro_end;
    }
    return h;
}

static PHP_MINIT_FUNCTION(repro)
{
    /* reserve our op_array handle before the observer reserves its own */
    repro_op_array_extension = zend_get_op_array_extension_handle("repro");
    zend_observer_fcall_register(repro_init);
    fprintf(stderr, "[repro] op_array_extension handle = %d\n", repro_op_array_extension);
    return SUCCESS;
}

zend_module_entry repro_module_entry = {
    STANDARD_MODULE_HEADER,
    "repro", NULL, PHP_MINIT(repro),
    NULL, NULL, NULL, NULL, "0.1",
    STANDARD_MODULE_PROPERTIES
};

ZEND_GET_MODULE(repro)

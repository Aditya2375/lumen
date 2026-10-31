/* Browser entry point: assemble and run source in the page. */
#include "lumen.h"

#include <emscripten.h>
#include <stdio.h>
#include <string.h>

/* Assemble and run src. Returns NULL on success (program output goes to
   Module.print), or a static error message. */
EMSCRIPTEN_KEEPALIVE
const char *lumen_run(const char *src) {
    static char err[512];
    err[0] = '\0';
    struct program *p = assemble(src, err, sizeof err);
    if (!p) return err;
    struct vm vm;
    if (vm_run(&vm, p) != 0) {
        snprintf(err, sizeof err, "%s", vm.err);
        program_free(p);
        return err;
    }
    program_free(p);
    return NULL;
}

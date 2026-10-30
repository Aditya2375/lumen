#include "../harness.h"
#include "lumen.h"

#include <string.h>

/* run a program; returns 0 on clean halt. stdout from print goes to the
   caller's redirected fd in end-to-end tests; here we assert state. */
static int run(struct vm *vm, const char *src, char *err) {
    struct program *p = assemble(src, err, 256);
    if (!p) return -2;
    int rc = vm_run(vm, p);
    if (rc != 0) snprintf(err, 256, "%s", vm->err);
    program_free(p);
    return rc;
}

int main(void) {
    struct vm vm;
    char err[256];

    /* arithmetic */
    CHECK(run(&vm, "const 6\nconst 7\nmul\nhalt", err) == 0, "mul halts");
    CHECK(vm.sp == 1 && vm.stack[0] == 42, "6*7 = 42");

    CHECK(run(&vm, "const 10\nconst 3\nmod\nhalt", err) == 0, "mod halts");
    CHECK(vm.stack[0] == 1, "10%%3 = 1");

    CHECK(run(&vm, "const 5\nneg\nconst 2\nadd\nhalt", err) == 0, "neg halts");
    CHECK(vm.stack[0] == -3, "-5+2 = -3");

    /* comparisons push 1/0 */
    CHECK(run(&vm, "const 3\nconst 4\nlt\nhalt", err) == 0, "lt halts");
    CHECK(vm.stack[0] == 1, "3<4 is 1");
    CHECK(run(&vm, "const 3\nconst 4\nge\nhalt", err) == 0, "ge halts");
    CHECK(vm.stack[0] == 0, "3>=4 is 0");

    /* locals */
    CHECK(run(&vm, "const 9\nstore 2\nload 2\nhalt", err) == 0, "store/load halts");
    CHECK(vm.sp > 0 && vm.stack[vm.sp - 1] == 9, "local round-trips");

    /* jumps: skip a poison instruction */
    CHECK(run(&vm, "jump ok\nconst 0\ndiv\nok:\nconst 1\nhalt", err) == 0, "jump skips");
    CHECK(vm.stack[0] == 1, "jump landed");

    /* conditional not taken falls through */
    CHECK(run(&vm, "const 0\njump_if bad\nconst 7\nhalt\nbad:\nconst 8\nhalt", err) == 0, "fall through");
    CHECK(vm.stack[0] == 7, "jump_if 0 falls through");
    CHECK(run(&vm, "const 1\njump_if good\nconst 8\nhalt\ngood:\nconst 9\nhalt", err) == 0, "taken");
    CHECK(vm.stack[0] == 9, "jump_if 1 jumps");

    /* faults */
    CHECK(run(&vm, "const 1\nconst 0\ndiv\nhalt", err) != 0, "div by zero faults");
    CHECK(strstr(err, "division by zero") != NULL, "fault says why");
    CHECK(run(&vm, "pop\nhalt", err) != 0, "underflow faults");
    CHECK(run(&vm, "load 5\nhalt", err) != 0, "bad local faults");
    CHECK(run(&vm, "", err) != 0, "empty program runs off the end");
    CHECK(run(&vm, "ret", err) != 0, "return outside call faults");

    /* assembler errors */
    CHECK(run(&vm, "jump nowhere", err) == -2, "undefined label rejected");
    CHECK(strstr(err, "undefined label") != NULL, "error names the label");
    CHECK(run(&vm, "frobnicate", err) == -2, "unknown instruction rejected");
    CHECK(run(&vm, "loop:\nloop:\nhalt", err) == -2, "duplicate label rejected");

    /* constant pool dedupes */
    {
        struct program *p = assemble("const 5\nconst 5\nconst 6\nhalt", err, 256);
        CHECK(p && p->nconstants == 2, "constants deduped");
        program_free(p);
    }

    TEST_RESULTS();
}

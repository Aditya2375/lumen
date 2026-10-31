#include "../harness.h"
#include "lumen.h"

#include <stdint.h>
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

    /* division overflow is a fault, not a crash */
    {
        struct program *p = assemble("const -9223372036854775808\nconst -1\ndiv\nhalt", err, 256);
        CHECK(p != NULL, "64-bit constant assembles");
        if (p) {
            int rc = vm_run(&vm, p);
            CHECK(rc != 0 && strstr(vm.err, "division overflow") != NULL, "INT64_MIN/-1 faults");
            program_free(p);
        }
    }
    CHECK(run(&vm, "const -9223372036854775808\nconst -1\nmod\nhalt", err) != 0, "mod overflow faults");

    /* full-range constants survive the assembler */
    {
        struct program *p = assemble("const 4611686018427387904\nhalt", err, 256);
        CHECK(p != NULL, "big constant assembles");
        if (p) {
            CHECK(p->nconstants == 1 && p->constants[0] == (int64_t)4611686018427387904LL,
                  "constant keeps all 64 bits");
            program_free(p);
        }
    }

    /* wrapping arithmetic: defined two's-complement, no UB */
    CHECK(run(&vm, "const 9223372036854775807\nconst 1\nadd\nhalt", err) == 0, "add wraps, halts");
    CHECK(vm.stack[0] == INT64_MIN, "INT64_MAX+1 wraps to INT64_MIN");
    CHECK(run(&vm, "const -9223372036854775808\nneg\nhalt", err) == 0, "neg of INT64_MIN defined");
    CHECK(vm.stack[0] == INT64_MIN, "neg INT64_MIN wraps to itself");

    /* label and instruction share a line */
    CHECK(run(&vm, "top: const 4\nhalt", err) == 0 && vm.stack[0] == 4, "same-line label works");

    /* malformed numbers rejected */
    CHECK(run(&vm, "const 12abc\nhalt", err) == -2, "trailing junk in number rejected");
    CHECK(strstr(err, "malformed number") != NULL, "malformed says why");
    CHECK(run(&vm, "const 99999999999999999999999999\nhalt", err) == -2, "out-of-range number rejected");

    /* a label may share its line with an instruction */
    CHECK(run(&vm, "start: const 5\nhalt", err) == 0, "label:instr assembles and runs");
    CHECK(vm.stack[0] == 5, "instruction after label ran");

    /* over-long label names are an error, not a silent truncation */
    {
        char src[512];
        snprintf(src, sizeof src,
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa: const 1\nhalt");
        CHECK(run(&vm, src, err) == -2, "long label rejected");
        CHECK(strstr(err, "too long") != NULL, "long label says why");
    }

    /* hand-built bytecode: truncated operand, bad opcode, bad constant index */
    {
        struct program p = {0};
        uint8_t code1[] = {OP_CONST, 0x00};
        p.code = code1; p.code_len = 2;
        CHECK(vm_run(&vm, &p) != 0 && strstr(vm.err, "truncated") != NULL, "truncated operand faults");

        uint8_t code2[] = {0xEE};
        p.code = code2; p.code_len = 1;
        CHECK(vm_run(&vm, &p) != 0 && strstr(vm.err, "bad opcode") != NULL, "bad opcode faults");

        uint8_t code3[] = {OP_CONST, 0x40, 0, 0, 0, OP_HALT};
        p.code = code3; p.code_len = 6;
        CHECK(vm_run(&vm, &p) != 0 && strstr(vm.err, "constant index") != NULL, "bad constant index faults");

        /* jump into nowhere */
        uint8_t code4[] = {OP_JUMP, 0xFF, 0xFF, 0xFF, 0x7F};
        p.code = code4; p.code_len = 5;
        CHECK(vm_run(&vm, &p) != 0 && strstr(vm.err, "out of bounds") != NULL, "wild jump faults");
    }

    /* stack overflow is a fault: 4100 pushes into a 4096-deep stack */
    {
        static uint8_t code_push[5 * 4100];
        uint8_t *cp = code_push;
        for (int i = 0; i < 4100; i++) { *cp++ = OP_CONST; *cp++ = 0; *cp++ = 0; *cp++ = 0; *cp++ = 0; }
        static int64_t k[1];
        struct program p = {0};
        p.code = code_push; p.code_len = sizeof code_push; p.constants = k; p.nconstants = 1;
        CHECK(vm_run(&vm, &p) != 0 && strstr(vm.err, "stack overflow") != NULL, "stack overflow faults");
    }

    /* binary save/load round-trips */
    {
        struct program *p = assemble("const 11\nconst 31\nmul\nhalt", err, 256);
        CHECK(p != NULL, "round-trip source assembles");
        if (p) {
            CHECK(program_save(p, "/tmp/lumen-roundtrip.lmb") == 0, "save works");
            char lerr[256];
            struct program *q = program_load("/tmp/lumen-roundtrip.lmb", lerr, sizeof lerr);
            CHECK(q != NULL, "load works");
            if (q) {
                CHECK(q->code_len == p->code_len && memcmp(q->code, p->code, (size_t)p->code_len) == 0,
                      "code survives the round-trip");
                CHECK(q->nconstants == p->nconstants, "pool survives the round-trip");
                CHECK(vm_run(&vm, q) == 0, "loaded program runs");
                CHECK(vm.sp == 1 && vm.stack[0] == 341, "loaded program computes 11*31");
                program_free(q);
            }
            program_free(p);
        }
    }

    TEST_RESULTS();
}

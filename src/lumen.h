#ifndef LUMEN_H
#define LUMEN_H

#include <stdint.h>
#include <stdio.h>

/* lumen - a bytecode virtual machine: assemble text -> bytecode -> execute. */

enum opcode {
    OP_CONST,       /* push constant pool[idx] */
    OP_LOAD,        /* push local[idx] */
    OP_STORE,       /* pop into local[idx] */
    OP_POP,
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
    OP_NEG,
    OP_EQ, OP_NE, OP_LT, OP_LE, OP_GT, OP_GE,
    OP_JUMP,        /* ip = addr */
    OP_JUMP_IF,     /* pop; jump if nonzero */
    OP_JUMP_IF_NOT, /* pop; jump if zero */
    OP_CALL,        /* call fn at addr with argc; frame on the stack */
    OP_RET,
    OP_PRINT,       /* pop and print with newline */
    OP_HALT,
    OP_COUNT
};

struct program {
    uint8_t *code;
    long code_len, code_cap;
    int64_t *constants;
    long nconstants, constants_cap;
};

struct vm {
    const struct program *prog;
    long ip;
    int64_t stack[4096];
    long sp;                /* next free slot */
    long fp;                /* frame pointer: locals are stack[fp + idx] */
    long call_stack[1024];  /* pairs: return ip, caller fp */
    long csp;
    char err[256];
};

/* assembler: parse assembly text into a program. NULL on error (err filled). */
struct program *assemble(const char *source, char *err, int err_cap);
void program_free(struct program *p);

/* binary form: save/load an assembled program ("LMB1" container) */
int program_save(const struct program *p, const char *path);
struct program *program_load(const char *path, char *err, int err_cap);
int program_is_binary(const char *path);

/* execute. Returns 0 on HALT, -1 on a runtime fault (vm->err filled). */
int vm_run(struct vm *vm, const struct program *prog);

/* encoding helpers shared with the disassembler */
extern const char *const opcode_names[OP_COUNT];
extern const int opcode_operands[OP_COUNT];   /* -1 = none, else operand count */

#endif

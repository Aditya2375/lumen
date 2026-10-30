#include "lumen.h"

#include <stdio.h>
#include <string.h>

static int fault(struct vm *vm, const char *msg) {
    snprintf(vm->err, sizeof vm->err, "fault at ip=%ld: %s", vm->ip, msg);
    return -1;
}

static int push(struct vm *vm, int64_t v) {
    if (vm->sp >= (long)(sizeof vm->stack / sizeof vm->stack[0]))
        return fault(vm, "stack overflow");
    vm->stack[vm->sp++] = v;
    return 0;
}

static int pop(struct vm *vm, int64_t *out) {
    if (vm->sp <= vm->fp)
        return fault(vm, "stack underflow");
    *out = vm->stack[--vm->sp];
    return 0;
}

/* fetch a 32-bit operand; refuses to read past the end of the code */
static int fetch32(const struct program *prog, long *ip, int32_t *out) {
    if (*ip < 0 || *ip + 4 > prog->code_len) return -1;
    const uint8_t *c = prog->code + *ip;
    *out = (int32_t)((uint32_t)c[0] | ((uint32_t)c[1] << 8) |
                     ((uint32_t)c[2] << 16) | ((uint32_t)c[3] << 24));
    *ip += 4;
    return 0;
}

/* Two's-complement wrapping arithmetic, defined at the C level via unsigned
   ops: the VM promises machine semantics, never C's signed-overflow UB. */
static int64_t wrap_add(int64_t a, int64_t b) { return (int64_t)((uint64_t)a + (uint64_t)b); }
static int64_t wrap_sub(int64_t a, int64_t b) { return (int64_t)((uint64_t)a - (uint64_t)b); }
static int64_t wrap_mul(int64_t a, int64_t b) { return (int64_t)((uint64_t)a * (uint64_t)b); }
static int64_t wrap_neg(int64_t a)            { return (int64_t)(0u - (uint64_t)a); }

/* the two idiv traps: zero divisor and the one overflowing quotient */
static int div_would_trap(int64_t a, int64_t b) {
    return b == 0 || (a == INT64_MIN && b == -1);
}

#define BINOP(expr) do {                        \
    int64_t b, a;                               \
    if (pop(vm, &b) || pop(vm, &a)) return -1;  \
    if (push(vm, (expr))) return -1;            \
} while (0)

int vm_run(struct vm *vm, const struct program *prog) {
    memset(vm, 0, sizeof *vm);
    vm->prog = prog;
    vm->ip = 0;
    vm->sp = 0;
    vm->fp = 0;
    vm->csp = 0;
    const uint8_t *code = prog->code;
    (void)code;

    for (;;) {
        if (vm->ip < 0 || vm->ip >= prog->code_len)
            return fault(vm, "instruction pointer out of bounds");
        enum opcode op = (enum opcode)code[vm->ip++];
        if (op < 0 || op >= OP_COUNT)
            return fault(vm, "bad opcode");
        switch (op) {
        case OP_CONST: {
            int32_t idx;
            if (fetch32(prog, &vm->ip, &idx)) return fault(vm, "truncated instruction");
            if (idx < 0 || idx >= prog->nconstants) return fault(vm, "constant index out of range");
            if (push(vm, prog->constants[idx])) return -1;
            break;
        }
        case OP_LOAD: {
            int32_t idx;
            if (fetch32(prog, &vm->ip, &idx)) return fault(vm, "truncated instruction");
            long slot = vm->fp + idx;
            if (idx < 0 || slot >= vm->sp) return fault(vm, "local index out of range");
            if (push(vm, vm->stack[slot])) return -1;
            break;
        }
        case OP_STORE: {
            int32_t idx;
            if (fetch32(prog, &vm->ip, &idx)) return fault(vm, "truncated instruction");
            int64_t v;
            if (pop(vm, &v)) return -1;
            if (idx < 0) return fault(vm, "local index out of range");
            long slot = vm->fp + idx;
            while (vm->sp <= slot)
                if (push(vm, 0)) return -1;   /* widen frame for new locals */
            vm->stack[slot] = v;
            break;
        }
        case OP_POP: {
            int64_t v;
            if (pop(vm, &v)) return -1;
            break;
        }
        case OP_ADD: BINOP(wrap_add(a, b)); break;
        case OP_SUB: BINOP(wrap_sub(a, b)); break;
        case OP_MUL: BINOP(wrap_mul(a, b)); break;
        case OP_DIV:
            if (vm->sp < vm->fp + 2) return fault(vm, "stack underflow");
            if (vm->stack[vm->sp - 1] == 0) return fault(vm, "division by zero");
            if (div_would_trap(vm->stack[vm->sp - 2], vm->stack[vm->sp - 1]))
                return fault(vm, "division overflow");
            BINOP(a / b);
            break;
        case OP_MOD:
            if (vm->sp < vm->fp + 2) return fault(vm, "stack underflow");
            if (vm->stack[vm->sp - 1] == 0) return fault(vm, "division by zero");
            if (div_would_trap(vm->stack[vm->sp - 2], vm->stack[vm->sp - 1]))
                return fault(vm, "division overflow");
            BINOP(a % b);
            break;
        case OP_NEG: {
            int64_t v;
            if (pop(vm, &v)) return -1;
            if (push(vm, wrap_neg(v))) return -1;
            break;
        }
        case OP_EQ: BINOP(a == b); break;
        case OP_NE: BINOP(a != b); break;
        case OP_LT: BINOP(a < b); break;
        case OP_LE: BINOP(a <= b); break;
        case OP_GT: BINOP(a > b); break;
        case OP_GE: BINOP(a >= b); break;
        case OP_JUMP: {
            int32_t addr;
            if (fetch32(prog, &vm->ip, &addr)) return fault(vm, "truncated instruction");
            vm->ip = addr;
            break;
        }
        case OP_JUMP_IF: case OP_JUMP_IF_NOT: {
            int32_t addr;
            if (fetch32(prog, &vm->ip, &addr)) return fault(vm, "truncated instruction");
            int64_t v;
            if (pop(vm, &v)) return -1;
            if ((op == OP_JUMP_IF && v) || (op == OP_JUMP_IF_NOT && !v))
                vm->ip = addr;
            break;
        }
        case OP_CALL: {
            int32_t addr, argc;
            if (fetch32(prog, &vm->ip, &addr) || fetch32(prog, &vm->ip, &argc))
                return fault(vm, "truncated instruction");
            if (argc < 0 || argc > vm->sp - vm->fp) return fault(vm, "bad argument count");
            if (vm->csp + 2 > (long)(sizeof vm->call_stack / sizeof vm->call_stack[0]))
                return fault(vm, "call stack overflow");
            vm->call_stack[vm->csp++] = vm->ip;    /* return address */
            vm->call_stack[vm->csp++] = vm->fp;    /* caller frame */
            vm->fp = vm->sp - argc;                /* args become locals 0..argc-1 */
            vm->ip = addr;
            break;
        }
        case OP_RET: {
            if (vm->csp == 0) return fault(vm, "return outside a call");
            int64_t ret = 0;
            if (vm->sp > vm->fp) ret = vm->stack[vm->sp - 1];  /* top = return value */
            vm->sp = vm->fp;
            vm->fp = vm->call_stack[--vm->csp];   /* pushed last: caller fp */
            vm->ip = vm->call_stack[--vm->csp];   /* pushed first: return address */
            if (push(vm, ret)) return -1;
            break;
        }
        case OP_PRINT: {
            int64_t v;
            if (pop(vm, &v)) return -1;
            printf("%ld\n", (long)v);
            break;
        }
        case OP_HALT:
            return 0;
        default:
            return fault(vm, "unimplemented opcode");
        }
    }
}

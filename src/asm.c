#include "lumen.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *const opcode_names[OP_COUNT] = {
    [OP_CONST] = "const",   [OP_LOAD] = "load",     [OP_STORE] = "store",
    [OP_POP] = "pop",
    [OP_ADD] = "add",       [OP_SUB] = "sub",       [OP_MUL] = "mul",
    [OP_DIV] = "div",       [OP_MOD] = "mod",       [OP_NEG] = "neg",
    [OP_EQ] = "eq",         [OP_NE] = "ne",         [OP_LT] = "lt",
    [OP_LE] = "le",         [OP_GT] = "gt",         [OP_GE] = "ge",
    [OP_JUMP] = "jump",     [OP_JUMP_IF] = "jump_if", [OP_JUMP_IF_NOT] = "jump_if_not",
    [OP_CALL] = "call",     [OP_RET] = "ret",
    [OP_PRINT] = "print",   [OP_HALT] = "halt",
};

const int opcode_operands[OP_COUNT] = {
    [OP_CONST] = 1, [OP_LOAD] = 1, [OP_STORE] = 1,
    [OP_JUMP] = 1,  [OP_JUMP_IF] = 1, [OP_JUMP_IF_NOT] = 1,
    [OP_CALL] = 2,
    [OP_POP] = 0, [OP_ADD] = 0, [OP_SUB] = 0, [OP_MUL] = 0, [OP_DIV] = 0,
    [OP_MOD] = 0, [OP_NEG] = 0, [OP_EQ] = 0, [OP_NE] = 0, [OP_LT] = 0,
    [OP_LE] = 0, [OP_GT] = 0, [OP_GE] = 0, [OP_RET] = 0, [OP_PRINT] = 0,
    [OP_HALT] = 0,
};

/* growable emitters */
static void emit8(struct program *p, uint8_t b) {
    if (p->code_len >= p->code_cap) {
        p->code_cap = p->code_cap ? p->code_cap * 2 : 256;
        p->code = realloc(p->code, (size_t)p->code_cap);
        if (!p->code) { perror("realloc"); exit(1); }
    }
    p->code[p->code_len++] = b;
}

static void emit32(struct program *p, int32_t v) {
    emit8(p, (uint8_t)v);
    emit8(p, (uint8_t)(v >> 8));
    emit8(p, (uint8_t)(v >> 16));
    emit8(p, (uint8_t)(v >> 24));
}

static void patch32(struct program *p, long at, int32_t v) {
    p->code[at] = (uint8_t)v;
    p->code[at + 1] = (uint8_t)(v >> 8);
    p->code[at + 2] = (uint8_t)(v >> 16);
    p->code[at + 3] = (uint8_t)(v >> 24);
}

static int add_constant(struct program *p, int64_t v) {
    for (long i = 0; i < p->nconstants; i++)
        if (p->constants[i] == v) return (int)i;
    if (p->nconstants >= p->constants_cap) {
        p->constants_cap = p->constants_cap ? p->constants_cap * 2 : 64;
        p->constants = realloc(p->constants, (size_t)p->constants_cap * sizeof *p->constants);
        if (!p->constants) { perror("realloc"); exit(1); }
    }
    p->constants[p->nconstants] = v;
    return (int)p->nconstants++;
}

struct label { char name[64]; long addr; };       /* addr < 0: undefined */
struct fixup { long at; char name[64]; int line; };

struct program *assemble(const char *source, char *err, int err_cap) {
    struct program *p = calloc(1, sizeof *p);
    if (!p) { perror("calloc"); exit(1); }
    struct label labels[512];
    int nlabels = 0;
    struct fixup fixups[1024];
    int nfixups = 0;
    int line = 0;

    const char *cur = source;
    while (*cur) {
        line++;
        const char *eol = strchr(cur, '\n');
        long len = eol ? eol - cur : (long)strlen(cur);
        char buf[256];
        if (len >= (long)sizeof buf) { snprintf(err, err_cap, "line %d: line too long", line); goto fail; }
        memcpy(buf, cur, (size_t)len);
        buf[len] = '\0';
        cur = eol ? eol + 1 : cur + len;

        /* strip comments and trim */
        char *hash = strchr(buf, '#');
        if (hash) *hash = '\0';
        char *s = buf;
        while (isspace((unsigned char)*s)) s++;
        char *end = s + strlen(s);
        while (end > s && isspace((unsigned char)end[-1])) *--end = '\0';
        if (!*s) continue;

        /* label? */
        char *colon = strchr(s, ':');
        if (colon && !strchr(s, ' ')) {
            *colon = '\0';
            if (nlabels >= 512) { snprintf(err, err_cap, "too many labels"); goto fail; }
            for (int i = 0; i < nlabels; i++)
                if (strcmp(labels[i].name, s) == 0) {
                    snprintf(err, err_cap, "line %d: duplicate label '%s'", line, s);
                    goto fail;
                }
            snprintf(labels[nlabels].name, 64, "%s", s);
            labels[nlabels].addr = p->code_len;
            nlabels++;
            continue;
        }

        /* instruction */
        char mnem[64] = {0};
        int i = 0;
        while (s[i] && !isspace((unsigned char)s[i]) && i < 63) { mnem[i] = s[i]; i++; }
        mnem[i] = '\0';
        s += i;
        while (isspace((unsigned char)*s)) s++;

        enum opcode op = OP_COUNT;
        for (int k = 0; k < OP_COUNT; k++)
            if (opcode_names[k] && strcmp(mnem, opcode_names[k]) == 0) { op = (enum opcode)k; break; }
        if (op == OP_COUNT) { snprintf(err, err_cap, "line %d: unknown instruction '%s'", line, mnem); goto fail; }
        emit8(p, (uint8_t)op);

        int want = opcode_operands[op];
        for (int a = 0; a < want; a++) {
            if (!*s) { snprintf(err, err_cap, "line %d: '%s' needs %d operand(s)", line, mnem, want); goto fail; }
            char operand[64] = {0};
            int j = 0;
            while (s[j] && !isspace((unsigned char)s[j]) && j < 63) { operand[j] = s[j]; j++; }
            operand[j] = '\0';
            s += j;
            while (isspace((unsigned char)*s)) s++;
            if (isdigit((unsigned char)operand[0]) || (operand[0] == '-' && isdigit((unsigned char)operand[1]))) {
                int32_t v = (int32_t)strtol(operand, NULL, 10);
                if (op == OP_CONST && a == 0) v = add_constant(p, v);  /* literal -> pool index */
                emit32(p, v);
            } else {
                /* symbolic operand: label fixup */
                if (nfixups >= 1024) { snprintf(err, err_cap, "too many fixups"); goto fail; }
                fixups[nfixups].at = p->code_len;
                snprintf(fixups[nfixups].name, 64, "%s", operand);
                fixups[nfixups].line = line;
                nfixups++;
                emit32(p, 0);
            }
        }
        if (*s) { snprintf(err, err_cap, "line %d: trailing text after '%s'", line, mnem); goto fail; }
    }

    /* resolve labels */
    for (int f = 0; f < nfixups; f++) {
        int found = -1;
        for (int l = 0; l < nlabels; l++)
            if (strcmp(labels[l].name, fixups[f].name) == 0) { found = l; break; }
        if (found < 0) {
            snprintf(err, err_cap, "line %d: undefined label '%s'", fixups[f].line, fixups[f].name);
            goto fail;
        }
        patch32(p, fixups[f].at, (int32_t)labels[found].addr);
    }
    return p;

fail:
    program_free(p);
    return NULL;
}

void program_free(struct program *p) {
    if (!p) return;
    free(p->code);
    free(p->constants);
    free(p);
}

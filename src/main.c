#include "lumen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(FILE *f) {
    fprintf(f,
        "lumen - a bytecode virtual machine\n"
        "\n"
        "usage: lumen <program.lm|program.lmb> [--disassemble] [--save out.lmb]\n"
        "\n"
        "Assembles the program and executes it; a .lmb binary is loaded and\n"
        "run directly. --save writes the binary form instead of running, and\n"
        "--disassemble prints the bytecode with resolved addresses.\n");
}

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)size + 1);
    if (!buf) { perror("malloc"); exit(1); }
    if (fread(buf, 1, (size_t)size, f) != (size_t)size) { fclose(f); free(buf); return NULL; }
    fclose(f);
    buf[size] = '\0';
    return buf;
}

static void disassemble(const struct program *p) {
    /* the binary may be corrupt: never read past the code or the pool */
    long ip = 0;
    while (ip < p->code_len) {
        long at = ip;
        enum opcode op = (enum opcode)p->code[ip++];
        printf("%04ld  %-12s", at, op < OP_COUNT ? opcode_names[op] : "???");
        int ops = op < OP_COUNT ? opcode_operands[op] : 0;
        for (int i = 0; i < ops; i++) {
            if (ip + 4 > p->code_len) {
                printf(" ; truncated operand");
                return;
            }
            int32_t v = (int32_t)((uint32_t)p->code[ip] |
                                  ((uint32_t)p->code[ip + 1] << 8) |
                                  ((uint32_t)p->code[ip + 2] << 16) |
                                  ((uint32_t)p->code[ip + 3] << 24));
            ip += 4;
            if (op == OP_CONST && i == 0) {
                if (v >= 0 && v < p->nconstants)
                    printf(" %d  ; %ld", v, (long)p->constants[v]);
                else
                    printf(" %d  ; <constant index out of range>", v);
            } else {
                printf(" %d", v);
            }
        }
        printf("\n");
    }
}

int main(int argc, char **argv) {
    const char *input = NULL, *save = NULL;
    int dis = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--disassemble") == 0) dis = 1;
        else if (strcmp(argv[i], "--save") == 0 && i + 1 < argc) save = argv[++i];
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) { usage(stdout); return 0; }
        else if (!input) input = argv[i];
        else { usage(stderr); return 2; }
    }
    if (!input) { usage(stderr); return 2; }

    char err[256];
    struct program *prog;
    if (program_is_binary(input)) {
        prog = program_load(input, err, sizeof err);
        if (!prog) { fprintf(stderr, "lumen: %s\n", err); return 1; }
    } else {
        char *src = read_file(input);
        if (!src) { fprintf(stderr, "lumen: cannot read %s\n", input); return 1; }
        prog = assemble(src, err, sizeof err);
        if (!prog) { fprintf(stderr, "lumen: %s: %s\n", input, err); free(src); return 1; }
        free(src);
    }

    if (save) {
        if (program_save(prog, save) != 0) {
            fprintf(stderr, "lumen: cannot write %s\n", save);
            program_free(prog);
            return 1;
        }
        program_free(prog);
        return 0;
    }

    if (dis) {
        disassemble(prog);
        program_free(prog);
        return 0;
    }

    struct vm vm;
    if (vm_run(&vm, prog) != 0) {
        fprintf(stderr, "lumen: %s\n", vm.err);
        program_free(prog);
        return 1;
    }
    program_free(prog);
    return 0;
}

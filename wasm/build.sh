#!/bin/sh
# Rebuild the browser demo in docs/ (requires emsdk on PATH).
set -e
cd "$(dirname "$0")/.."
emcc -O2 -Isrc src/vm.c src/asm.c wasm/lumen_wasm.c -o docs/lumen.js \
    -sEXPORTED_FUNCTIONS=_lumen_run \
    -sEXPORTED_RUNTIME_METHODS=ccall,cwrap \
    -sMODULARIZE=1 -sEXPORT_NAME=LumenModule \
    -sSTACK_SIZE=4194304 -sALLOW_MEMORY_GROWTH=1
# 4MB stack: the assembler and VM use large on-stack tables, fine for a
# native process but over Emscripten's 64KB default.

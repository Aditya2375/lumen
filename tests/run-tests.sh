#!/bin/sh
# lumen test suite: VM/assembler unit checks, then assemble+run every
# program and diff real stdout against the golden file.
set -e
cd "$(dirname "$0")/.."
OUT=build/tests
mkdir -p "$OUT"

cc -std=gnu11 -O2 -g -Wall -Wextra -Werror -Isrc \
    tests/unit/test_vm.c src/vm.c src/asm.c -o "$OUT/test_vm"
"$OUT/test_vm"

for src in tests/programs/*.lm examples/*.lm; do
    name=$(basename "$src" .lm)
    if [ -f "tests/programs/$name.expected" ]; then
        got=$(./build/lumen "$src")
        want=$(cat "tests/programs/$name.expected")
        if [ "$got" != "$want" ]; then
            echo "FAIL $name: output differs" >&2
            printf '%s\n' "$want" > "$OUT/want.txt"
            printf '%s\n' "$got" > "$OUT/got.txt"
            diff "$OUT/want.txt" "$OUT/got.txt" >&2 || true
            exit 1
        fi
        echo "PASS $name"
    else
        ./build/lumen "$src" > /dev/null
        echo "PASS $name (runs)"
    fi
done
echo "ALL TESTS PASSED"

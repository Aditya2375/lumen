#!/bin/sh
# Doc-claims guard: the unit-check count quoted in README.md must match
# what the suite actually runs.
set -e
cd "$(dirname "$0")/.."
OUT=build/docs-check
mkdir -p "$OUT"

cc -std=gnu11 -O2 -g -Wall -Wextra -Werror -Isrc \
    tests/unit/test_vm.c src/vm.c src/asm.c -o "$OUT/test_vm"
REAL=$("$OUT/test_vm" | grep -o '^[0-9]* checks passed' | grep -o '^[0-9]*')
for stated in $(grep -o '[0-9]* VM and assembler unit checks' README.md | grep -o '^[0-9]*'); do
    if [ "$stated" != "$REAL" ]; then
        echo "DOC DRIFT: README says $stated unit checks, suite runs $REAL"
        exit 1
    fi
done
echo "doc counts verified: $REAL checks"

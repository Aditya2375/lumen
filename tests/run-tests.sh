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
# binary save/load round-trip through the CLI
./build/lumen tests/programs/collatz.lm --save "$OUT/collatz.lmb"
dis_src=$(./build/lumen tests/programs/collatz.lm --disassemble)
dis_bin=$(./build/lumen "$OUT/collatz.lmb" --disassemble)
if [ "$dis_src" != "$dis_bin" ]; then
    echo "FAIL round-trip: disassembly differs after save/load" >&2
    exit 1
fi
got=$(./build/lumen "$OUT/collatz.lmb")
want=$(cat tests/programs/collatz.expected)
[ "$got" = "$want" ] || { echo "FAIL round-trip: binary run differs" >&2; exit 1; }
echo "PASS binary round-trip"

# adversarial binaries: the disassembler must describe corruption, not crash
python3 - "$OUT" <<'PY'
import struct, sys
out = sys.argv[1]
def write(name, code, consts):
    with open(f"{out}/{name}", "wb") as f:
        f.write(b"LMB1")
        f.write(struct.pack("<Q", len(code)))
        f.write(struct.pack("<Q", len(consts)))
        f.write(code)
        for c in consts:
            f.write(struct.pack("<q", c))
OP_CONST = 0
write("badconst.lmb", bytes([OP_CONST, 9, 0, 0, 0]), [42])     # pool has 1 entry, index 9
write("truncated.lmb", bytes([OP_CONST, 1, 0]), [42])           # operand cut short
write("badop.lmb", bytes([250]), [])                            # not an opcode
PY
got=$(./build/lumen "$OUT/badconst.lmb" --disassemble)
echo "$got" | grep -q "constant index out of range" || { echo "FAIL disasm: bad const index not flagged" >&2; exit 1; }
got=$(./build/lumen "$OUT/truncated.lmb" --disassemble)
echo "$got" | grep -q "truncated operand" || { echo "FAIL disasm: truncation not flagged" >&2; exit 1; }
got=$(./build/lumen "$OUT/badop.lmb" --disassemble)
echo "$got" | grep -q "???" || { echo "FAIL disasm: bad opcode not flagged" >&2; exit 1; }
echo "PASS adversarial disassembly"

# recursion depth: 1000 nested calls run, 1030 fault with a clean message.
# fn: count down local 0 (its argument), calling itself until it hits 0.
cat > "$OUT/rec.lm.body" <<'EOF'
fn:
load 0
const 1
sub
store 0
load 0
jump_if_not done
load 0
call fn 1
ret
done:
ret
EOF
for n in 1000 1030; do
    { printf 'jump start\n'; cat "$OUT/rec.lm.body"; printf 'start:\nconst %d\ncall fn 1\nhalt\n' "$n"; } > "$OUT/rec$n.lm"
done
./build/lumen "$OUT/rec1000.lm" > /dev/null || { echo "FAIL recursion: depth 1000 should run" >&2; exit 1; }
set +e
err=$(./build/lumen "$OUT/rec1030.lm" 2>&1)
rc=$?
set -e
[ "$rc" -ne 0 ] && echo "$err" | grep -q "call stack overflow" || { echo "FAIL recursion: depth 1030 should fault cleanly, got: $err" >&2; exit 1; }
echo "PASS recursion depth"

# assembler validation: only jumps and call targets take a label operand
for bad in "const loop" "load loop" "store loop" "call fn loop"; do
    printf '%s\nhalt\nloop:\nhalt\nfn:\nret\n' "$bad" > "$OUT/badop.lm"
    set +e
    err=$(./build/lumen "$OUT/badop.lm" 2>&1)
    rc=$?
    set -e
    [ "$rc" -ne 0 ] && echo "$err" | grep -q "needs a numeric operand" || {
        echo "FAIL assembler: '$bad' accepted or wrong error: $err" >&2; exit 1; }
done
echo "PASS assembler operand validation"

echo "ALL TESTS PASSED"

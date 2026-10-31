# lumen

A bytecode virtual machine with its own assembler. lumen assembles a small
text instruction set into compact bytecode and executes it on a stack
machine with call frames, locals, jumps, and checked faults - every bad
program fails with a named fault, never a crash.

```text
$ lumen examples/fact.lm
3628800
```

## Quick start

```bash
make
./build/lumen examples/fib.lm          # fib(0..9)
./build/lumen examples/fact.lm         # 10! via a recursive function
./build/lumen --disassemble examples/fact.lm
```

## The instruction set

All values are signed 64-bit integers. `const` takes any 64-bit literal;
jump targets and slot indices are 32-bit immediates.

### Arithmetic semantics

`add`, `sub`, `mul`, and `neg` wrap with two's-complement machine
semantics - defined behavior, never C's signed-overflow undefinedness.
`div` and `mod` fault cleanly on the two hardware traps: division by zero
and `INT64_MIN / -1`. Every fault names itself and the instruction pointer;
no input can crash the machine.

| Instruction | Effect |
|---|---|
| `const N` | push constant N (deduped in the constant pool) |
| `load i` / `store i` | read / write local slot i of the current frame |
| `pop` | discard the top of stack |
| `add sub mul neg` | wrapping two's-complement arithmetic |
| `div mod` | division; zero divisor or `INT64_MIN / -1` is a fault |
| `eq ne lt le gt ge` | comparisons, push 1 or 0 |
| `jump label` | unconditional jump |
| `jump_if label` / `jump_if_not label` | pop; jump when nonzero / zero |
| `call label argc` | call with argc arguments as the callee's locals 0..argc-1 |
| `ret` | return the top of stack to the caller |
| `print` | pop and print with a newline |
| `halt` | stop |

Labels end in `:` and are resolved by the assembler; `#` starts a comment.
A label may share its line with an instruction (`loop: const 1`).

## Binary programs

`lumen program.lm --save program.lmb` writes the assembled form in a small
`LMB1` container (code plus constant pool). Running or disassembling a
`.lmb` loads it directly - the disassembly of a loaded binary is identical
to the disassembly of its source, which the test suite round-trips.

## The execution model

- **Value stack** (4096 slots) holds operands; **locals live in the same
  stack** above the frame pointer, so calls are just a frame-pointer move
  and `ret` restores it.
- **Call stack** (1024 frames) records return addresses and caller frames;
  recursion depth is bounded by it and reported cleanly when exceeded.
- **Faults are checked**: stack underflow/overflow, division by zero, out-
  of-range locals or constants, an instruction pointer off the bytecode,
  `ret` outside a call. Each prints `fault at ip=N: reason`.

## Architecture

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Tests

```bash
./tests/run-tests.sh
```

58 VM and assembler unit checks (arithmetic, locals, jumps, call/ret,
constant-pool dedup, and every fault path), then every program under
`tests/programs/` and `examples/` is assembled and run for real with golden
stdout. CI runs the same script on every push, plus a docs job that fails
when a count quoted here drifts from the real suite.

## Tested / not tested

Tested here: 58 VM and assembler unit checks covering arithmetic
(including wrap-around edges), locals, jumps, call/ret, constant-pool
dedup, 64-bit constants, malformed numbers, same-line labels, every fault
path (stack overflow, underflow, bad opcode, bad constant index, truncated
instruction, wild jump, division traps), and binary save/load round-trips;
plus assemble-and-run golden programs including a recursive factorial.
Recursion is exercised at depth 1000 (runs) and 1030 (clean "call stack
overflow" fault at the 1024-frame limit). Adversarial binaries - a
constant index past the pool, a truncated trailing instruction, a byte
that is no opcode - are disassembled with the corruption spelled out
instead of a crash, and the assembler rejects label operands where a
number belongs (const/load/store/argc). The suite runs ASan/UBSan-clean.

Not tested: binaries with a valid header but nonsense pool values beyond
what the disassembler checks above, and timing/throughput benchmarks.

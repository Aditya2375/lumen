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

All values are signed 64-bit integers. Operands are 32-bit immediates.

| Instruction | Effect |
|---|---|
| `const N` | push constant N (deduped in the constant pool) |
| `load i` / `store i` | read / write local slot i of the current frame |
| `pop` | discard the top of stack |
| `add sub mul div mod neg` | arithmetic; division by zero is a fault |
| `eq ne lt le gt ge` | comparisons, push 1 or 0 |
| `jump label` | unconditional jump |
| `jump_if label` / `jump_if_not label` | pop; jump when nonzero / zero |
| `call label argc` | call with argc arguments as the callee's locals 0..argc-1 |
| `ret` | return the top of stack to the caller |
| `print` | pop and print with a newline |
| `halt` | stop |

Labels end in `:` and are resolved by the assembler; `#` starts a comment.

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

29 VM and assembler unit checks (arithmetic, locals, jumps, call/ret,
constant-pool dedup, and every fault path), then every program under
`tests/programs/` and `examples/` is assembled and run for real with golden
stdout. CI runs the same script on every push.

## Tested / not tested

Tested here: 29 VM and assembler unit checks covering arithmetic, locals,
jumps, call/ret, constant-pool dedup, and every fault path, plus
assemble-and-run golden programs including a recursive factorial.

Not tested: recursion near the 1024-frame call-stack limit under real
programs (the overflow fault itself is tested), and hand-crafted bytecode
(only assembler-produced programs are executed; there is no binary loader
to attack).

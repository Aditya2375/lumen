# lumen architecture

## Two halves

```text
text assembly -> bytecode program -> execution
     (asm.c)                      (vm.c)
```

The assembler and the VM share only the opcode table (`lumen.h`), so the
bytecode format is the contract: one-byte opcode, then 32-bit little-endian
operands.

## asm.c - the assembler

Single pass with fixups. Labels record the current code offset; symbolic
operands emit a zero and a fixup (offset, name, line) that a final sweep
patches with the resolved address - undefined labels and duplicates are
errors with line numbers. `const N` goes through a deduplicating constant
pool, so repeated literals cost one pool entry.

## vm.c - the interpreter

A fetch-decode-execute loop with three registers: `ip` (byte offset into
the code), `sp` (next free value-stack slot), and `fp` (base of the current
frame).

**One stack, two jobs.** Operands and locals share the value stack: a call
with N arguments makes those arguments the callee's locals 0..N-1 by
setting `fp = sp - N`. New locals (`store` past `sp`) widen the frame with
zeros. `ret` takes the top of the frame as the return value, pops the
frame, restores the caller's `fp` and the return address from the call
stack, and pushes the value. The discipline that makes this safe is pop
order: the call stack is LIFO, so the caller frame (pushed last) comes off
first.

**Every dynamic error is a fault, not a crash.** Bounds on `ip`, stack
over/underflow, division by zero, constant and local range checks, call-
stack depth, `ret` with no caller. The fault message carries the ip so a
bad program is debuggable from its own output.

## Why this shape

A stack machine needs no register allocator and the bytecode is trivially
serializable and disassemblable (`--disassemble` prints it with resolved
addresses and constant values). Frames-in-the-value-stack keeps calls to
three assignments and makes recursion work without any heap at all.

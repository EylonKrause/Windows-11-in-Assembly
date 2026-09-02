# harness/

Shared measurement code included by each change.

- **`bench.h`** — header-only benchmark harness. Pins a core, raises priority, warms cache, times
  minimum-of-N trials (robust to this machine's bad-RAM noise), runs a set of size classes, and prints an
  explicit **BETTER / WORSE / ~tie** verdict per size class plus an overall geomean. Returns non-zero if
  any size class regressed, so `build.bat` can gate on it.

Correctness harnesses live per-change in `correctness.c` — they compare our asm against both the scalar
reference and the **live system function** loaded from its system DLL on this PC.

## The contract a change's `impl.asm` must satisfy

- Win64 ABI: args in `rcx, rdx, r8, r9`; return in `rax`; caller-saved `rax rcx rdx r8 r9 r10 r11` and
  `xmm0-5`; everything else callee-saved (including `xmm6-15`, `rbx rbp rsi rdi r12-r15`).
- No read past the logical end of an input buffer into a potentially unmapped page. String/scan kernels
  must align down and mask, or use the standard "safe within the same 4 KB page" argument.
- Behavior **identical** to the Windows function replaced, including edge cases.

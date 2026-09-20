# 254 `FindStringOrdinal` — TGL variant → **LANDS**, 7.00× → 7.66×

**Bench:** #3, Intel Core i9-11900H (Tiger Lake-H) — [`docs/PLATFORM-i9-11900H.md`](../../docs/PLATFORM-i9-11900H.md).
`impl.asm` is untouched and remains the implementation of record; this records `impl_tgl.asm`.

**Correctness: PASS every run** — index *and* last-error exact against both the oracle and live
`kernelbase`, which is the parent's own gate, unchanged.

## It was parked here on exactly one row, and the sweep could not see it

The parent measures **6.997×** on this machine and parks, on a single row:

```
STARTSWITH 4000, yes    16.69    12.03    0.72x   WORSE
STARTSWITH 4000, no      7.25     7.73    1.07x   BETTER
ENDSWITH   4000, yes    10.36    12.60    1.22x   BETTER
```

That row had **never been reported**. `tools/revalidate.ps1` matched a bench row with a one-token
label, and this bench labels its rows in words, so the sweep parsed zero rows for this change and
published it as `LANDS geo=6.987` with no regressions. Fixing the parser is what surfaced it.

## The cause is not throughput — the needle is eight characters

The obvious reading of "4000" is a bandwidth problem, and it is wrong. [`bench.c`](bench.c) line 112
builds that row with a **4000-character haystack and an 8-character needle**, and `FIND_STARTSWITH`
compares at position 0 only. The whole call is an argument validation and one eight-character
compare — which is exactly what this change's own header says the row is dominated by.

So the 4.7 ns gap is the compare itself. The parent's `fo_verify` walks one character at a time:

```asm
fo_v1:  cmp   r10d, r9d
        jae   fo_v_ok
        lea   r11d, [rcx + r10]
        movzx r11d, word ptr [rsi + r11*2]
        cmp   r11w, word ptr [rdi + r10*2]
        jne   fo_v_no
        inc   r10d
        jmp   fo_v1
```

Eight iterations of a seven-µop body with a taken branch each — about twenty cycles, against one
wide compare.

## Why the parent is right to be scalar, and what changes here

A vector compare of an *m*-character needle wants to read a whole register, and *m* may be smaller
than that **and** may sit at the end of the haystack. Without AVX-512 that needs a bounds test and a
scalar fallback, which is most of what was being avoided. A **masked** load does not fault on a
masked-off element, so the tail needs neither:

```asm
        cmp       r9d, 32
        jb        fo_v_tail
        vmovdqu16 zmm0, zmmword ptr [r10 + r11]
        vpcmpw    k1, zmm0, zmmword ptr [rdi + r11], 4    ; 4 = not equal
        kortestd  k1, k1
        jnz       fo_v_no
...
fo_v_tail:
        mov       eax, -1
        bzhi      eax, eax, r9d          ; the characters that really exist
        kmovd     k2, eax
        vmovdqu16 zmm0{k2}{z}, zmmword ptr [r10 + r11]
        vmovdqu16 zmm1{k2}{z}, zmmword ptr [rdi + r11]
        vpcmpw    k1, zmm0, zmm1, 4
        kandd     k1, k1, k2             ; a lane past the needle can never disagree
```

Only the **case-sensitive** verifier changes. `fo_verify_ci` resolves each character through the
case-mate table and is left exactly as it was; the regressing row is case-sensitive.

The register contract is the parent's plus `rax`: `rax` is dead across both call sites (one is
followed by `mov eax, 3`, the other recomputes `ecx` from `edx`), and `edx` and `ebx` — which the
block loop depends on surviving *by design* — are untouched.

## Five runs

| run | correctness | geomean | `STARTSWITH 4000, yes` | rows WORSE | verdict |
|---|---|---:|---:|---|---|
| 1 | PASS | 7.702× | 1.75× | none | **LANDS** |
| 2 | PASS | 7.675× | 1.80× | none | **LANDS** |
| 3 | PASS | 7.487× | 1.96× | none | **LANDS** |
| 4 | PASS | 7.631× | 1.73× | none | **LANDS** |
| 5 | PASS | 7.529× | 1.83× | none | **LANDS** |

The regressing row goes **16.69 ns → 7.58 ns**, which is faster than `kernelbase`'s 12.03 rather
than merely level with it, and `STARTSWITH 4000, no` improves too (1.07× → 1.13×) because the same
compare serves both. Parent on the same machine: **6.997×, PARKED**.

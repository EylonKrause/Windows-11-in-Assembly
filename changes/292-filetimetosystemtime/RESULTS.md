# 292 — `kernel32!FileTimeToSystemTime` — **LANDS** (4.24×–4.45× geomean over seven runs; 4.8×–6.0× on the accepted rows)

- **Contract:** `BOOL FileTimeToSystemTime(const FILETIME* lpFileTime, LPSYSTEMTIME lpSystemTime)`
- **Compared against:** live `kernel32.dll!FileTimeToSystemTime` (RVA `0x57620`, a six-byte `jmp
  qword ptr [0x18008C2B0]` thunk onto `kernelbase.dll!FileTimeToSystemTime`, RVA `0xB3660`),
  resolved with `GetProcAddress`. `kernel32.dll` / `kernelbase.dll` / `ntdll.dll` all
  **10.0.26100.9278**, Windows 11 Pro 25H2 build **26200.9457**.
- **Bench:** #3, Intel Core i9-11900H (Tiger Lake-H) — `docs/PLATFORM-i9-11900H.md`.
- **ISA:** BMI2 (`mulx`) and nothing else. No vector instruction of any width, therefore no CPUID
  dispatch and no fallback path — the same baseline change 126 runs on, valid on bench #1 (Zen 3,
  no AVX-512) as well as here.
- **Gates:** correctness **210,826,291 cases / 0 mismatches**; speed **no size class regressed**
  (every row BETTER on all seven runs); ABI **static PASS** and **dynamic PASS on five call
  shapes** including the one that makes a call; vector-re-entry audit clean (no vector loop exists).

`discovery/momentary_tier2.c` measured the shipped export on this machine at **36.00 ns per call,
flat**, bound by **95 startup modules** and a matching desktop count. `discovery/momentary-tier2-timings.md`
is explicit that flat is not a verdict here: a `FILETIME` is always eight bytes, so flat is the
shape of a fixed-size input and the verdict has to come from the magnitude. 36 ns for arithmetic on
one 64-bit integer is the magnitude this change goes after.

---

## What the shipped implementation actually does

Three functions, disassembled before anything was written.

### `kernel32!FileTimeToSystemTime` — RVA `0x57620`, the whole of it

```
180057620: FF 25 8A 4C 03 00   jmp   qword ptr [18008C2B0h]     ; -> kernelbase
```

Not a PE forwarder string — a real six-byte thunk through kernel32's own IAT. `GetProcAddress` on
kernel32 returns `0x...7620` and on kernelbase returns a different address, which `probes/contract.c`
prints side by side. The benchmark deliberately calls the **kernel32** entry, because that is the
one 95 startup modules import.

### `kernelbase!FileTimeToSystemTime` — RVA `0xB3660`, the whole of it

```
1800B3660: push  rbx
1800B3662: sub   rsp,40h
1800B3666: mov   rax,qword ptr [1803A90D0h]      ; __security_cookie
1800B366D: xor   rax,rsp
1800B3670: mov   qword ptr [rsp+38h],rax         ; ... a stack cookie, for this
1800B3675: mov   eax,dword ptr [rcx]             ; dwLowDateTime
1800B3677: xorps xmm0,xmm0
1800B367A: mov   dword ptr [rsp+20h],eax         ; copy the FILETIME onto its own stack
1800B367E: mov   rbx,rdx                         ; lpSystemTime
1800B3681: mov   eax,dword ptr [rcx+4]           ; dwHighDateTime
1800B3684: mov   dword ptr [rsp+24h],eax
1800B3688: cmp   qword ptr [rsp+20h],0           ; SIGNED test of the 64-bit time
1800B368E: movups xmmword ptr [rsp+28h],xmm0     ; zero a 16-byte TIME_FIELDS it is about to fill
1800B3693: jl    1800B370E                       ; negative -> fail
1800B3695: xor   r8d,r8d                         ; LeapSecondData argument = NULL
1800B3698: lea   rdx,[rsp+28h]                   ; &TimeFields
1800B369D: lea   rcx,[rsp+20h]                   ; &Time
1800B36A2: call  qword ptr [18029B960h]          ; ntdll!RtlpTimeToTimeFields
1800B36AE: movzx eax,word ptr [rsp+28h]          ; and now EIGHT loads and EIGHT stores,
1800B36B3: mov   word ptr [rbx],ax               ;   because SYSTEMTIME is a PERMUTATION
1800B36B6: movzx eax,word ptr [rsp+2Ah]          ;   of TIME_FIELDS, not the same layout
1800B36BB: mov   word ptr [rbx+2],ax
1800B36BF: movzx eax,word ptr [rsp+2Ch]          ; TIME_FIELDS.Day    -> SYSTEMTIME.wDay   (+6)
1800B36C4: mov   word ptr [rbx+6],ax
1800B36C8: movzx eax,word ptr [rsp+36h]          ; TIME_FIELDS.Weekday-> SYSTEMTIME.wDayOfWeek (+4)
1800B36CD: mov   word ptr [rbx+4],ax
1800B36D1: movzx eax,word ptr [rsp+2Eh]
1800B36D6: mov   word ptr [rbx+8],ax
1800B36DA: movzx eax,word ptr [rsp+30h]
1800B36DF: mov   word ptr [rbx+0Ah],ax
1800B36E3: movzx eax,word ptr [rsp+32h]
1800B36E8: mov   word ptr [rbx+0Ch],ax
1800B36EC: movzx eax,word ptr [rsp+34h]
1800B36F1: mov   word ptr [rbx+0Eh],ax
1800B36F5: mov   eax,1                           ; TRUE. The last error is NOT touched.
1800B36FA: mov   rcx,qword ptr [rsp+38h]
1800B36FF: xor   rcx,rsp
1800B3702: call  180196EC0                       ; __security_check_cookie
1800B3707: add   rsp,40h
1800B370B: pop   rbx
1800B370C: ret
1800B370E: mov   ecx,57h                         ; ERROR_INVALID_PARAMETER
1800B3713: call  qword ptr [18029A860h]          ; ntdll!RtlSetLastWin32Error
1800B371F: xor   eax,eax                         ; FALSE, and *lpSystemTime is NEVER written
   ... same cookie check and epilogue ...
```

The two imports were identified by index against kernelbase's ntdll IAT (`0x18029A6E0`):
`(0xB960-0xA6E0)/8 = 592` → **`RtlpTimeToTimeFields`**, `(0xA860-0xA6E0)/8 = 48` →
**`RtlSetLastWin32Error`**.

### `ntdll!RtlpTimeToTimeFields` — RVA `0xC16A0`, and why it is the 36 ns

The import is **not** `RtlTimeToTimeFields`. It is the private three-argument form, and
`ntdll!RtlTimeToTimeFields` (RVA `0xEE4D0`) is a two-instruction thunk onto it:

```
1800EE4D0: xor  r8d,r8d
1800EE4D3: jmp  1800C16A0                        ; RtlpTimeToTimeFields
```

which means **kernelbase is calling exactly the routine change 126 replaced**, with the same third
argument kernelbase passes. `RtlpTimeToTimeFields` opens with a leap-second test:

```
1800C16A6: mov   rax,qword ptr gs:[60h]          ; PEB
1800C16BE: mov   r10,qword ptr [rax+7B8h]        ; PEB.LeapSecondData
1800C16C5: test  r10,r10       / je  plain_body
1800C16CE: cmp   byte ptr [r10],r9b / je plain_body
1800C16DC: mov   ebx,dword ptr [r10+4]           ; leap-second COUNT
1800C1715: test  ebx,ebx       / jne scan_leap_table
```

`probes/leapdata.c` reads that structure on this machine: `LeapSecondData` is **non-NULL**,
`[+0] = 1`, **`[+4] = 0`**, and `PEB.LeapSecondFlags = 0`. So the first two tests fall through to
the inlined body, the count is zero, the scan is never entered and the arithmetic that executes is
the unmodified civil-from-days path. The probe also sweeps every minute boundary from 1972 to 2024
and finds **zero** instants where `wSecond >= 60`, which is the only way a leap second could become
observable. That is the one contract assumption this change makes about the machine, it is checked
rather than assumed, and `correctness.c` re-checks the consequence against the live export on every
run.

---

## The contract, as PROVED — `probes/contract.c`

Nothing below is from documentation. Change 289 proved MSDN wrong about `WideCharToMultiByte` in
three separate places this week, so every question was answered by observation.

| question | answer, from the live export |
|---|---|
| negative `FILETIME`? | `ret = 0`, `GetLastError() == 87` (`ERROR_INVALID_PARAMETER`), and `*lpSystemTime` **left byte-for-byte untouched** — verified for `-1`, `0x8000000000000000`, `-1 day`, `-1 second` |
| validated before or after the store? | **before**: a negative time with `lpSystemTime = NULL` returns `0`/`87` instead of faulting |
| last error on **success**? | **not touched** — a `0xDEADBEEF` sentinel written before every call survives every accepted input |
| is `wDayOfWeek` filled in? | **yes**, `(days + 1) mod 7`; the first eight days of 1601 come back `1,2,3,4,5,6,0,1`, i.e. 1601-01-01 is a **Monday** and Sunday is 0 |
| `FILETIME == 0`? | accepted: `1601-01-01 00:00:00.000`, `wDayOfWeek = 1` |
| past year 30827? | **no upper bound exists.** `0x7FFFFFFFFFFFFFFF` is ACCEPTED and returns `30828-09-14 02:48:05.477`, `ret = 1` |
| `lpFileTime = NULL` / `lpSystemTime = NULL`? | access violation, `0xC0000005`, in both directions — not emulated, and not emulable without an SEH contract the export does not have |
| is kernel32's export kernelbase's code? | no — kernel32's is a `jmp` thunk; the bytes are `FF 25 8A 4C 03 00` |

### Where the boundary is between "validate here" and "hand it to the engine"

This is the whole design of a Win32 wrapper over an NT routine, and it is why this file is not just
a call.

Change 126's `RESULTS.md` records that ntdll's output for a **negative** `Time` is internally
overflowed garbage — `-1 day` comes back as year 29878, and `Weekday` stops being monotonic across
consecutive days — and that 126 **documents and does not emulate** it. That left exactly one class
of input undefined for the engine.

The Win32 wrapper never lets that class reach the engine. It tests the sign first and fails. So
126's undefined class and this layer's rejected class are **the same class**, and the two fit
together with no gap and no overlap. In the other direction the wrapper adds **no** upper bound, so
everything the sign test admits is inside the engine's declared domain. The validation is exactly
one `test`/`js`, and it is the only validation there is.

---

## The shipped implementation, disassembled — `dumpbin /disasm impl.obj`

363 bytes, one forward branch, no prologue, no stack frame on the accepted path.

```
wia_filetime_to_systemtime:
  000: 4C 8B 11        mov   r10,qword ptr [rcx]      ; one 8-byte read of an 8-byte FILETIME
  003: 4C 8B DA        mov   r11,rdx
  006: 4D 85 D2        test  r10,r10
  009: 0F 88 47 01..   js    156                      ; the only validation, not taken on success
  00F: 49 8B D2        mov   rdx,r10
  012: 48 B8 ..        mov   rax,0A2E3FF1DE20581E3h
  01C: C4 62 BB F6 C8  mulx  r9,r8,rax
  021: 49 C1 E9 27     shr   r9,27h                   ; days = T / 864000000000
  025: 48 B8 ..        mov   rax,0C92A69C000h
  02F: 49 0F AF C1     imul  rax,r9
  033: 4C 2B D0        sub   r10,rax                  ; rem  -- both chains now start from T
  036: 49 8D 41 01     lea   rax,[r9+1]               ; wDayOfWeek = (days+1) mod 7
  03A: 48 69 C8 ..     imul  rcx,rax,24924925h
  041: 48 C1 E9 20     shr   rcx,20h
  045: 48 6B C9 07     imul  rcx,rcx,7
  049: 48 2B C1        sub   rax,rcx
  04C: 66 41 89 43 04  mov   word ptr [r11+4],ax
  051: 49 8D 81 ..     lea   rax,[r9+8EBF6h]          ; N   = days + 584694
  058: 48 8D 04 85 ..  lea   rax,[rax*4+3]            ; N_1 = 4N+3
  060: 48 69 C8 ..     imul  rcx,rax,0E5AC1Bh
  067: 48 C1 E9 29     shr   rcx,29h                  ; C   = N_1/146097     (century)
  06B: 48 69 D1 ..     imul  rdx,rcx,23AB1h
  072: 48 2B C2        sub   rax,rdx
  075: 48 C1 E8 02     shr   rax,2                    ; N_C
  079: 48 8D 04 85 ..  lea   rax,[rax*4+3]            ; N_2
  081: 48 69 C0 ..     imul  rax,rax,2CDB61h          ; P_2 = 2939745*N_2
  088: 48 8B D0        mov   rdx,rax
  08B: 48 C1 EA 20     shr   rdx,20h                  ; Z   = year in century
  08F: 8B C0           mov   eax,eax                  ; low 32 bits of P_2
  091: 48 69 C0 ..     imul  rax,rax,5B4FFFCBh
  098: 48 C1 E8 36     shr   rax,36h                  ; N_Y = day of the March year
  09C: 48 6B C9 64     imul  rcx,rcx,64h
  0A0: 48 03 CA        add   rcx,rdx                  ; Y   = 100*C + Z
  0A3: 48 69 D0 5D 08  imul  rdx,rax,85Dh
  0AA: 48 81 C2 ..     add   rdx,30519h               ; N_3 = 2141*N_Y + 197913
  0B1: 4C 8B C2        mov   r8,rdx
  0B4: 49 C1 E8 10     shr   r8,10h                   ; M   = N_3>>16   (3..14)
  0B8: 81 E2 FF FF ..  and   edx,0FFFFh
  0BE: 48 69 D2 ..     imul  rdx,rdx,7A71h
  0C5: 48 C1 EA 1A     shr   rdx,1Ah                  ; D
  0C9: 48 FF C2        inc   rdx                      ; wDay
  0CC: 45 33 C9        xor   r9d,r9d
  0CF: 48 3D 32 01 ..  cmp   rax,132h                 ; N_Y >= 306  ->  Jan/Feb
  0D5: 41 0F 93 C1     setae r9b
  0D9: 49 03 C9        add   rcx,r9                   ; wYear
  0DC: 4D 6B C9 0C     imul  r9,r9,0Ch
  0E0: 4D 2B C1        sub   r8,r9                    ; wMonth
  0E3: 66 41 89 0B     mov   word ptr [r11],cx
  0E7: 66 45 89 43 02  mov   word ptr [r11+2],r8w
  0EC: 66 41 89 53 06  mov   word ptr [r11+6],dx
  0F1: 49 8B D2        mov   rdx,r10
  0F4: 48 B8 ..        mov   rax,68DB8BAC710CCh
  0FE: C4 E2 BB F6 C8  mulx  rcx,r8,rax               ; msday = rem/10000   (0..86399999)
  103: 48 69 C1 ..     imul  rax,rcx,25485F3h
  10A: 48 C1 E8 2F     shr   rax,2Fh                  ; wHour        = msday/3600000
  10E: 48 69 D1 ..     imul  rdx,rcx,8BCF65h
  115: 48 C1 EA 27     shr   rdx,27h                  ; minute of day= msday/60000
  119: 4C 69 C1 ..     imul  r8,rcx,4189375h
  120: 49 C1 E8 24     shr   r8,24h                   ; second of day= msday/1000
  124: 4D 69 C8 ..     imul  r9,r8,3E8h
  12B: 49 2B C9        sub   rcx,r9                   ; wMilliseconds
  12E: 66 41 89 4B 0E  mov   word ptr [r11+0Eh],cx
  133: 4C 6B CA 3C     imul  r9,rdx,3Ch
  137: 4D 2B C1        sub   r8,r9                    ; wSecond
  13A: 66 45 89 43 0C  mov   word ptr [r11+0Ch],r8w
  13F: 4C 6B C8 3C     imul  r9,rax,3Ch
  143: 49 2B D1        sub   rdx,r9                   ; wMinute
  146: 66 41 89 53 0A  mov   word ptr [r11+0Ah],dx
  14B: 66 41 89 43 08  mov   word ptr [r11+8],ax      ; wHour
  150: B8 01 00 00 00  mov   eax,1                    ; TRUE, last error untouched
  155: C3              ret
  156: B9 57 00 00 00  mov   ecx,57h                  ; the reject path, cold and out of line
  15B: 48 83 EC 28     sub   rsp,28h
  15F: E8 00 00 00 00  call  SetLastError
  164: 48 83 C4 28     add   rsp,28h
  168: 33 C0           xor   eax,eax                  ; FALSE, *lpSystemTime never written
  16A: C3              ret
```

Everything the shipped wrapper spends on scaffolding is gone: no `__security_cookie` load, no
`__security_check_cookie` call, no copy of the `FILETIME` onto a second stack slot, no `movups`
zero-fill of a structure about to be overwritten, no indirect call, and — because each field is
written straight into its `SYSTEMTIME` slot — **the eight-field permutation does not exist as work
at all**. What remains is the date arithmetic.

### What is new here, and what is borrowed

The engine is change **126**'s in spirit but **not** in code. 126 uses Hinnant's era-based
civil-from-days, whose dependency chain from `days` to `Day` is about eight serial multiplies. This
file uses the **Neri–Schneider** form, which reaches the same three fields in five, and it moves the
time of day out of a serial 64-bit remainder chain into **one** wide divide followed by three
*independent* 32-bit-range divides of a single millisecond-of-day value.

That decision was measured, not assumed. A straight port of 126's arithmetic into this wrapper was
written first, passed the same exhaustive gate, and benchmarked over six runs:

| version | ours ns (7 runs / 6 runs) | geomean vs live |
|---|---|---|
| Hinnant port of change 126 | 9.67 – 11.00 | 3.099× – 3.232× |
| **Neri–Schneider, shipped** | **7.18 – 8.76** | **4.238× – 4.454×** |

126 also reports that computing the four time-of-day fields as *parallel* chains measured **slower**
there (1.60× against 1.73×). That result does not transfer and the difference is the reason: in 126
the four divides were all wide 64-bit ones and the chain was hidden underneath a longer calendar
anyway, so the extra instructions were pure throughput cost. Here the calendar is twelve cycles
shorter, which makes the time-of-day chain the thing that would have become critical, and after the
single `mulx` to milliseconds the remaining divides are cheap. Same question, different answer,
because the surrounding code changed — which is why it was re-measured instead of inherited.

### Registers — zero non-volatiles

`rax rcx rdx r8 r9 r10 r11` only, so there is **no prologue, no push and no pop** on the accepted
path. 126 spilled `rsi/rdi/r12/r13` for the same job. The four were recovered by emitting the
calendar and the time-of-day blocks **sequentially** and letting them share five scratch registers:
register renaming removes the write-after-read dependency between the blocks, so they still overlap
in the out-of-order window. The measurement confirms it — the two chains are about 30 and 22 cycles
and the whole function costs about 35, not 52.

### Every magic number, and how it was verified — `probes/magics.c`

```
days   = mulhi(T,0xA2E3FF1DE20581E3)>>39   OK   every quotient boundary, T in [0,2^63)
msday  = mulhi(rem,0x68DB8BAC710CC)        OK   every quotient boundary, rem in [0,864e9)
weekday= n - 7*((n*0x24924925)>>32)        OK   EXHAUSTIVE, n in [1,10675200]
C      = (N_1*15051803)>>41                OK   EXHAUSTIVE, N_1 in [0,45039575]
N_Y    = (low32(P_2)*1531969483)>>54       OK   every quotient boundary, u in [0,2^32)
D      = ((N_3 & 0xFFFF)*31345)>>26        OK   EXHAUSTIVE, u in [0,65535]
hour/min/sec divides of msday              OK   EXHAUSTIVE, msday in [0,86399999]
```

"Every quotient boundary" is a proof and not a sample: `floor(u/d)` steps only at multiples of `d`
and `(u*M)>>s` is non-decreasing, so agreement at `u = q*d` and `u = q*d - 1` for every `q` forces
agreement at every `u` between them. This is the standard change 126 set for its constants, and the
two constants this change shares with 126 (`days` and the weekday) were re-proved here rather than
inherited.

**One magic was found, verified, and thrown away.** The first version of `magics.c` verified each
constant only over the operand set the surrounding algorithm can actually present — 36 525 distinct
values for `/11758980`, 366 for `/2141` — and returned `(u*1461)>>34` for `/11758980`. It is exact
on every operand this code will ever hand it, and wrong above `u = 28 825 619`, against an operand
that is a 32-bit quantity. It would have passed every gate in this repository and been a trap for
the next reader. The search was reconstrained to each operand's **full natural range** and the
shipped constant is `(u*1531969483)>>54`, exact over all of `[0, 2^32)`.

---

## Correctness — `correctness.exe`: **PASS**, 210,826,291 cases, 0 mismatches

Three-way, every case: ours vs the **live** `kernel32!FileTimeToSystemTime` vs `reference.c`.
Compared each time: the `BOOL`, **the last error** (a `0xDEADBEEF` sentinel is written before every
call, so "did not touch it" is as observable as "set it to 87"), all sixteen bytes of the
`SYSTEMTIME`, and 32 canary bytes on each side of it.

`reference.c` is deliberately a **different algorithm**, so agreement is evidence and not a
tautology: it finds the year by peeling 400-, 100-, 4- and 1-year blocks off the day count with the
two `== 4` clamps, and the month by walking a twelve-entry table with an explicit leap-year test,
using real C `/` and `%` throughout. It was proved against the live export **before a line of
assembly was written** (`probes/refonly.c`: 38,025,617 cases, 0 fails).

The repository's corpus minimum is written for a function with a length axis. This one's input is a
fixed eight bytes, so "every length up to twice the vector width" and "the match at every position"
were translated into exhausting the two axes the value actually has:

| corpus | count | what it settles |
|---|---:|---|
| **every day boundary in the domain**, at midnight, at the last tick of the day, and at an interior instant | 10,675,200 × 3 | the calendar is **exhausted**: the date fields depend on nothing but the day count, so this is all of it, not a sample |
| **every millisecond-of-day quotient boundary**, at `k*10000` and `k*10000 - 1` | 86,400,000 × 2 | the time of day is **exhausted**: `wHour/wMinute/wSecond/wMilliseconds` are all functions of `floor(rem/10000)` because 10000 divides 10⁷, 6·10⁸ and 3.6·10¹⁰, and `floor(n/d)` steps only at multiples of `d` |
| edges | 25 | `0`; 1 tick; 9999 and 10000 ticks; the last tick of day 0; 1970; `0x7FFFFFFFFFFFFFFF` and `−1` from it; the 1700 skipped-leap-year region; the 2000 cycle boundary; `−1`, `INT64_MIN`, `−1 day`, `−1 second` |
| **unaligned**: the `FILETIME` at all 8 byte offsets of a qword × the `SYSTEMTIME` at all 16 | 8 × 16 × 5 | no alignment assumption anywhere |
| **page boundary**: a `FILETIME` whose last byte is a page's last byte, with the next page not committed; a `SYSTEMTIME` likewise; and both at once | 9 × 3 | a wide load or a wide store would fault. The only read is one 8-byte load of the 8-byte structure and the only writes are eight 2-byte stores inside the 16-byte structure — deliberately **not** one 16-byte store |
| **canaries** 32 bytes either side, checked on every case | all | nothing is written past the logical end |
| **reject path writes nothing**: the 0xCD pre-fill must survive in all three | every negative case | the contract point that makes `lpSystemTime = NULL` safe on a negative time |
| **fuzz**, fixed seed `0x9E3779B97F4A7C15` | 3,000,000 positive + 3,000,000 unrestricted (≈half reject) | mixes the two axes, which the exhaustive sweeps hold one of fixed |

## ABI — gate 3

- **Static** (`py tools/abi-audit.py .`): **PASS**, 605 `.asm` files, no implementation writes a
  non-volatile register without saving it. This one writes none at all.
- **Dynamic** (`probes/abi.c` + `probes/abi.asm`): **PASS**. The repository's dynamic gate lives in
  `tools/abi-check/check.bat` and this change may not modify an existing file, so the same probe is
  written here in the one shape that cannot be masked — the sentinels are armed by hand immediately
  before the `call`, with no compiled C in between. Both paths are driven, because the reject path
  builds a shadow frame and **calls** `SetLastError`:

```
   accept path (2023)                   all 8 GPRs and xmm6-xmm15 survived
   accept path (epoch 0)                all 8 GPRs and xmm6-xmm15 survived
   accept path (max)                    all 8 GPRs and xmm6-xmm15 survived
   reject path (-1, calls SetLastError) all 8 GPRs and xmm6-xmm15 survived
   reject path (INT64_MIN)              all 8 GPRs and xmm6-xmm15 survived
   ABI DYNAMIC: PASS
```

**Vector re-entry** (`py tools/vector-reentry-audit.py`): clean — there is no vector loop and no
scalar walk to re-enter one. The only branch in the function is the forward `js` to the cold reject
path.

---

## Benchmark — vs live `kernel32!FileTimeToSystemTime`

**A single run is not a measurement on this laptop**, so here are seven, back to back. Other agents
may have been running; these are indicative and will be re-measured serially.

| input class | ours ns (min–max over 7) | live ns (min–max) | ratio (min–max) | verdict |
|---|---|---|---|---|
| 1601 epoch | 7.35 – 8.52 | 39.47 – 46.78 | 5.05× – 5.67× | BETTER ×7 |
| 1970 | 7.18 – 8.76 | 38.67 – 45.87 | 4.80× – 5.38× | BETTER ×7 |
| 2023 | 7.35 – 8.24 | 39.37 – 44.15 | 5.06× – 5.51× | BETTER ×7 |
| max accepted (`0x7FFF…`) | 7.22 – 8.51 | 39.38 – 44.17 | 4.76× – 6.00× | BETTER ×7 |
| negative (reject path) | 4.57 – 5.23 | 9.48 – 10.35 | 1.91× – 2.14× | BETTER ×7 |
| **geomean** | | | **4.238× – 4.454×** | **LANDS ×7** |

Run-by-run geomeans: `4.238, 4.426, 4.317, 4.290, 4.284, 4.339, 4.454`. No size class regressed on
any run, and no row ever fell below 1.91×. A final clean `build.bat` afterwards produced an eighth
run — `5.12/5.36/5.37/5.36/1.98`, geomean **4.352×** — every number of which falls inside the bands
above, which is the point of reporting bands.

The accepted rows are flat to within the noise, which is the expected shape: the arithmetic is
branch-free and data-independent, so 1601, 1970, 2023 and year 30828 cost the same. The reject row
is a separate code path in both implementations and so is reported separately; ours is about 2×
because the shipped one pays for its stack cookie and frame *before* it reaches the sign test, while
this one tests the sign in the third instruction and the only remaining cost is the real
`SetLastError` call.

### Why the reject path calls `SetLastError` instead of storing to the TEB

`ntdll!RtlSetLastWin32Error` is **not** an unconditional store:

```
180072384: mov  rax,qword ptr gs:[30h]           ; TEB
18007238D: mov  edx,dword ptr [1801CE758h]       ; a global tracing flag
180072393: test edx,edx  / jne  1800723B2        ; ... side path when it is set
180072397: cmp  dword ptr [rax+68h],ecx
18007239C: mov  dword ptr [rax+68h],ecx
```

Writing `gs:[68h]` by hand would be indistinguishable to `GetLastError` and would silently drop that
side path, and the flag's address is an internal, version-specific datum. The real call is made
instead. It costs a 32-byte shadow frame on a path that runs only for an invalid argument, and the
path is still ~2× the shipped one.

---

## Scope and honest limits

- **`Time >= 0` is the whole accepted domain**, 1601-01-01 through 30828-09-14, and every day in it
  is tested. Negative times are rejected exactly as the live export rejects them; change 126's
  documented non-emulation of ntdll's negative-`Time` garbage is not reachable through this entry
  point, by construction.
- **NULL pointers are not emulated.** Both faults; reproducing that would mean reproducing an SEH
  contract the export does not have. Not tested, not claimed.
- **Leap seconds.** If a machine ever had `PEB.LeapSecondData[+4] != 0` or
  `PEB.LeapSecondFlags & 1`, `RtlpTimeToTimeFields` would take its leap-second scan and could return
  `wSecond == 60`, which this implementation cannot produce. On this machine the count is 0, the
  flags are 0, and no minute boundary between 1972 and 2024 yields a second ≥ 60 (`probes/leapdata.c`).
  Because `correctness.c` resolves its comparand with `GetProcAddress` against the live export, any
  machine where that stops being true fails the gate rather than shipping a silent difference.
- **Bench #3 only so far.** The implementation uses nothing above the BMI2 baseline, so it is
  expected to run and to win on bench #1 and #2 as well, but it has not been measured there.

## Reproduce

```
changes\292-filetimetosystemtime\build.bat
```

On a machine whose Visual Studio is not BuildTools at the hardcoded path, run
`. tools\vsenv.ps1 -Quiet` first and invoke `build.bat` by full path
(`docs/PLATFORM-i9-11900H.md`, *Toolchain*).

Probes (throwaway, not gates):

```
cl /nologo /O2 /EHa probes\contract.c        &&  contract.exe    (the contract table above)
cl /nologo /O2      probes\leapdata.c        &&  leapdata.exe    (the leap-second state)
cl /nologo /O2      probes\magics.c          &&  magics.exe      (every constant, re-verified)
cl /nologo /O2      probes\refonly.c reference.c  &&  refonly.exe (the oracle vs live, alone)
ml64 /nologo /c /Fo abiasm.obj probes\abi.asm
cl /nologo /O2 probes\abi.c abiasm.obj impl.obj kernel32.lib     (gate 3, dynamic)
```

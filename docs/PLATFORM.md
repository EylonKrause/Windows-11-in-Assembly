# Validation bench — captured platform facts

Every measurement in this repository is taken on this machine unless a `RESULTS.md` says otherwise.
Captured 2026-09-02.

## Operating system

| Field | Value |
|---|---|
| Marketing version | Windows 11, 25H2 |
| Build | 26200.8655 |
| BuildLab | 26100.1.amd64fre.ge_release.240331-1435 |
| Registry `ProductName` | "Windows 10 Pro" — **stale key**, Microsoft never rewrote it on the 11 upgrade; the build number (26200) is the source of truth |

Note: the original project prompt referenced "26H2". This bench is **25H2** (build 26200). Any routine
disassembled as a baseline is disassembled from *this* build; a different build may ship a different
implementation and must be re-baselined.

## CPU

| Field | Value |
|---|---|
| Model | AMD Ryzen 9 5950X 16-Core Processor |
| Microarchitecture | Zen 3 (family 19h) |
| Topology | 16 cores / 32 threads |
| Total RAM | 31.9 GB |

### ISA feature probe (CPUID, leaf 1 / 7 / 0x80000001)

```
SSE2=1  SSE4.2=1  AVX=1  FMA=1  POPCNT=1
AVX2=1  BMI1=1  BMI2=1  AVX512F=0  SHA=1  ADX=1  RDSEED=1
VAES=1  VPCLMUL=1  GFNI=0  LZCNT/ABM=1
```

**Emit-and-run baseline for this bench:** `x86-64-v3` superset —
SSE2, SSSE3, SSE4.1/4.2, AVX, **AVX2**, FMA3, **BMI1/BMI2**, POPCNT, LZCNT/ABM, ADX, RDSEED,
plus **SHA-NI**, **VAES**, **VPCLMULQDQ**.

**Absent:** AVX-512 (all subsets), GFNI.

Implication: hand-written kernels validated here top out at 256-bit (YMM). A 512-bit variant may be
written for portability but is unvalidated on this bench — it needs a Zen4/AVX-512 machine to measure.

## Security / boot configuration

| Field | Value | Effect on this project |
|---|---|---|
| Secure Boot | **ON** | Kernel-mode code signing enforced → no unsigned/modified kernel binaries load |
| HVCI (memory integrity) | **OFF** | Kernel CI not hypervisor-enforced, but KMCS still applies via Secure Boot |
| VBS | **OFF** | — |
| Session elevation | **Non-elevated** | Cannot write `C:\Windows\System32`; live deployment is out of scope until run elevated + test-signed |
| Free space on C: | ~905 GB | Ample for disassembly dumps and build artifacts |

## Toolchain

| Tool | Version / path |
|---|---|
| MASM | `ml64` 14.50.35728 — `…\VS 2026 BuildTools\VC\Tools\MSVC\14.50.35717\bin\Hostx64\x64\ml64.exe` |
| C/C++ | `cl` 14.50, `link` 14.50 |
| Disassembler | `dumpbin` 14.50 (`/disasm`, `/headers`) |
| Not present | clang, nasm, objdump, radare2 |

The `ml64 → link → run` round trip is verified working (a hand-written `.asm` function called from C
returns the expected value). This is the pipeline every change is built and validated through.

### Bench hygiene notes

- **Bad RAM.** This machine has documented WHEA machine-check errors and BCD-blacklisted pages. Treat
  single-run timings as noisy; every benchmark takes the minimum of many trials on a pinned core and is
  cross-checked for run-to-run stability before any number is recorded.
- `cmd.exe` here has `NoDefaultCurrentDirectoryInExePath` set — built executables must be invoked by full
  path in scripts, not by bare name.

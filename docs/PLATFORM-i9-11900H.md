# Validation bench #3 — Intel Core i9-11900H (Tiger Lake-H)

Captured 2026-09-20. This is the **third** validation machine for this project. The two before it are
AMD; this one is the first Intel bench, and the first with AVX-512. Read `docs/PLATFORM.md` for bench #1
(Ryzen 9 5950X, Zen 3) — that is the machine every geomean in `SUMMARY.md` was measured on.

Why a separate file rather than an edit: a `RESULTS.md` number is only meaningful next to the machine it
was taken on. Overwriting `PLATFORM.md` would silently re-attribute 288 changes' measurements to hardware
that never ran them.

## Operating system

| Field | Value |
|---|---|
| Marketing version | Windows 11 Pro, 25H2 |
| Build | **26200.9457** |
| Bench #1 build | 26200.8655 |

**This matters more than it looks.** Bench #1's disassembly baselines were taken from 26200.8655. This
machine runs binaries serviced past that point:

| DLL | version here |
|---|---|
| ntdll.dll | 10.0.26100.9278 |
| ucrtbase.dll | 10.0.26100.9444 |
| kernelbase.dll | 10.0.26100.9278 |
| shlwapi.dll | 10.0.26100.8117 |
| combase.dll | 10.0.26100.7705 |
| crypt32.dll / iphlpapi.dll / rpcrt4.dll | 10.0.26100.1 |

Every `correctness.c` in this repository resolves its comparand with `GetProcAddress` against the **live**
export, so a sweep here is not a re-run of an old result — it is a fresh proof of each contract against
newer Windows code. A change that passes on both benches has been proven against two different builds of
the function it replaces.

## CPU

| Field | Value |
|---|---|
| Model | 11th Gen Intel Core i9-11900H @ 2.50 GHz |
| Microarchitecture | **Tiger Lake-H (Willow Cove)**, family 0x6 model **0x8D** stepping 1 |
| Topology | 8 cores / 16 threads (**not** hybrid — `HYBRID=0`, no E-cores) |
| Total RAM | 31.7 GB |

### Cache hierarchy (CPUID leaf 4)

| Level | Size | Ways | Line |
|---|---|---|---|
| L1 data | **48 KB** | 12 | 64 B |
| L1 inst | 32 KB | 8 | 64 B |
| L2 unified | **1280 KB** | 20 | 64 B |
| L3 unified | 24 MB (shared) | 12 | 64 B |

Willow Cove's 48 KB L1d and 1.25 MB **private** L2 are both much larger than Zen 3's 32 KB / 512 KB. Size
classes in the benches that straddle a cache boundary on bench #1 do not straddle the same boundary here,
which is one of the two reasons a ratio can legitimately differ between the machines.

### ISA feature probe (`tools/platform-probe/cpuid_probe.c`)

```
SSE2=1 SSE3=1 SSSE3=1 SSE41=1 SSE42=1 POPCNT=1 AES=1 PCLMUL=1
AVX=1 AVX2=1 FMA=1 F16C=1 BMI1=1 BMI2=1 LZCNT=1 MOVBE=1 ADX=1 RDSEED=1 SHA=1
AVX512F=1 AVX512DQ=1 AVX512CD=1 AVX512BW=1 AVX512VL=1
AVX512VBMI=1 AVX512VBMI2=1 AVX512VNNI=1 AVX512BITALG=1 AVX512VPOPCNTDQ=1 AVX512IFMA=1
GFNI=1 VAES=1 VPCLMULQDQ=1
ERMS=1 FSRM=1
AVX512FP16=0 AVX512BF16=0 AVX_VNNI=0 HYBRID=0
```

### Delta vs bench #1 (Zen 3 / 5950X) — the whole reason this bench exists

| Feature | 5950X | i9-11900H | consequence for this repo |
|---|---|---|---|
| AVX-512 F/BW/VL/DQ/CD | **absent** | **present** | 512-bit kernels are measurable here for the first time. `PLATFORM.md` explicitly parked them as "unvalidated-on-bench". |
| AVX512VBMI / VBMI2 | absent | **present** | `vpermb` = arbitrary byte permute across 64 B; `vpcompressb`/`vpexpandb` = branchless pack/unpack. Directly relevant to base64, hex, UTF-8 and the Crypt* family. |
| GFNI | **absent** | **present** | `vgf2p8affineqb` does an arbitrary 8×8 bit-matrix per byte in one uop — ASCII case-fold, bit-reverse and parity collapse to a single instruction. Relevant to the whole `_stricmp`/`Upcase`/`_strlwr` family. |
| AVX512VPOPCNTDQ / BITALG | absent | **present** | per-element popcount; relevant to the RTL_BITMAP run-finders. |
| VAES / VPCLMULQDQ | present | present | `RtlCrc64` (076) and `crc32` keep their folding path. |
| ERMS / FSRM | — | **present** | Intel's fast `rep movsb`/`stosb` is a real competitor at mid sizes that Zen 3 tuning never had to beat. |
| L1d / L2 | 32 KB / 512 KB | **48 KB / 1280 KB** | shifts where a size class falls out of cache. |
| Cores | 16C/32T Zen 3 | 8C/16T Willow Cove | benches are single-core and pinned, so this affects only wall-clock of the sweep. |

### AVX-512 frequency behaviour on this part — do not assume the Skylake-X penalty

Tiger Lake is **not** Skylake-SP. 256-bit and "light" 512-bit code run at the normal turbo licence here;
only heavy 512-bit FP/FMA sequences pull a licence-based frequency reduction, and the integer/permute/
compare instructions this project actually uses are light. A 512-bit variant therefore has to be measured,
not assumed to lose — but it also must not be adopted on theory. The gate is unchanged: it lands only if
it beats the live system function on **every** size class, measured here.

## Security / boot configuration

| Field | Value | Effect |
|---|---|---|
| Session elevation | **Non-elevated** | Cannot write `C:\Windows\System32`. Same as bench #1. |
| Secure Boot / WRP / TrustedInstaller | enforced | System32 binaries are catalog-signed and owned by TrustedInstaller. |

This bench reaches the same conclusion `tools/revalidate.ps1` already documents: **the unit of work is
"proven correct and faster against the live export", and the strongest honest form of "Windows ran our
code" is the per-process hot patch in `live-substitution/`,** not an on-disk replacement of a signed
system binary. See `docs/DEPLOYMENT-REALITY.md`.

## Toolchain

| Tool | Version / path |
|---|---|
| MASM | `ml64` **14.50.35723** — `…\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.50.35717\bin\HostX64\x64\ml64.exe` |
| C/C++ | `cl` 19.50.35723, `link` 14.50.35723 |
| Disassembler | `dumpbin` 14.50.35723 |
| Python | 3.10.11 (`py`) — drives `tools/abi-audit.py` |

Same 14.50 toolset as bench #1 (which had 14.50.35728), but installed as **Community**, not
**BuildTools**. Every `changes/*/build.bat` hardcodes the BuildTools path and redirects the `call` to
nul, so on this machine that line fails *silently* and the build then dies on "ml64 is not recognized".

Fixed without touching a single one of the 349 batch files: `tools/vsenv.ps1` locates whatever VS edition
is installed (vswhere first, then the known layouts) and imports its x64 environment into the current
session; a `build.bat` launched from that session inherits `PATH`/`INCLUDE`/`LIB` and its own dead `call`
becomes a no-op. `tools/revalidate-here.ps1` is the same trick wrapped around the sweep driver. Keeping
the batch files byte-identical across machines is what makes their results comparable.

### Bench hygiene notes

- Unlike bench #1, this machine has **no known bad RAM** — timings here are less noisy at the source.
  The minimum-of-N-trials estimator on a pinned core is kept anyway.
- This is a **laptop**. Thermal and power state move results far more than on a desktop. Sustained
  all-core work before a bench run will depress the numbers. Benches are single-threaded and pinned, and
  any measurement that decides a land/park is re-run on an idle, cool machine before it is recorded.
- `cmd.exe` here also has `NoDefaultCurrentDirectoryInExePath` set — a `build.bat` must be invoked by
  full path, exactly as on bench #1.

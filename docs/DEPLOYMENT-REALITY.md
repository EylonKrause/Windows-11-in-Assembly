# Deployment reality — what "replace the Windows function" can actually mean

This project's unit of work is: *a hand-written assembly routine that is bit-exact against the live
Windows export and faster than it on every size class.* That part is settled and measured, 288 times over.

The separate question — **how does Windows come to run it?** — has one wrong answer that keeps getting
proposed and several right ones. This file exists so the answer does not have to be re-derived on every
new machine.

## The wrong answer: overwrite `C:\Windows\System32\*.dll`

It does not work, and the failure is not subtle.

| Barrier | What happens |
|---|---|
| **Authenticode + catalog signing** | Every System32 binary is hashed into a signed catalog (`C:\Windows\System32\CatRoot`). Changing one byte invalidates the hash. |
| **Windows Resource Protection / TrustedInstaller** | The ACL owner is `NT SERVICE\TrustedInstaller`. Even an elevated Administrator cannot write these files without first seizing ownership — and WRP restores them from the component store afterwards. |
| **Windows Update / SFC / DISM** | Servicing reverts in-place edits at the next cumulative update. The change is not merely dangerous, it is *temporary*. |
| **Boot-critical path** | `ntdll.dll` is mapped into every process including `smss.exe`. A defect in a replacement does not produce a failed unit test, it produces an unbootable machine. `ucrtbase.dll` is barely better. |
| **Secure Boot / KMCS** | Anything kernel-side additionally requires test-signing, which requires Secure Boot off. |

There is no ordering of these steps that ends with a stable, updated Windows running modified signed
binaries. It is not a permissions problem to be solved with more privilege.

## The right answers, in increasing order of reach

### 1. Per-process hot patch — **implemented and proven** (`live-substitution/`)

Resolve the real export, `VirtualProtect` its first page, write a 14-byte
`jmp qword ptr [rip+0]; <abs64>` over the prologue, `FlushInstructionCache`. Because the image mapping is
**copy-on-write**, this modifies only the calling process's private copy. Every caller inside that process
— including Windows' own in-process code — then executes our assembly.

This is the mechanism Detours uses. It is supported, reversible, and it is a genuine answer to "did
Windows run our code": the harness proves the patched prologue starts `FF 25`, that the counter
incremented by exactly the number of calls made through the **real** function pointer, that all results
matched, and that the original bytes were restored cleanly.

Scope limit, stated plainly: **one process, until it exits.**

### 2. Link-time replacement in your own binaries — the honest "permanent" option

For code you build yourself, the CRT routine is chosen at link time. Publishing the landed routines as a
static library that is linked ahead of the CRT gives a *permanent, unconditional, zero-risk* replacement
for every program built against it, with no patching, no signature problem and nothing for Windows Update
to revert. This is the only form of "replaced forever" that is actually true.

### 3. Process-scoped injection at launch — `IFEO` + a Detours-style shim

`Image File Execution Options` can attach a helper DLL to a *named* executable, which installs the same
hot patches at startup. Reach: every run of that one program. Still reversible (delete the registry key),
still no signed binary modified. Appropriate for a specific hot application; **not** appropriate for
`svchost.exe` or anything in the boot path.

### 4. System-wide injection — possible, and deliberately not done here

`AppInit_DLLs` is deprecated and disabled under Secure Boot; a kernel shim requires test-signing. Both
trade the entire machine's stability and its update path for a benchmark number. The project does not do
this, and the reason is written down rather than left implicit.

## Consequence for how results are stated

A change marked **LANDED** in this repository means **proven on this PC** — correct against the live
export and faster than it — and, for the subset covered by `live-substitution/`, **executed by Windows in
place of the shipped function**. It does not mean the file on disk changed. Any sentence in this repo that
implies otherwise is a bug in the sentence.

# tools/on-update.ps1
# ---------------------------------------------------------------------------------------------
# The action a scheduled task runs. Cheap when nothing changed, thorough when something did.
#
#   1. Compare the watched System32 DLLs against the recorded baseline (hash, not just version).
#   2. If none of them changed, write one line to the history and stop. This is the normal case:
#      most updates replace dozens of DLLs without touching ntdll / ucrtbase / shlwapi /
#      kernelbase / crypt32 / msvcrt / iphlpapi / rpcrt4 / combase, and re-running a multi-hour
#      sweep for those would be waste. (The list lives in revalidate.ps1 -- $WatchedDlls.)
#   3. If any DID change, re-prove every change against the new binaries and write a report. The
#      correctness harnesses compare against the LIVE export, so this is a real answer, not a
#      heuristic.
#   4. On failure, leave a loud marker file the next interactive session will see.
#   5. Roll the baseline forward only when everything still passes -- so a broken update stays
#      flagged on every subsequent run instead of being silently accepted.
#
# It deliberately does NOT modify anything under System32. See the header of revalidate.ps1 for
# why that is not automatable, and what "re-apply" means here instead.
# ---------------------------------------------------------------------------------------------
[CmdletBinding()]
param(
    [string] $Repo  = '',
    [switch] $Force          # run the full sweep even if no watched DLL changed
)

$ErrorActionPreference = 'Continue'
$here = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
if (-not $Repo) { $Repo = Split-Path -Parent $here }

$outDir  = Join-Path $Repo 'revalidation'
$history = Join-Path $outDir 'history.log'
$alert   = Join-Path $outDir 'NEEDS-ATTENTION.md'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

function Note([string]$m) {
    $line = "{0}  {1}" -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $m
    Add-Content -Encoding utf8 -Path $history -Value $line
    Write-Host $line
}

$reval = Join-Path $here 'revalidate.ps1'
if (-not (Test-Path $reval)) { Note "FATAL: revalidate.ps1 not found next to on-update.ps1"; exit 2 }

# ---- step 1/2: has anything we depend on actually changed? -------------------------------------
& powershell -NoProfile -ExecutionPolicy Bypass -File $reval -Repo $Repo -CheckOnly | Out-Null
$changed = ($LASTEXITCODE -eq 1)

if (-not $changed -and -not $Force) {
    Note "check: no watched DLL changed - nothing to re-prove"
    exit 0
}

if ($changed) { Note "check: a watched DLL CHANGED - re-proving every change against the new binaries" }
else          { Note "check: forced full re-validation" }

# ---- step 3: the real work ----------------------------------------------------------------------
& powershell -NoProfile -ExecutionPolicy Bypass -File $reval -Repo $Repo
$rc = $LASTEXITCODE

$report = Get-ChildItem $outDir -Filter 'report_*.md' -EA SilentlyContinue |
          Sort-Object LastWriteTime -Descending | Select-Object -First 1

if ($rc -eq 0) {
    Note "re-validation PASSED (report: $($report.Name))"
    Remove-Item $alert -EA SilentlyContinue
    # step 5: only now is it safe to accept the new binaries as the reference point
    & powershell -NoProfile -ExecutionPolicy Bypass -File $reval -Repo $Repo -Baseline | Out-Null
    Note "baseline rolled forward to the current binaries"
    exit 0
}

# ---- step 4: loud, and sticky ------------------------------------------------------------------
Note "re-validation FAILED (rc=$rc) - see $($report.Name)"
@(
  "# Re-validation FAILED after a Windows update"
  ""
  "Generated $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') by tools/on-update.ps1."
  ""
  "A watched System32 DLL changed and at least one change no longer matches the shipped function."
  "That means a serviced Windows routine altered its behaviour: our assembly is now WRONG for it,"
  "not merely slower."
  ""
  "* report: ``$($report.FullName)``"
  "* history: ``$history``"
  ""
  "The baseline was deliberately NOT rolled forward, so every later run keeps reporting this until"
  "it is fixed. Re-derive the contract for the failing change the same way it was derived"
  "originally - probe the live export, then fix impl.asm - and re-run:"
  ""
  '```'
  "powershell -NoProfile -ExecutionPolicy Bypass -File tools\revalidate.ps1 -Only <change-dir>"
  '```'
) -join "`r`n" | Set-Content -Encoding utf8 $alert
exit 1

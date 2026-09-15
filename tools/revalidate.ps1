# tools/revalidate.ps1
# ---------------------------------------------------------------------------------------------
# Re-prove every landed change against the CURRENT System32 binaries.
#
# WHY THIS EXISTS. Every change in this repository is bit-exact against a specific build of
# ntdll / ucrtbase / shlwapi / kernelbase / crypt32 on this machine. A Windows update can replace
# any of those binaries, and a serviced function is free to change behaviour: a new edge case, a
# different error code, a widened character table. Nothing warns you. The correctness harnesses
# already compare against the LIVE export via GetProcAddress, so re-running them is a complete
# answer to "does our assembly still match what Windows now ships".
#
# WHAT "RE-APPLY" CAN AND CANNOT MEAN HERE -- read this before expecting more than it does:
#   * This repository does NOT install its assembly into Windows. It cannot: System32 binaries are
#     catalog-signed, Windows Resource Protection + TrustedInstaller own them, and Windows Update
#     reverts in-place edits. A modified system DLL would fail signature validation.
#   * What it does instead is live substitution: a per-process hot patch of the process's own
#     copy-on-write copy, proven and then reverted. That is the strongest honest form of "Windows
#     ran our code", and it is re-provable on demand -- which is what this script does.
#   * So after an update the meaningful action is RE-VALIDATE, and fix anything the update broke.
#     That is what gets automated. Permanently patching System32 is not automated here, and should
#     not be: it needs Secure Boot off or test-signing on, and it would be undone by the next
#     servicing pass anyway.
#
# USAGE
#   .\revalidate.ps1                  full sweep: every change + every live-substitution harness
#   .\revalidate.ps1 -Baseline        record the current DLL versions/hashes as the baseline
#   .\revalidate.ps1 -CheckOnly       just compare DLLs against the baseline and report
#   .\revalidate.ps1 -Only 193,194    restrict to matching change directories
#   .\revalidate.ps1 -SkipLive        skip the live-substitution harnesses
#
# EXIT CODES
#   0  everything still correct (speed regressions are reported but do NOT fail the run -- see below)
#   1  at least one CORRECTNESS or BUILD failure: a shipped function no longer matches our model
#   2  environment problem (no Visual Studio, bad paths)
#
# Speed is deliberately NOT an exit-code condition here. A run triggered right after servicing
# competes with Windows Update's own post-install work, so timings are unreliable exactly when
# this script is most likely to fire. Regressions are recorded in the report and should be
# re-measured on an idle machine before being believed.
# ---------------------------------------------------------------------------------------------
[CmdletBinding()]
param(
    [string]  $Repo       = '',
    [string]  $OutDir     = '',
    [int]     $TimeoutSec = 1200,
    [string[]]$Only       = @(),
    [switch]  $Baseline,
    [switch]  $CheckOnly,
    [switch]  $SkipLive
)

$ErrorActionPreference = 'Continue'

# $PSScriptRoot is not populated inside a param() default under Windows PowerShell 5.1, so the
# paths are resolved here instead of in the signature.
$here = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
if (-not $Repo)   { $Repo   = Split-Path -Parent $here }
if (-not $OutDir) { $OutDir = Join-Path $Repo 'revalidation' }
$stamp = Get-Date -Format 'yyyy-MM-dd_HHmmss'
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$logs = Join-Path $OutDir "logs_$stamp"
New-Item -ItemType Directory -Force -Path $logs | Out-Null

# The DLLs this repository reimplements functions from. If one of these changes, our contracts
# are the thing at risk.
# iphlpapi.dll joined the list with change 202 (ConvertGuidToStringW). Keep this in step with
# the DLLs any change actually validates against -- a DLL missing here is a DLL whose
# servicing would not trigger a sweep.
$WatchedDlls = 'ntdll.dll','ucrtbase.dll','shlwapi.dll','kernelbase.dll','crypt32.dll',
               'msvcrt.dll','iphlpapi.dll'
$baselineFile = Join-Path $OutDir 'dll-baseline.json'

function Get-DllState {
    $rows = foreach ($d in $WatchedDlls) {
        $p = Join-Path $env:SystemRoot "System32\$d"
        if (-not (Test-Path $p)) { continue }
        $fi = Get-Item $p
        [pscustomobject]@{
            dll     = $d
            version = $fi.VersionInfo.FileVersion
            size    = $fi.Length
            written = $fi.LastWriteTimeUtc.ToString('o')
            sha256  = (Get-FileHash -Path $p -Algorithm SHA256).Hash
        }
    }
    [pscustomobject]@{
        captured = (Get-Date).ToString('o')
        osbuild  = "$([System.Environment]::OSVersion.Version).$((Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion').UBR)"
        dlls     = @($rows)
    }
}

$now = Get-DllState

if ($Baseline) {
    $now | ConvertTo-Json -Depth 5 | Set-Content -Encoding utf8 $baselineFile
    Write-Host "baseline recorded -> $baselineFile"
    Write-Host "  OS build $($now.osbuild)"
    foreach ($r in $now.dlls) { Write-Host ("  {0,-16} {1,-30} {2}" -f $r.dll, $r.version, $r.sha256.Substring(0,16)) }
    exit 0
}

# ---- compare against the baseline -------------------------------------------------------------
$changed = @()
if (Test-Path $baselineFile) {
    $base = Get-Content $baselineFile -Raw | ConvertFrom-Json
    Write-Host "baseline:  OS build $($base.osbuild)  captured $($base.captured)"
    Write-Host "current :  OS build $($now.osbuild)"
    foreach ($r in $now.dlls) {
        $b = $base.dlls | Where-Object { $_.dll -eq $r.dll }
        if (-not $b) { $changed += "$($r.dll): NEW"; continue }
        if ($b.sha256 -ne $r.sha256) {
            $changed += "$($r.dll): $($b.version) -> $($r.version)"
        }
    }
    if ($changed.Count) {
        Write-Host "`nCHANGED SINCE BASELINE:" -ForegroundColor Yellow
        $changed | ForEach-Object { Write-Host "  $_" -ForegroundColor Yellow }
    } else {
        Write-Host "`nno watched DLL changed since the baseline."
    }
    if ($base.osbuild -ne $now.osbuild) {
        Write-Host "  OS build moved $($base.osbuild) -> $($now.osbuild)" -ForegroundColor Yellow
    }
} else {
    Write-Host "no baseline recorded yet (run with -Baseline to create one)."
}

if ($CheckOnly) { exit ($(if ($changed.Count) { 1 } else { 0 })) }

# ---- Visual Studio environment, imported once so each build.bat short-circuits ------------------
$vcvars = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat'
if (-not $env:VSCMD_VER) {
    if (-not (Test-Path $vcvars)) { Write-Host "FATAL: vcvarsall not found at $vcvars"; exit 2 }
    & cmd /c "`"$vcvars`" x64 -vcvars_ver=14.50 >nul 2>&1 && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($matches[1])" -Value $matches[2] -EA SilentlyContinue }
    }
}
if (-not $env:VSCMD_VER) { Write-Host 'FATAL: could not initialize the VS environment'; exit 2 }

$tsv = Join-Path $OutDir "results_$stamp.tsv"
"dir`tstatus`tcorrectness`tgeomean`tworst_ratio`tworst_size`tregressed`tsecs`texit" | Set-Content -Encoding utf8 $tsv

$dirs = Get-ChildItem (Join-Path $Repo 'changes') -Directory | Sort-Object Name
if ($Only.Count) {
    $dirs = $dirs | Where-Object { $n = $_.Name; ($Only | Where-Object { $n -like "*$_*" }).Count -gt 0 }
}

$fails = @(); $regressions = @(); $done = 0

foreach ($d in $dirs) {
    $name = $d.Name
    $bat  = Join-Path $d.FullName 'build.bat'
    if (-not (Test-Path $bat)) {
        "$name`tNO_BUILD`t-`t-`t-`t-`t-`t0`t-" | Add-Content -Encoding utf8 $tsv
        continue
    }
    $log = Join-Path $logs "$name.log"
    $so  = Join-Path $logs "$name.out.tmp"
    $se  = Join-Path $logs "$name.err.tmp"
    $sw  = [Diagnostics.Stopwatch]::StartNew()

    $p = Start-Process -FilePath 'cmd.exe' -ArgumentList '/c', "`"$bat`"" `
            -WorkingDirectory $d.FullName -NoNewWindow -PassThru `
            -RedirectStandardOutput $so -RedirectStandardError $se
    $ok = $p.WaitForExit($TimeoutSec * 1000)
    if (-not $ok) {
        try { Stop-Process -Id $p.Id -Force -EA SilentlyContinue } catch {}
        Start-Sleep -Milliseconds 300
        Get-Process correctness, bench -EA SilentlyContinue |
            Where-Object { $_.Path -like "$($d.FullName)*" } | Stop-Process -Force -EA SilentlyContinue
        $exit = 'TIMEOUT'
    } else { $exit = $p.ExitCode }
    $sw.Stop()

    $txt = ''
    foreach ($f in @($so, $se)) { if (Test-Path $f) { $txt += (Get-Content $f -Raw -EA SilentlyContinue) } }
    Set-Content -Path $log -Value $txt -Encoding utf8
    Remove-Item $so, $se -EA SilentlyContinue

    $corr = 'n/a'
    if     ($txt -match 'CORRECTNESS[^\r\n]*FAILED')      { $corr = 'FAIL' }
    elseif ($txt -match 'CORRECTNESS[^\r\n]*:?\s*PASS')   { $corr = 'PASS' }
    elseif ($txt -match '(?m)^\s*PASS\b')                 { $corr = 'PASS' }
    elseif ($txt -match 'MISMATCH|mismatch')              { $corr = 'FAIL' }

    $geo = '-'
    if ($txt -match 'geomean[^)]*\)\s*:\s*([\d.]+)x') { $geo = $matches[1] }

    $worst = $null; $worstSize = '-'; $reg = @()
    foreach ($line in ($txt -split "`r?`n")) {
        if ($line -match '^\s*(\S+)\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)x\s+([\d.]+)\s+(BETTER|WORSE|~tie)\s*$') {
            $sz = $matches[1]; $r = [double]$matches[4]; $v = $matches[6]
            if ($null -eq $worst -or $r -lt $worst) { $worst = $r; $worstSize = $sz }
            if ($v -eq 'WORSE') { $reg += "$sz=$($matches[4])x" }
        }
    }
    $worstStr = if ($null -ne $worst) { '{0:N3}' -f $worst } else { '-' }
    $regStr   = if ($reg.Count) { $reg -join ',' } else { '-' }

    $status =
        if     ($exit -eq 'TIMEOUT')                                     { 'TIMEOUT' }
        elseif ($corr -eq 'FAIL')                                        { 'CORRECTNESS_FAIL' }
        elseif ($txt -match 'BUILD/RUN ERROR|BUILD ERROR|error [A-Z]+\d+'){ 'BUILD_FAIL' }
        elseif ($geo -eq '-')                                            { 'NO_BENCH' }
        elseif ($reg.Count)                                              { 'REGRESSED' }
        else                                                             { 'LANDS' }

    if ($status -in 'CORRECTNESS_FAIL','BUILD_FAIL','TIMEOUT') { $fails += "$name ($status)" }
    elseif ($status -eq 'REGRESSED') { $regressions += "$name $regStr" }

    "$name`t$status`t$corr`t$geo`t$worstStr`t$worstSize`t$regStr`t$([int]$sw.Elapsed.TotalSeconds)`t$exit" |
        Add-Content -Encoding utf8 $tsv
    $done++
    Write-Host ("[{0,4}/{1}] {2,-26} {3,-18} geo={4,-7} worst={5}@{6}" -f $done, $dirs.Count, $name, $status, $geo, $worstStr, $worstSize)
}

# ---- gate 3: the static ABI audit --------------------------------------------------------------
# Cheap (a regex pass over every .asm) and it catches the one class of bug the correctness and speed
# gates are structurally blind to: a callee-saved vector register used as scratch. Sixteen changes
# carried that for months while passing both other gates. Unlike a speed regression, this DOES fail
# the run -- it is a correctness property, not a measurement.
$abiFails = @()
$abiScript = Join-Path $PSScriptRoot 'abi-audit.py'
if (Test-Path $abiScript) {
    $abiOut = & py $abiScript $Repo 2>&1
    if ($LASTEXITCODE -ne 0) {
        $abiFails = @($abiOut | Where-Object { $_ -match '\s+\S+\.asm\s' } | ForEach-Object { $_.Trim() })
        if (-not $abiFails.Count) { $abiFails = @('abi-audit.py reported a violation') }
        Write-Host ("[ABI ] AUDIT FAIL -- {0} file(s)" -f $abiFails.Count) -ForegroundColor Red
    } else {
        Write-Host "[ABI ] audit clean (no callee-saved vector register used without a spill)"
    }
} else {
    Write-Host "[ABI ] tools/abi-audit.py not found -- gate 3 SKIPPED" -ForegroundColor Yellow
}

# ---- live-substitution harnesses ---------------------------------------------------------------
$liveFails = @()
if (-not $SkipLive) {
    $liveDir = Join-Path $Repo 'live-substitution'
    foreach ($b in (Get-ChildItem $liveDir -Filter 'build*.bat' | Sort-Object Name)) {
        $lg = Join-Path $logs "live_$($b.BaseName).log"
        $so = "$lg.out"; $se = "$lg.err"
        $p = Start-Process -FilePath 'cmd.exe' -ArgumentList '/c', "`"$($b.FullName)`"" `
                -WorkingDirectory $liveDir -NoNewWindow -PassThru `
                -RedirectStandardOutput $so -RedirectStandardError $se
        $ok = $p.WaitForExit($TimeoutSec * 1000)
        if (-not $ok) { try { Stop-Process -Id $p.Id -Force -EA SilentlyContinue } catch {}; $ex = 'TIMEOUT' }
        else { $ex = $p.ExitCode }
        $t = ''
        foreach ($f in @($so,$se)) { if (Test-Path $f) { $t += (Get-Content $f -Raw -EA SilentlyContinue) } }
        Set-Content -Path $lg -Value $t -Encoding utf8
        Remove-Item $so,$se -EA SilentlyContinue
        $verdict = if ($t -match 'LIVE SUBSTITUTION: PASS') { 'PASS' }
                   elseif ($t -match 'FAILURE\(S\)|UNPROVEN') { 'FAIL' }
                   elseif ($ex -eq 0) { 'PASS' } else { 'FAIL' }
        if ($verdict -eq 'FAIL') { $liveFails += $b.BaseName }
        Write-Host ("[live] {0,-28} {1}" -f $b.BaseName, $verdict)
    }
}

# ---- report -------------------------------------------------------------------------------------
$report = Join-Path $OutDir "report_$stamp.md"
$lines = @()
$lines += "# Re-validation $stamp"
$lines += ""
$lines += "OS build **$($now.osbuild)**"
$lines += ""
$lines += "| DLL | version | sha256 (first 16) |"
$lines += "|---|---|---|"
foreach ($r in $now.dlls) { $lines += "| $($r.dll) | $($r.version) | $($r.sha256.Substring(0,16)) |" }
$lines += ""
if ($changed.Count) {
    $lines += "## Watched DLLs that changed since the baseline"
    $lines += ""
    foreach ($c in $changed) { $lines += "* $c" }
    $lines += ""
}
$lines += "## Result"
$lines += ""
$lines += "* changes run: **$done**"
$lines += "* correctness / build failures: **$($fails.Count)**"
$lines += "* speed regressions: **$($regressions.Count)** (informational -- see the header note; a run"
$lines += "  triggered by servicing competes with Windows Update's own post-install work)"
if (-not $SkipLive) { $lines += "* live-substitution harness failures: **$($liveFails.Count)**" }
$lines += "* ABI audit (callee-saved vector registers): **$(if ($abiFails.Count) { "$($abiFails.Count) file(s) FAIL" } else { 'clean' })**"
$lines += ""
if ($fails.Count) {
    $lines += "### FAILURES -- a shipped function no longer matches our model"
    $lines += ""
    foreach ($f in $fails) { $lines += "* $f" }
    $lines += ""
}
if ($regressions.Count) {
    $lines += "### Speed regressions (re-measure on an idle machine before believing these)"
    $lines += ""
    foreach ($r in $regressions) { $lines += "* $r" }
    $lines += ""
}
if ($abiFails.Count) {
    $lines += "### ABI violations -- a callee-saved vector register used without a spill"
    $lines += ""
    $lines += "Win64 preserves the LOW 128 BITS of xmm6-xmm15. See tools/README.md."
    $lines += ""
    foreach ($f in $abiFails) { $lines += "* $f" }
    $lines += ""
}
if ($liveFails.Count) {
    $lines += "### Live-substitution failures"
    $lines += ""
    foreach ($f in $liveFails) { $lines += "* $f" }
    $lines += ""
}
$lines += "Per-change logs: ``$logs``"
$lines += "Machine-readable: ``$tsv``"
$lines -join "`r`n" | Set-Content -Encoding utf8 $report

Write-Host ""
Write-Host "report -> $report"
Write-Host ("changes=$done  failures=$($fails.Count)  regressions=$($regressions.Count)" +
            "  abiViolations=$($abiFails.Count)" +
            $(if (-not $SkipLive) { "  liveFailures=$($liveFails.Count)" }))

if ($fails.Count -or $liveFails.Count -or $abiFails.Count) { exit 1 } else { exit 0 }

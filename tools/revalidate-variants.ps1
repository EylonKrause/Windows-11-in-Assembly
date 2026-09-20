# tools/revalidate-variants.ps1
# ---------------------------------------------------------------------------------------------
# Sweep the MICROARCHITECTURE VARIANTS, which the main sweep does not touch.
#
# Why this exists. tools/revalidate.ps1 walks changes/* and runs each directory's `build.bat`. That
# is the implementation of record and it is the right thing for it to run. But a change can also
# carry variants beside it:
#
#     changes/047-strlwr/impl_2ndpc.asm   + build_2ndpc.bat   + RESULTS-2ndpc.md
#     changes/023-rtlnumberofsetbits/impl_tgl.asm + build_tgl.bat + RESULTS-tgl.md
#
# and `build_2ndpc.bat` / `build_tgl.bat` are never invoked by the main sweep. So sixteen
# implementations in this repository -- seven Zen 4 variants and nine Tiger Lake ones -- have been
# outside every automated gate since the day they were written. They are the files most likely to
# rot, too: they exist precisely because the shipped function behaved differently on one machine,
# so they are the ones a servicing update is most likely to invalidate.
#
# This runs them. It deliberately does NOT modify revalidate.ps1: the two sweeps answer different
# questions and conflating them would make a variant failure look like a failure of the change.
#
# What a result means here
#   * A CORRECTNESS failure is as serious as in the main sweep and fails the run. The variant is
#     built against the change's UNMODIFIED correctness.c, which resolves the live export through
#     GetProcAddress, so a failure means the variant no longer matches what Windows ships.
#   * A SPEED regression does NOT fail the run, and for a variant that is not even news: several are
#     documented PARKED because they improve a class without clearing it. Their RESULTS-<suffix>.md
#     records which, and this script reads that verdict rather than assuming.
#
# USAGE
#   .\tools\revalidate-variants.ps1                 every variant of every change
#   .\tools\revalidate-variants.ps1 -Suffix tgl     only the Tiger Lake ones
#   .\tools\revalidate-variants.ps1 -Only 023,124
#
# EXIT CODES
#   0  every variant still correct
#   1  at least one correctness or build failure
#   2  environment problem
# ---------------------------------------------------------------------------------------------
[CmdletBinding()]
param(
    [string]   $Repo       = '',
    [string[]] $Suffix     = @(),
    [string[]] $Only       = @(),
    [int]      $TimeoutSec = 900
)

$ErrorActionPreference = 'Continue'
$here = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
if (-not $Repo) { $Repo = Split-Path -Parent $here }

# Import the toolchain portably -- every build_*.bat carries the same hardcoded BuildTools path as
# its parent build.bat, and on a machine with a different VS edition that call fails silently.
. (Join-Path $here 'vsenv.ps1') -Quiet
if (-not $env:VSCMD_VER) { Write-Host 'FATAL: could not initialize the VS environment'; exit 2 }

$stamp  = Get-Date -Format 'yyyy-MM-dd_HHmmss'
$outDir = Join-Path $Repo 'revalidation'
$logs   = Join-Path $outDir "variants_$stamp"
New-Item -ItemType Directory -Force -Path $logs | Out-Null

# Find every variant build script. The pattern deliberately excludes plain `build.bat` -- that is
# the main sweep's job, and running it here would double every measurement.
$builds = Get-ChildItem (Join-Path $Repo 'changes') -Recurse -Filter 'build_*.bat' |
          Sort-Object FullName
if ($Suffix.Count) {
    $builds = $builds | Where-Object {
        $n = $_.BaseName -replace '^build_',''
        $Suffix -contains $n
    }
}
if ($Only.Count) {
    $builds = $builds | Where-Object { $p = $_.Directory.Name; ($Only | Where-Object { $p -like "*$_*" }).Count -gt 0 }
}

if (-not $builds) { Write-Host 'no variant build scripts found.'; exit 0 }

Write-Host ("sweeping {0} variant build(s)" -f $builds.Count)
Write-Host ''

# a variant's own RESULTS-<suffix>.md records whether it lands or is parked. a parked variant is one
# already documented as losing a size class -- reporting that every run is noise, and noise is what
# buries a real finding.
function Get-VariantVerdict {
    param([string] $dir, [string] $suffix)
    $r = Join-Path $dir "RESULTS-$suffix.md"
    if (-not (Test-Path $r)) { return 'UNKNOWN' }
    $head = (Get-Content $r -TotalCount 4 -EA SilentlyContinue) -join ' '
    # The verdict may carry a qualifier, and requiring the bare word made this return unknown for
    # two variants whose heading reads `**LANDS (variant)**` -- so the sweep could not tell whether
    # a regression in them was expected or news. A verdict that cannot be read is not a neutral
    # 'unknown': it silently moves the change out of both the expected and the actionable list.
    if ($head -match '\*\*PARKED')                     { return 'PARKED' }
    if ($head -match '\*\*UNPROVEN')                   { return 'UNPROVEN' }
    if ($head -match '\*\*LANDS|\*\*LANDED')        { return 'LANDED' }
    return 'UNKNOWN'
}

$tsv = Join-Path $outDir "variants_$stamp.tsv"
"change`tvariant`tstatus`tcorrectness`tgeomean`tworst`tworst_size`tdocumented`tsecs" |
    Set-Content -Encoding utf8 $tsv

$fails = @(); $regressions = @(); $expected = @(); $done = 0

foreach ($b in $builds) {
    $change  = $b.Directory.Name
    $vsuffix = $b.BaseName -replace '^build_',''   # NOT $suffix: PowerShell variable
                                                   # names are case-insensitive and
                                                   # that is the -Suffix parameter
    $log     = Join-Path $logs ("{0}__{1}.log" -f $change, $vsuffix)
    $so = "$log.out"; $se = "$log.err"
    $sw = [Diagnostics.Stopwatch]::StartNew()

    $p = Start-Process -FilePath 'cmd.exe' -ArgumentList '/c', "`"$($b.FullName)`"" `
            -WorkingDirectory $b.Directory.FullName -NoNewWindow -PassThru `
            -RedirectStandardOutput $so -RedirectStandardError $se
    $ok = $p.WaitForExit($TimeoutSec * 1000)
    if (-not $ok) {
        try { Stop-Process -Id $p.Id -Force -EA SilentlyContinue } catch {}
        $exit = 'TIMEOUT'
    } else { $exit = $p.ExitCode }
    $sw.Stop()

    $txt = ''
    foreach ($f in @($so, $se)) { if (Test-Path $f) { $txt += (Get-Content $f -Raw -EA SilentlyContinue) } }
    [System.IO.File]::WriteAllText($log, $txt, [System.Text.UTF8Encoding]::new($false))
    Remove-Item $so, $se -EA SilentlyContinue

    # Same classifier as the fixed one in revalidate.ps1. Rule 2 requires a NON-ZERO count, and the
    # lookbehind is what stops "10 mismatches" being read as a zero -- a bare 'mismatch' word match
    # fires on "0 mismatches", which is the phrase a PASSING harness prints.
    $corr = 'n/a'
    if     ($txt -match 'CORRECTNESS[^\r\n]*FAILED')          { $corr = 'FAIL' }
    elseif ($txt -match '(?<![\d.])[1-9]\d*\s+mismatch')      { $corr = 'FAIL' }
    elseif ($txt -match 'CORRECTNESS[^\r\n]*:?\s*PASS')       { $corr = 'PASS' }
    elseif ($txt -match '(?m)^\s*PASS\b')                     { $corr = 'PASS' }
    elseif ($txt -match '(?<![\d.])0\s+mismatch|bit-exact')   { $corr = 'PASS' }
    elseif ($txt -match 'MISMATCH|mismatch')                  { $corr = 'FAIL' }

    $geo = '-'
    if ($txt -match 'geomean[^)]*\)\s*:\s*([\d.]+)x') { $geo = $matches[1] }

    # The row label is not one token, and assuming it was made this gate inert for 109 Of 288
    # CHANGES. `(\S+)` matches a single word, so it reads "4096" in `4096  12.3  20.1  1.63x ...`
    # and nothing at all in `bad32 64 ...`, `Copy 64 Kbit, target 8 (byte-aligned) ...` or
    # `m:a+4 8191 ...`. Those benches parsed to ZERO rows, so $worst stayed null, $reg stayed empty,
    # and every one of them was reported LANDS on the strength of correctness and a geomean alone --
    # including 260-rtlcopybitmap, which has EIGHT rows below parity on bench #3. A lazy label
    # backtracks into the right split at no cost: for "bad32 64" it tries label="bad32", fails to
    # find a ratio, and retries with label="bad32 64".
    $worst = $null; $worstSize = '-'; $reg = @()
    foreach ($line in ($txt -split "`r?`n")) {
        if ($line -match '^\s*(.*?)\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)x\s+([\d.]+)\s+(BETTER|WORSE|~tie)\s*$') {
            $sz = $matches[1]; $r = [double]$matches[4]
            if ($null -eq $worst -or $r -lt $worst) { $worst = $r; $worstSize = $sz }
            if ($matches[6] -eq 'WORSE') { $reg += $sz }
        }
    }
    $worstStr = if ($null -ne $worst) { '{0:N3}' -f $worst } else { '-' }
    $doc = Get-VariantVerdict $b.Directory.FullName $vsuffix

    $status =
        if     ($exit -eq 'TIMEOUT')                                      { 'TIMEOUT' }
        elseif ($corr -eq 'FAIL')                                         { 'CORRECTNESS_FAIL' }
        elseif ($txt -match 'BUILD/RUN ERROR|error [A-Z]+\d+')            { 'BUILD_FAIL' }
        elseif ($geo -eq '-')                                             { 'NO_BENCH' }
        elseif ($null -eq $worst)                                         { 'BENCH_UNPARSED' }
        elseif ($reg.Count)                                               { 'REGRESSED' }
        else                                                              { 'LANDS' }

    # BENCH_UNPARSED is listed with the failures on purpose: an inert gate reads as a pass.
    if ($status -in 'CORRECTNESS_FAIL','BUILD_FAIL','TIMEOUT','BENCH_UNPARSED') {
        $fails += "$change/$vsuffix ($status)"
    } elseif ($status -eq 'REGRESSED') {
        # A variant documented PARKED is one already known to lose a class. Separate it, so the
        # actionable list contains only variants that have actually drifted.
        if ($doc -eq 'PARKED') { $expected += "$change/$vsuffix ($($reg -join ','))" }
        else                   { $regressions += "$change/$vsuffix ($($reg -join ','))" }
    }

    "$change`t$vsuffix`t$status`t$corr`t$geo`t$worstStr`t$worstSize`t$doc`t$([int]$sw.Elapsed.TotalSeconds)" |
        Add-Content -Encoding utf8 $tsv
    $done++
    $colour = switch ($status) { 'LANDS' {'Green'} 'REGRESSED' {'Yellow'} default {'Red'} }
    Write-Host ("[{0,3}/{1}] {2,-36} {3,-6} {4,-17} geo={5,-8} worst={6}@{7}  doc={8}" -f `
        $done, $builds.Count, $change, $vsuffix, $status, $geo, $worstStr, $worstSize, $doc) `
        -ForegroundColor $colour
}

Write-Host ''
Write-Host ("variants run: {0}   correctness/build failures: {1}   undocumented regressions: {2}   documented-PARKED regressions: {3}" -f `
    $done, $fails.Count, $regressions.Count, $expected.Count)

if ($fails.Count) {
    Write-Host ''
    Write-Host 'FAILURES -- a variant no longer matches the live export:' -ForegroundColor Red
    $fails | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
}
if ($regressions.Count) {
    Write-Host ''
    Write-Host 'Speed regressions in variants NOT documented as PARKED (re-measure on an idle machine):' -ForegroundColor Yellow
    $regressions | ForEach-Object { Write-Host "  $_" -ForegroundColor Yellow }
}
if ($expected.Count) {
    Write-Host ''
    Write-Host 'Regressions in variants already documented PARKED (expected):'
    $expected | ForEach-Object { Write-Host "  $_" }
}

Write-Host ''
Write-Host "report -> $tsv"
exit ($(if ($fails.Count) { 1 } else { 0 }))

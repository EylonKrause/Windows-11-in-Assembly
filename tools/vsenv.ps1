# tools/vsenv.ps1
# ---------------------------------------------------------------------------------------------
# Import the MSVC x64 build environment into the CURRENT PowerShell session, on ANY machine.
#
# WHY THIS EXISTS. Every changes/*/build.bat in this repository begins with a hardcoded
#
#     call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 ...
#
# because that is where Visual Studio lives on the bench the project started on. On a machine with
# a different VS edition (Community/Professional/Enterprise) or a different drive, that path does
# not exist. The `call` is redirected to nul, so it fails SILENTLY and the build then dies on
# "ml64 is not recognized".
#
# The fix deliberately does NOT rewrite those 349 batch files. They only need ml64/cl/link/dumpbin
# to be resolvable, and a batch file inherits the environment of whatever launched it. So: import
# the right vcvars ONCE here, then invoke build.bat as usual. Its own dead `call` is harmless and
# the inherited PATH/INCLUDE/LIB carries the build. This keeps every build.bat byte-identical
# across all three validation machines, which is what makes their RESULTS.md comparable.
#
# It also sets VSCMD_VER, which is the flag tools/revalidate.ps1 checks before trying (and
# failing) to locate BuildTools itself.
#
# USAGE
#   . .\tools\vsenv.ps1              # dot-source: imports into your session
#   . .\tools\vsenv.ps1 -Quiet
# ---------------------------------------------------------------------------------------------
[CmdletBinding()]
param(
    [string] $VcVarsVer = '14.50',
    [switch] $Quiet
)

if ($env:VSCMD_VER) {
    if (-not $Quiet) { Write-Host "vsenv: already initialized (VSCMD_VER=$env:VSCMD_VER)" }
    return
}

# Ordered by preference: whatever vswhere reports first, then the known layouts. vswhere itself
# only ever installs under the x86 Program Files, even for an x64 VS.
$candidates = @()
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (Test-Path $vswhere) {
    $candidates += & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
                              -property installationPath 2>$null
    $candidates += & $vswhere -products * -property installationPath 2>$null
}
foreach ($root in @($env:ProgramFiles, ${env:ProgramFiles(x86)})) {
    foreach ($ver in '18','2026','2022') {
        foreach ($ed in 'BuildTools','Community','Professional','Enterprise') {
            $candidates += (Join-Path $root "Microsoft Visual Studio\$ver\$ed")
        }
    }
}

$vcvars = $null
foreach ($c in ($candidates | Where-Object { $_ } | Select-Object -Unique)) {
    $p = Join-Path $c 'VC\Auxiliary\Build\vcvarsall.bat'
    if (Test-Path $p) { $vcvars = $p; break }
}
if (-not $vcvars) { throw "vsenv: no vcvarsall.bat found. Install the MSVC x64 build tools." }

# -vcvars_ver pins the toolset so every machine assembles with the same ml64 major.minor; if the
# requested toolset is not installed, fall back to the install's default rather than silently
# producing no environment at all.
$imported = 0
foreach ($args in @("x64 -vcvars_ver=$VcVarsVer", 'x64')) {
    & cmd /c "`"$vcvars`" $args >nul 2>&1 && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($matches[1])" -Value $matches[2] -EA SilentlyContinue }
    }
    if ($env:VSCMD_VER) { $imported = 1; break }
}
if (-not $imported) { throw "vsenv: '$vcvars' produced no environment." }

if (-not $Quiet) {
    Write-Host "vsenv: $vcvars"
    Write-Host "vsenv: VSCMD_VER=$env:VSCMD_VER  toolset=$env:VCToolsVersion"
    Write-Host "vsenv: ml64 -> $((Get-Command ml64 -EA SilentlyContinue).Source)"
}

# tools/revalidate-here.ps1
# ---------------------------------------------------------------------------------------------
# Run the full re-validation sweep on THIS machine, whatever edition of Visual Studio it has.
#
# revalidate.ps1 looks for vcvarsall.bat at one hardcoded BuildTools path and exits 2 if it is not
# there -- true on the original bench, false on a machine with VS Community. It does, however,
# skip that lookup entirely when VSCMD_VER is already set. So this wrapper imports the environment
# portably first (tools/vsenv.ps1) and then hands off. Nothing in revalidate.ps1 or in any of the
# 349 build.bat files has to change.
#
# Arguments are forwarded verbatim:
#   .\tools\revalidate-here.ps1 -Baseline
#   .\tools\revalidate-here.ps1 -Only 001,002
#   .\tools\revalidate-here.ps1 -SkipLive
# ---------------------------------------------------------------------------------------------
$here = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
. (Join-Path $here 'vsenv.ps1') -Quiet
& (Join-Path $here 'revalidate.ps1') @args
exit $LASTEXITCODE

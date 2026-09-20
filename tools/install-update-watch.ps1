# tools/install-update-watch.ps1
# ---------------------------------------------------------------------------------------------
# Register a Scheduled Task that re-proves this repository against System32 whenever Windows
# services those binaries.
#
# TRIGGERS -- three, deliberately overlapping, because no single one is reliable on its own:
#   1. Event: Microsoft-Windows-WindowsUpdateClient/Operational, Event ID 19 ("installation
#      successful"). Fires the moment an update completes. Registered on a best-effort basis: on
#      some configurations an event subscription needs elevation, and if it cannot be registered
#      the other two still cover the case.
#   2. At logon. Servicing finishes during a reboot, so the first logon afterwards is the most
#      dependable moment to notice - and it catches updates installed while this task did not
#      exist.
#   3. Daily. A backstop for out-of-band component servicing that raises no Event 19.
#
# The action is tools\on-update.ps1, which is cheap when nothing relevant changed: it only runs
# the full sweep if one of the six DLLs we reimplement from actually changed, by hash.
#
# What this does not do. It does not touch System32, does not install anything into Windows, and
# does not need administrator rights for the work itself. It re-PROVES the repository against the
# new binaries and shouts if an update broke a contract. Permanently replacing a signed system DLL
# is a different and much more dangerous thing, and is not automated here - see revalidate.ps1.
#
# USAGE
#   .\install-update-watch.ps1              register (idempotent - replaces an existing task)
#   .\install-update-watch.ps1 -Uninstall   remove it
#   .\install-update-watch.ps1 -RunNow      register, then run it once immediately
# ---------------------------------------------------------------------------------------------
[CmdletBinding()]
param(
    [string] $TaskName  = 'WIA-Revalidate-OnUpdate',
    [string] $Repo      = '',
    [switch] $Uninstall,
    [switch] $RunNow
)

$ErrorActionPreference = 'Stop'
$here = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
if (-not $Repo) { $Repo = Split-Path -Parent $here }
$action_script = Join-Path $here 'on-update.ps1'

if ($Uninstall) {
    if (Get-ScheduledTask -TaskName $TaskName -EA SilentlyContinue) {
        Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false
        Write-Host "removed scheduled task '$TaskName'"
    } else {
        Write-Host "no scheduled task '$TaskName' to remove"
    }
    exit 0
}

if (-not (Test-Path $action_script)) { throw "on-update.ps1 not found at $action_script" }

$action = New-ScheduledTaskAction -Execute 'powershell.exe' `
    -Argument ('-NoProfile -NonInteractive -WindowStyle Hidden -ExecutionPolicy Bypass -File "{0}" -Repo "{1}"' -f $action_script, $Repo) `
    -WorkingDirectory $Repo

$triggers = @()
$triggers += New-ScheduledTaskTrigger -AtLogOn -User "$env:USERDOMAIN\$env:USERNAME"
$triggers += New-ScheduledTaskTrigger -Daily -At '03:30'

# Event trigger: built through the CIM class because New-ScheduledTaskTrigger has no event form.
$eventTriggerOk = $false
try {
    $q = @'
<QueryList><Query Id="0" Path="Microsoft-Windows-WindowsUpdateClient/Operational">
<Select Path="Microsoft-Windows-WindowsUpdateClient/Operational">*[System[Provider[@Name='Microsoft-Windows-WindowsUpdateClient'] and (EventID=19)]]</Select>
</Query></QueryList>
'@
    $cls = Get-CimClass -ClassName MSFT_TaskEventTrigger -Namespace Root/Microsoft/Windows/TaskScheduler
    $ev  = New-CimInstance -CimClass $cls -ClientOnly
    $ev.Enabled      = $true
    $ev.Subscription = $q
    $triggers += $ev
    $eventTriggerOk = $true
} catch {
    Write-Warning "could not build the Windows Update event trigger ($($_.Exception.Message)). Logon + daily triggers still cover it."
}

$settings = New-ScheduledTaskSettingsSet `
    -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
    -StartWhenAvailable -MultipleInstances IgnoreNew `
    -ExecutionTimeLimit (New-TimeSpan -Hours 6)

# Runs as the current interactive user. No elevation: reading System32 and building in the repo
# needs none, and a task that does not need admin is a task that cannot do damage with it.
$principal = New-ScheduledTaskPrincipal -UserId "$env:USERDOMAIN\$env:USERNAME" -LogonType Interactive -RunLevel Limited

if (Get-ScheduledTask -TaskName $TaskName -EA SilentlyContinue) {
    Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false
}
Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger $triggers `
    -Settings $settings -Principal $principal `
    -Description 'Re-proves Windows-11-in-Assembly against System32 after Windows servicing. Runs the full correctness sweep only when a watched DLL actually changed. Never modifies System32.' | Out-Null

Write-Host "registered scheduled task '$TaskName'"
Write-Host "  action    : $action_script"
Write-Host "  repo      : $Repo"
Write-Host "  triggers  : at logon, daily 03:30$(if ($eventTriggerOk) { ', and on WindowsUpdateClient Event ID 19' } else { ' (event trigger unavailable)' })"
Write-Host "  remove    : .\install-update-watch.ps1 -Uninstall"

if ($RunNow) {
    Write-Host "`nrunning once now..."
    Start-ScheduledTask -TaskName $TaskName
}

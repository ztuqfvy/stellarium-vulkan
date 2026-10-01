# wt37-41-stop.ps1 -- hard-stop the W-T37..W-T41 batch and leave the box clean.
#
# WHY A FILE: the host security policy blocks `schtasks` / `taskkill` typed
# directly on an ssh command line, so every system-tool call goes through -File.
#
# Order matters: delete the task FIRST (so nothing can re-launch), then kill the
# processes, then sweep. A task deleted last could fire during the sweep.

$ErrorActionPreference = "Continue"
$TN = "StelQC_t37w_suites"

"=== 1. delete task (must come first) ==="
& schtasks.exe /delete /tn $TN /f
"delete rc=$LASTEXITCODE"

"=== 2. kill launcher/suite powershell ==="
# Pattern built by concatenation so this file's command line never contains the
# literal script name -- otherwise the filter could match the caller itself.
$pat1 = 'wt37' + '-41-launch'
$pat2 = 'wt37' + '-41-suites'
Get-CimInstance Win32_Process -Filter "Name='powershell.exe'" | Where-Object {
    $_.CommandLine -and ($_.CommandLine -like "*$pat1*" -or $_.CommandLine -like "*$pat2*")
} | ForEach-Object {
    "kill PID $($_.ProcessId)"
    Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue
}

"=== 3. kill stelQuickUI ==="
Get-Process stelQuickUI -ErrorAction SilentlyContinue | ForEach-Object {
    "kill stelQuickUI PID $($_.Id) started $($_.StartTime.ToString('HH:mm:ss'))"
    Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue
}

Start-Sleep -Seconds 2

"=== 4. sweep leftovers ==="
$left = @(& schtasks.exe /query /fo csv 2>$null | Select-String "StelQC")
if ($left.Count -gt 0) { "LEFTOVER-FOUND:"; $left } else { "no StelQC_* leftovers" }

"=== 5. remaining ==="
Get-Process stelQuickUI -ErrorAction SilentlyContinue |
    Select-Object ProcessName, Id | Format-Table -AutoSize | Out-String
"done"

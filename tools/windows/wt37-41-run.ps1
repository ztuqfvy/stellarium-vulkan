# wt37-41-run.ps1 -- drive the schtasks /it round-trip for the W-T37..W-T41 batch.
#
# WHY schtasks /it: every suite creates a real QQuickWindow. A GUI program
# started from an SSH session has no desktop session, so the Qt scene graph
# never initializes and the run is meaningless. /it binds the task to the
# logged-on interactive session instead.
#
# THIS SCRIPT ALWAYS DELETES THE TASK AND SWEEPS LEFTOVERS, even on failure --
# a stale StelQC_* task firing later would silently contaminate a future run.
#
# ASCII-only: PS 5.1 reads a BOM-less .ps1 in the ANSI codepage.
$ErrorActionPreference = "Continue"

$TN         = "StelQC_t37w_suites"
$LAUNCH_LOG = "C:\temp\t37w-launch.log"
$SUITE_SUM  = "C:\temp\t37w-suites\SUMMARY.txt"
$TR         = "powershell -NoProfile -ExecutionPolicy Bypass -File C:\temp\wt37-41-launch.ps1"

"=== pre: sweep any leftover task ==="
schtasks /delete /tn $TN /f 2>$null | Out-Null
$preQ = schtasks /query /tn $TN 2>$null
"pre-query says: task " + $(if ($LASTEXITCODE -eq 0) { "STILL PRESENT (bad)" } else { "absent (good)" })

Remove-Item $LAUNCH_LOG -ErrorAction SilentlyContinue
Remove-Item $SUITE_SUM  -ErrorAction SilentlyContinue

"=== create /it task ==="
& schtasks.exe /create /tn $TN /sc once /st 23:59 /it /f /tr $TR
$rcCreate = $LASTEXITCODE
"create rc=$rcCreate"
if ($rcCreate -ne 0) {
    "FATAL create failed -- nothing will run. Common cause: the /tr path does not exist."
    exit 91
}

"=== run ==="
& schtasks.exe /run /tn $TN
$rcRun = $LASTEXITCODE
"run rc=$rcRun"
if ($rcRun -ne 0) {
    & schtasks.exe /delete /tn $TN /f | Out-Null
    "FATAL run failed; task deleted."
    exit 92
}

"=== wait for the launcher's exit line (budget 40 min) ==="
$t0 = Get-Date
$done = $false
while (((Get-Date) - $t0).TotalMinutes -lt 40) {
    if (Test-Path $LAUNCH_LOG) {
        $c = Get-Content $LAUNCH_LOG -Raw -ErrorAction SilentlyContinue
        if ($c -match "launcher exit=") { $done = $true; break }
    }
    Start-Sleep -Seconds 5
}
$waitMin = [math]::Round(((Get-Date) - $t0).TotalMinutes, 2)
"waited_min=$waitMin  sawExitLine=$done"
if (-not $done) {
    "WARNING: timed out waiting. Dumping whatever exists before cleanup."
}

"=== launcher log ==="
if (Test-Path $LAUNCH_LOG) { Get-Content $LAUNCH_LOG } else { "(no launcher log at all)" }

$OUT = "C:\temp\t37w-launch.out.txt"
$ERR = "C:\temp\t37w-launch.err.txt"
"=== child stdout (last 15) ==="
if (Test-Path $OUT) { Get-Content $OUT -Tail 15 } else { "(none)" }
"=== child stderr (last 10) ==="
if (Test-Path $ERR) { Get-Content $ERR -Tail 10 } else { "(none)" }

# A /sc once task that has already run can already be gone; rc=1 here is
# expected noise, NOT a failure. The sweep below is the authoritative check.
"=== delete task ==="
& schtasks.exe /delete /tn $TN /f
"delete rc=$LASTEXITCODE (1 = already gone, harmless)"

"=== sweep leftovers (any StelQC_* task) ==="
$left = @(schtasks /query /fo csv 2>$null | Select-String "StelQC")
if ($left.Count -gt 0) { "LEFTOVER-FOUND:"; $left } else { "no StelQC_* leftovers" }

"=== suite SUMMARY ==="
if (Test-Path $SUITE_SUM) { Get-Content $SUITE_SUM } else { "(no SUMMARY -- the suite script never started)" }

exit 0

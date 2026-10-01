# win-residue-scan.ps1 -- Windows-side residue scan for the Stellarium QML/Vulkan
# verification runs. Pure ASCII on purpose: PowerShell 5.1 reads BOM-less scripts
# using the ANSI code page, so non-ASCII would be mangled.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File win-residue-scan.ps1
#   powershell -NoProfile -ExecutionPolicy Bypass -File win-residue-scan.ps1 -Clean
#
# Default is READ-ONLY: it prints three buckets and removes nothing.
#   [this round]    -- matched by -Match; these are what -Clean removes
#   [earlier rounds]-- matched by -HistoryMatch; REPORTED ONLY, never removed
#                      (this script has not verified their archives, so it is not
#                       its call to delete them)
#   [not ours]      -- matched by nothing; LEFT ALONE (other projects share C:\temp)
#
# Checks performed:
#   1. scheduled tasks left behind (Stel*)
#   2. leftover stel* processes
#   3. staging dir (C:\temp) inventory in the three buckets above
#
# Verified before first use: docs/evidence/2026-10-01-w-t37-41/win/ holds all 46
# t37w-* files byte-identical (md5 46/46) to the Windows side, plus the
# t37w-launch.out.txt / t37w-build.rc / wt-cleanup-report.txt pulled back on
# 2026-10-01 -- so removing the [this round] bucket loses nothing.

param(
    [switch]$Clean,
    [string]$StagingDir = "C:\temp",
    [string[]]$Match = @("t37w-*", "wt37-*", "wt37-41-*", "wtcleanup.ps1", "wt-cleanup-report.txt"),
    [string[]]$HistoryMatch = @("t*w-*", "wt*-*", "win-suites.ps1", "win-longrun.ps1",
                                "judge-diag.ps1", "t17-win-*", "t18-win-*", "t20-win-*",
                                "t33wneg-*")
)

$ErrorActionPreference = "Continue"
$lines = New-Object System.Collections.Generic.List[string]
function W($s) { $lines.Add($s) | Out-Null; Write-Host $s }

W ("=== win-residue-scan.ps1  mode=" + $(if ($Clean) { "CLEAN" } else { "READ-ONLY" }) +
   "  stagedir=" + $StagingDir + " ===")
W ("  -Match        = " + ($Match -join ", "))
W ("  -HistoryMatch = " + ($HistoryMatch -join ", "))

W ""
W "--- 1. scheduled tasks (Stel*) ---"
$tasks = @(schtasks /query /fo csv 2>$null | Select-String -Pattern "Stel")
if ($tasks.Count -eq 0) { W "  none" } else { $tasks | ForEach-Object { W ("  " + $_.Line) } }

W ""
W "--- 2. leftover processes (stel*) ---"
$procs = @(Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -like "stel*" })
if ($procs.Count -eq 0) { W "  none" }
else { $procs | ForEach-Object { W ("  PID " + $_.Id + "  " + $_.ProcessName + "  " + $_.StartTime) } }

W ""
W "--- 3. staging dir inventory (" + $StagingDir + ") ---"
$roundItems = New-Object System.Collections.Generic.List[object]
$olderItems = New-Object System.Collections.Generic.List[object]
$otherItems = New-Object System.Collections.Generic.List[object]
$all = @(Get-ChildItem -LiteralPath $StagingDir -Force -ErrorAction SilentlyContinue)
foreach ($it in $all) {
    $bucket = "other"
    foreach ($p in $Match)        { if ($it.Name -like $p) { $bucket = "round"; break } }
    if ($bucket -eq "other") {
        foreach ($p in $HistoryMatch) { if ($it.Name -like $p) { $bucket = "older"; break } }
    }
    switch ($bucket) {
        "round" { $roundItems.Add($it) | Out-Null }
        "older" { $olderItems.Add($it) | Out-Null }
        default { $otherItems.Add($it) | Out-Null }
    }
}
W ("  total = " + $all.Count + "   this round = " + $roundItems.Count +
   "   earlier rounds = " + $olderItems.Count + "   not ours = " + $otherItems.Count)
W ("  [this round -- " + $(if ($Clean) { "WILL BE REMOVED" } else { "would be removed by -Clean" }) + "]")
foreach ($it in $roundItems) {
    $tag = if ($it.PSIsContainer) { "DIR " } else { "FILE" }
    W ("    " + $tag + " " + $it.Name)
}
W "  [earlier rounds -- REPORTED ONLY, never removed by this script]"
foreach ($it in $olderItems) {
    $tag = if ($it.PSIsContainer) { "DIR " } else { "FILE" }
    W ("    " + $tag + " " + $it.Name + "  " + $it.LastWriteTime)
}
W "  [not matched by any project pattern -- LEFT ALONE]"
foreach ($it in $otherItems) {
    $tag = if ($it.PSIsContainer) { "DIR " } else { "FILE" }
    W ("    " + $tag + " " + $it.Name)
}

if (-not $Clean) {
    W ""
    W "READ-ONLY: nothing removed."
} else {
    W ""
    W "--- 4. removal (this round only) ---"
    foreach ($it in $roundItems) {
        try {
            Remove-Item -LiteralPath $it.FullName -Recurse -Force -ErrorAction Stop
            W ("  removed " + $it.Name)
        } catch {
            W ("  FAILED  " + $it.Name + " : " + $_.Exception.Message)
        }
    }
    W ""
    W "--- 5. after ---"
    $rest = @(Get-ChildItem -LiteralPath $StagingDir -Force -ErrorAction SilentlyContinue)
    $restOur = 0
    foreach ($it in $rest) {
        foreach ($p in $Match) { if ($it.Name -like $p) { $restOur++; break } }
    }
    $t2 = @(schtasks /query /fo csv 2>$null | Select-String -Pattern "StelQC_")
    $p2 = @(Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -like "stel*" })
    W ("  staging items now = " + $rest.Count + "   this-round artifacts remaining = " + $restOur)
    W ("  StelQC_* tasks remaining = " + $t2.Count)
    W ("  stel* processes remaining = " + $p2.Count)
    W ("  RESIDUE-VERDICT = " + $(if ($restOur -eq 0 -and $t2.Count -eq 0 -and $p2.Count -eq 0) { "CLEAN" } else { "RESIDUE-LEFT" }))
}

W ""
W "SCAN-DONE"

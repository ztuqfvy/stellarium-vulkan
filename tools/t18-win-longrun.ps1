# t18-win-longrun.ps1 -- deliver the 30-minute formal long run on Windows
# (merged host form x native Vulkan).
#
# Why this file exists:
#   The first Windows 30min formal run (2026-09-24, commit 5869a76) used a
#   hand-written wrapper that was thrown away afterwards, so reproducing it
#   meant rebuilding the whole invocation by hand. This script freezes it in
#   the repo so "rebuild -> long run -> archive" can be replayed verbatim.
#
# How to launch (schtasks is mandatory, see below):
#   schtasks /create /tn StelQuickT18LongRun /sc once /st 23:59 /it /f ^
#     /tr "powershell -NoProfile -ExecutionPolicy Bypass -File C:\temp\t18-win-longrun.ps1"
#   schtasks /run /tn StelQuickT18LongRun
#
#   /it is NOT optional. A GUI program started from an SSH session has no
#   desktop session, so the Qt scene graph never initializes (measured
#   2026-09-23: log stops at configure, window never appears). The task has to
#   be delivered into the logged-on interactive session.
#
# Runtime PATH (measured, do not trim):
#   Qt bin           -> Qt6*.dll plus the whole QML module tree
#   Vulkan SDK Bin   -> vulkan-1.dll loader
#   util\spout2\x64  -> SpoutLibrary.dll
#   Missing any of them => 0xC0000135 (DLL not found) at startup.
#
# Note on encoding: PowerShell 5.1 `*>` redirection of a native exe writes
#   UTF-16LE. Archiving must transcode by BOM (see docs/WINDOWS_BUILD.zh_CN.md).
#   This script itself is ASCII-only on purpose: PS 5.1 decodes a BOM-less
#   script file using the ANSI codepage, which would garble CJK comments.
#
# All output files land in C:\temp so the Mac side can pull them over the tunnel.

# Parameters exist so the same file can do a 10-second pipeline smoke test
# before committing 45 minutes of machine time:
#   ... -File C:\temp\t18-win-longrun.ps1 -WarmupSeconds 0 -MeasureSeconds 8 -Tag t18-win-smoke
# A smoke run is NOT part of the acceptance evidence: SL-C* thresholds are
# defined for the 1800 s measurement window.
param(
    [string]$Repo           = "E:\Qt_demo\stellarium-vulkan",
    [string]$QtDir          = "E:\Qt\6.11.2\msvc2022_64",
    [string]$VulkanBin      = "E:\Vulkan\SDK\Bin",
    [string]$Tag            = "t18-win-30min",
    [int]$WarmupSeconds     = 900,
    [int]$MeasureSeconds    = 1800
)

$ErrorActionPreference = "Continue"

$exe = Join-Path $Repo "build-win\src\ui\Release\stelQuickUI.exe"
$out = "C:\temp\$Tag.txt"
$rcf = "C:\temp\$Tag.rc.txt"
$csv = "C:\temp\$Tag.csv"

if (-not (Test-Path $exe)) {
    "rc=99" | Out-File -Encoding ascii $rcf
    ("exe not found: " + $exe) | Out-File -Encoding ascii $out
    exit 99
}

Remove-Item $out, $rcf, $csv, ($csv + ".frames.csv") -ErrorAction SilentlyContinue

$env:PATH = (Join-Path $QtDir "bin") + ";" + $VulkanBin + ";" + (Join-Path $Repo "util\spout2\x64") + ";" + $env:PATH

$env:STELQUICK_LONGRUN                = "1"
$env:STELQUICK_LONGRUN_PRODUCER       = "engine"
$env:STELQUICK_LONGRUN_WARMUP_SECONDS = "$WarmupSeconds"
$env:STELQUICK_LONGRUN_SECONDS        = "$MeasureSeconds"
$env:STELQUICK_LONGRUN_CSV            = $csv

Set-Location (Split-Path $exe)

("[start] " + (Get-Date -Format o) + " warmup=" + $WarmupSeconds + "s measure=" + $MeasureSeconds + "s") |
    Out-File -Encoding ascii $out
& $exe *>> $out
$rc = $LASTEXITCODE
"rc=$rc" | Out-File -Encoding ascii $rcf
("[done] " + (Get-Date -Format o) + " rc=" + $rc) | Out-File -Encoding ascii -Append $out

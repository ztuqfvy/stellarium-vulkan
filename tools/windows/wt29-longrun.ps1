# t29w-win-longrun.ps1 -- formal 30-minute long run on Windows (merged host form
# x native Vulkan). The exact commit is recorded in the header file ($hdr) from
# `git rev-parse --short HEAD` at run time; the frozen copy shipped in the repo
# runs against b4e8cf2 (T29 instrument patch).
#
# Derived from t18-win-longrun.ps1 (same shape, same env vars); kept as a
# separate frozen file so this run is replayable verbatim.
#
# Launch (schtasks /it is mandatory -- an SSH session has no desktop session,
# so the Qt scene graph never initializes):
#   schtasks /create /tn StelQC_t29w_longrun /sc once /st 23:59 /it /f ^
#     /tr "powershell -NoProfile -ExecutionPolicy Bypass -File C:\temp\t29w-win-longrun.ps1"
#   schtasks /run /tn StelQC_t29w_longrun
#
# Runtime PATH (measured, do not trim):
#   Qt bin           -> Qt6*.dll plus the whole QML module tree
#   Vulkan SDK Bin   -> vulkan-1.dll loader
#   util\spout2\x64  -> SpoutLibrary.dll
#   Missing any of them => 0xC0000135 (DLL not found) at startup.
#
# Encoding: Start-Process -RedirectStandardOutput writes the exe's raw UTF-8
#   bytes to disk, so no CP936/UTF-16LE round-trip is involved (that is what
#   the older `*>>` wrapper needed the BOM decode recipe for).
#
# ASCII-only on purpose: PS 5.1 decodes a BOM-less .ps1 using the ANSI codepage.
param(
    [string]$Repo           = "E:\Qt_demo\stellarium-vulkan",
    [string]$QtDir          = "E:\Qt\6.11.2\msvc2022_64",
    [string]$VulkanBin      = "E:\Vulkan\SDK\Bin",
    [string]$Tag            = "t29w-win-30min",
    [int]$WarmupSeconds     = 900,
    [int]$MeasureSeconds    = 1800
)

$ErrorActionPreference = "Continue"

$exe = Join-Path $Repo "build-win\src\ui\Release\stelQuickUI.exe"
$out = "C:\temp\$Tag.out.txt"    # raw stdout bytes, straight from the exe
$hdr = "C:\temp\$Tag.head.txt"   # ASCII metadata header (kept separate on purpose)
$err = "C:\temp\$Tag.err.txt"
$rcf = "C:\temp\$Tag.rc.txt"
$csv = "C:\temp\$Tag.csv"

if (-not (Test-Path $exe)) {
    "rc=99" | Out-File -Encoding ascii $rcf
    ("exe not found: " + $exe) | Out-File -Encoding ascii $out
    exit 99
}

Remove-Item $out, $hdr, $err, $rcf, $csv, ($csv + ".frames.csv") -ErrorAction SilentlyContinue

$env:PATH = (Join-Path $QtDir "bin") + ";" + $VulkanBin + ";" + (Join-Path $Repo "util\spout2\x64") + ";" + $env:PATH

Get-ChildItem Env: | Where-Object { $_.Name -like "STELQUICK_*" } | ForEach-Object {
    Remove-Item ("Env:" + $_.Name) -ErrorAction SilentlyContinue
}

$env:STELQUICK_LONGRUN                = "1"
$env:STELQUICK_LONGRUN_PRODUCER       = "engine"
$env:STELQUICK_LONGRUN_WARMUP_SECONDS = "$WarmupSeconds"
$env:STELQUICK_LONGRUN_SECONDS        = "$MeasureSeconds"
$env:STELQUICK_LONGRUN_CSV            = $csv

$wd = Split-Path $exe
Set-Location $wd

"[start] " + (Get-Date -Format o) + " warmup=${WarmupSeconds}s measure=${MeasureSeconds}s" |
    Out-File -Encoding ascii $hdr
"repo HEAD = " + (& git -C $Repo rev-parse --short HEAD) | Out-File -Encoding ascii -Append $hdr
"exe md5   = " + (Get-FileHash $exe -Algorithm MD5).Hash | Out-File -Encoding ascii -Append $hdr
"producer  = engine"    | Out-File -Encoding ascii -Append $hdr
"backend   = native Vulkan (no STELQUICK_GRAPHICS_API override)" | Out-File -Encoding ascii -Append $hdr

$t0 = Get-Date
$p = Start-Process -FilePath $exe -WorkingDirectory $wd `
     -RedirectStandardOutput $out -RedirectStandardError $err `
     -NoNewWindow -Wait -PassThru
$rc = $p.ExitCode
$t1 = Get-Date

"rc=$rc" | Out-File -Encoding ascii $rcf
"elapsed_min=" + [math]::Round(($t1 - $t0).TotalMinutes, 2) |
    Out-File -Encoding ascii -Append $rcf
"[done] " + (Get-Date -Format o) + " rc=$rc" | Out-File -Encoding ascii -Append $hdr

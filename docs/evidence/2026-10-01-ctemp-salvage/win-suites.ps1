# t29w-win-suites.ps1 -- run the T15..T29 self-check suites on Windows and
# collect raw evidence (stdout/stderr/exit code) for each one.
#
# Must be delivered through schtasks /it: every suite below creates a real
# QQuickWindow, and a GUI program started from an SSH session has no desktop
# session, so the Qt scene graph never initializes.
#
#   schtasks /create /tn StelQC_t29w_suites /sc once /st 23:59 /it /f ^
#     /tr "powershell -NoProfile -ExecutionPolicy Bypass -File C:\temp\t29w-win-suites.ps1"
#   schtasks /run /tn StelQC_t29w_suites
#
# Encoding: Start-Process -RedirectStandardOutput writes the raw byte stream
#   to disk -- no PowerShell transcoding step, so the files are exactly the
#   UTF-8 bytes the exe produced (the exe is compiled with MSVC /utf-8).
#   Do NOT use `*> file` here: PS 5.1 turns that into UTF-16LE with a
#   CP936 round-trip first (see the long-run script header for the same note).
#
# ASCII-only on purpose: PS 5.1 reads a BOM-less .ps1 using the ANSI codepage.
param(
    [string]$Repo        = "E:\Qt_demo\stellarium-vulkan",
    [string]$QtDir       = "E:\Qt\6.11.2\msvc2022_64",
    [string]$VulkanBin   = "E:\Vulkan\SDK\Bin",
    [string]$Tag         = "t29w",
    [int]$InteractRuns   = 5
)

$ErrorActionPreference = "Continue"

$exe = Join-Path $Repo "build-win\src\ui\Release\stelQuickUI.exe"
$dir = "C:\temp\$Tag-suites"
New-Item -ItemType Directory -Force -Path $dir | Out-Null

$sum = Join-Path $dir "SUMMARY.txt"
"[$Tag] suites start " + (Get-Date -Format o) | Out-File -Encoding ascii $sum
"repo HEAD = " + (& git -C $Repo rev-parse --short HEAD) | Out-File -Encoding ascii -Append $sum
if (Test-Path $exe) {
    $f = Get-Item $exe
    "exe = $exe" | Out-File -Encoding ascii -Append $sum
    "exe size = $($f.Length) B   mtime = $($f.LastWriteTime.ToString('o'))" | Out-File -Encoding ascii -Append $sum
    "exe md5  = " + (Get-FileHash $exe -Algorithm MD5).Hash | Out-File -Encoding ascii -Append $sum
} else {
    "FATAL exe not found: $exe" | Out-File -Encoding ascii -Append $sum
    exit 99
}

$env:PATH = (Join-Path $QtDir "bin") + ";" + $VulkanBin + ";" + (Join-Path $Repo "util\spout2\x64") + ";" + $env:PATH
Set-Location (Split-Path $exe)

# Wipe any STELQUICK_* the outer session may have left behind.
Get-ChildItem Env: | Where-Object { $_.Name -like "STELQUICK_*" } | ForEach-Object {
    Remove-Item ("Env:" + $_.Name) -ErrorAction SilentlyContinue
}

function Run-Suite {
    param(
        [string]$Name,
        [string]$Var,          # env var that switches the suite on
        [string]$OutName = $Name,
        [string]$Producer = "" # STELQUICK_DYN_PRODUCER ("engine" / "test"); "" = leave unset
    )
    $so = Join-Path $dir "$OutName.out.txt"
    $se = Join-Path $dir "$OutName.err.txt"
    Remove-Item $so, $se -ErrorAction SilentlyContinue

    if ($Var) { Set-Item -Path ("Env:" + $Var) -Value "1" }
    if ($Producer) { Set-Item -Path "Env:STELQUICK_DYN_PRODUCER" -Value $Producer }

    $t0 = Get-Date
    $p = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) `
         -RedirectStandardOutput $so -RedirectStandardError $se `
         -NoNewWindow -Wait -PassThru
    $rc = $p.ExitCode
    $t1 = Get-Date

    # leave no variable behind: every suite must start from a clean env
    Get-ChildItem Env: | Where-Object { $_.Name -like "STELQUICK_*" } | ForEach-Object {
        Remove-Item ("Env:" + $_.Name) -ErrorAction SilentlyContinue
    }

    $el = [math]::Round(($t1 - $t0).TotalSeconds, 1)
    "SUITE $OutName  rc=$rc  elapsed=${el}s  var=$Var  producer=$Producer" |
        Out-File -Encoding ascii -Append $sum
    # producer read-back: a "discriminating control" must prove WHO was controlled.
    # main.cpp only installs the real engine when STELQUICK_DYN_PRODUCER == "engine"
    # explicitly; without this line the label is self-asserted. tools/t29-verify.sh
    # lost exactly that variable -- see docs/evidence/2026-09-29-t29-ime/CORRECTION.md.
    if ($Producer) {
        $first = Get-Content $so -Encoding UTF8 | Select-String -SimpleMatch "DYNCHECK:" |
                 Select-Object -First 1
        "  producer-readback: $($first.Line)" | Out-File -Encoding utf8 -Append $sum
    }
    return $rc
}

# ---- the suites, in dependency order -------------------------------------
Run-Suite -Name "CLOCK"    -Var "STELQUICK_CLOCK_CHECK"    -OutName "clockcheck"
Run-Suite -Name "ACTION"   -Var "STELQUICK_ACTION_CHECK"   -OutName "actioncheck"
Run-Suite -Name "SEARCH"   -Var "STELQUICK_SEARCH_CHECK"   -OutName "searchcheck"
Run-Suite -Name "LOCATE"   -Var "STELQUICK_LOCATE_CHECK"   -OutName "locatecheck"
Run-Suite -Name "LOCATEUI" -Var "STELQUICK_UI_CHECK"       -OutName "locate-uicheck"
Run-Suite -Name "TIME"     -Var "STELQUICK_TIME_CHECK"     -OutName "timecheck"
Run-Suite -Name "TIMEUI"   -Var "STELQUICK_TIME_UI_CHECK"  -OutName "timeuicheck"
Run-Suite -Name "RETURNUI" -Var "STELQUICK_RETURN_UI_CHECK" -OutName "returnuicheck"
Run-Suite -Name "REPLAY"   -Var "STELQUICK_REPLAY_CHECK"   -OutName "replaycheck"

# ---- the T29 positive case, repeated (flakiness is a first-class reading) --
for ($i = 1; $i -le $InteractRuns; $i++) {
    Run-Suite -Name "INTERACT" -Var "STELQUICK_INTERACT_UI_CHECK" -OutName "interactcheck-run$i"
}

# ---- A2 pixel baseline (native Vulkan, NOT Metal) ------------------------
$env:STELQUICK_A2_DUMP = Join-Path $dir "a2-vulkan.png"
Run-Suite -Name "A2" -Var "STELQUICK_A2_CHECK" -OutName "a2-vulkan"
Remove-Item Env:STELQUICK_A2_DUMP -ErrorAction SilentlyContinue

# ---- DYN: two-way discriminating control ---------------------------------
# The producer MUST be given explicitly. The old comment here ("engine producer
# is chosen internally") was wrong: main.cpp:4120-4121 only installs the real
# engine when qgetenv("STELQUICK_DYN_PRODUCER") == "engine"; leaving it unset
# means the stub. The previous 3 runs were all stubs while being labelled
# "DYN(engine)" -- same defect as tools/t29-verify.sh (see CORRECTION.md).
foreach ($k in @("engine", "test")) {
    for ($i = 1; $i -le 3; $i++) {
        Run-Suite -Name "DYN-$k" -Var "STELQUICK_DYN_CHECK" `
                  -OutName "dyn-$k-run$i" -Producer $k
    }
}

"[done] " + (Get-Date -Format o) | Out-File -Encoding ascii -Append $sum

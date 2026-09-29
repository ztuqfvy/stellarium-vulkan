# wt30-suites.ps1 -- run the full self-check batch on Windows for T30, plus the
# new T30 negative control (break visual facts; the visual criteria must flip).
#
# Why this file exists: T30 adds eight visual-layer criteria (UI-17..UI-24) to
# TIMEUICHECK and a negative-control switch STELQUICK_TIME_VISUAL_NEGCTL. Both
# live in src/ui/main.cpp -- the host layer. Per the W-branch rule ("host-layer
# changes get cross-platform verification"), the whole batch is re-run here on
# native Vulkan, and the negative control is re-run here too.
#
# Must be delivered through schtasks /it: every suite below creates a real
# QQuickWindow, and a GUI program started from an SSH session has no desktop
# session, so the Qt scene graph never initializes (see the uu-remote-windows
# skill: also, such a program is NOT the foreground window -- key-injection
# suites rely on the bounded activation gate added in W-T29).
#
#   schtasks /create /tn StelQC_t30w_suites /sc once /st 23:59 /it /f ^
#     /tr "powershell -NoProfile -ExecutionPolicy Bypass -File C:\temp\t30w-win-suites.ps1"
#   schtasks /run /tn StelQC_t30w_suites
#
# Encoding: Start-Process -RedirectStandardOutput writes the raw byte stream to
#   disk -- no PowerShell transcoding step, so the files hold exactly the UTF-8
#   bytes the exe produced (the exe is compiled with MSVC /utf-8). Do NOT use
#   `*> file` here: PS 5.1 turns that into UTF-16LE after a CP936 round-trip.
#
# ASCII-only on purpose: PS 5.1 reads a BOM-less .ps1 using the ANSI codepage,
#   so a Chinese comment or a Chinese regex literal would be mangled into stray
#   quotes (PARSE-ERR). All pattern matching below therefore uses ASCII-only
#   anchors ("UI-17", "27/27", trailing "FAIL"/"OK"); the Chinese text inside
#   the log files is data and never enters the parser.
param(
    [string]$Repo      = "E:\Qt_demo\stellarium-vulkan",
    [string]$QtDir     = "E:\Qt\6.11.2\msvc2022_64",
    [string]$VulkanBin = "E:\Vulkan\SDK\Bin",
    [string]$Tag       = "t30w",
    [int]$InteractRuns = 5,
    [int]$NegRuns      = 3,
    [int]$CoreRuns     = 5,
    [switch]$DynOnly,
    [switch]$NegOnly
)

$ErrorActionPreference = "Continue"
$script:mismatch = 0
$script:bad = 0

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
        [string]$Producer = "", # STELQUICK_DYN_PRODUCER ("engine" / "test"); "" = leave unset
        [hashtable]$Extra = @{} # additional env vars for this run (T30 negative control)
    )
    $so = Join-Path $dir "$OutName.out.txt"
    $se = Join-Path $dir "$OutName.err.txt"
    Remove-Item $so, $se -ErrorAction SilentlyContinue

    if ($Var) { Set-Item -Path ("Env:" + $Var) -Value "1" }
    if ($Producer) { Set-Item -Path "Env:STELQUICK_DYN_PRODUCER" -Value $Producer }
    foreach ($k in $Extra.Keys) { Set-Item -Path ("Env:" + $k) -Value $Extra[$k] }

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
    #
    # The observed value is parsed with an ASCII-only regex ("=engine" / "=test")
    # so this file stays pure ASCII while the SUMMARY stays single-encoding.
    # A mismatch is recorded AND counted.
    if ($Producer) {
        $line = (Get-Content $so -Encoding UTF8 |
                 Select-String -SimpleMatch "DYNCHECK:" |
                 Select-Object -First 1).Line
        $rb = "none"
        if ($line -match "=(engine|test)") { $rb = $Matches[1] }
        $verdict = if ($rb -eq $Producer) { "OK" } else { "MISMATCH" }
        if ($verdict -ne "OK") { $script:mismatch++ }
        "  producer-readback: requested=$Producer observed=$rb $verdict" |
            Out-File -Encoding ascii -Append $sum
    }
    return $rc
}

# Criterion-level verdict helper (T30). ASCII-only matching: a criterion line
# looks like "TIMEUICHECK: UI-17 <chinese>...:OK|FAIL"; we key on the id and the
# trailing ASCII verdict, never on the Chinese middle.
function Get-CriterionVerdicts {
    param([string]$File)
    $res = @{}
    foreach ($l in (Get-Content $File -Encoding UTF8)) {
        if (-not $l.StartsWith("TIMEUICHECK: UI-")) { continue }
        $id = ($l -split " ")[1]
        if ($l.TrimEnd().EndsWith("FAIL")) { $res[$id] = "FAIL" }
        elseif ($l.TrimEnd().EndsWith("OK")) { $res[$id] = "OK" }
        else { $res[$id] = "OTHER" }
    }
    return $res
}

# ---- the suites, in dependency order -------------------------------------
# -DynOnly skips everything except the DYN two-way control below. It exists so
# the producer fingerprint can be re-taken WITHOUT re-running the whole batch
# (long-run / frame-rate readings must not be disturbed).
# -NegOnly runs only the T30 negative control.
if (-not $DynOnly -and -not $NegOnly) {
Run-Suite -Name "CLOCK"    -Var "STELQUICK_CLOCK_CHECK"    -OutName "clockcheck"
Run-Suite -Name "ACTION"   -Var "STELQUICK_ACTION_CHECK"   -OutName "actioncheck"
Run-Suite -Name "SEARCH"   -Var "STELQUICK_SEARCH_CHECK"   -OutName "searchcheck"
Run-Suite -Name "LOCATE"   -Var "STELQUICK_LOCATE_CHECK"   -OutName "locatecheck"
Run-Suite -Name "LOCATEUI" -Var "STELQUICK_UI_CHECK"       -OutName "locate-uicheck"
Run-Suite -Name "TIME"     -Var "STELQUICK_TIME_CHECK"     -OutName "timecheck"
Run-Suite -Name "RETURNUI" -Var "STELQUICK_RETURN_UI_CHECK" -OutName "returnuicheck"
Run-Suite -Name "REPLAY"   -Var "STELQUICK_REPLAY_CHECK"   -OutName "replaycheck"

# ---- T30 positive case: TIMEUICHECK x CoreRuns, must be 27/27 every time ----
for ($i = 1; $i -le $CoreRuns; $i++) {
    $rc = Run-Suite -Name "TIMEUI" -Var "STELQUICK_TIME_UI_CHECK" -OutName "timeuicheck-run$i"
    $so = Join-Path $dir "timeuicheck-run$i.out.txt"
    $tot = (Get-Content $so -Encoding UTF8 | Where-Object { $_ -like "*27/27*" } |
            Select-Object -First 1)
    $ok = ($rc -eq 0) -and $tot
    if (-not $ok) { $script:bad++ }
    "  TIMEUI run${i}: rc=$rc  criteria27of27=$([bool]$tot)  -> " +
    $(if ($ok) { "OK" } else { "BAD" }) | Out-File -Encoding ascii -Append $sum
}

# ---- the T29 positive case, repeated (flakiness is a first-class reading) --
for ($i = 1; $i -le $InteractRuns; $i++) {
    Run-Suite -Name "INTERACT" -Var "STELQUICK_INTERACT_UI_CHECK" -OutName "interactcheck-run$i"
}

# ---- A2 pixel baseline (native Vulkan, NOT Metal) ------------------------
$env:STELQUICK_A2_DUMP = Join-Path $dir "a2-vulkan.png"
Run-Suite -Name "A2" -Var "STELQUICK_A2_CHECK" -OutName "a2-vulkan"
Remove-Item Env:STELQUICK_A2_DUMP -ErrorAction SilentlyContinue
}

# ---- T30 negative control -------------------------------------------------
# Break four visual facts in the observation surface only (no product change):
#   month field height -> 0        (UI-17 degenerate geometry)
#   year field visible -> false    (UI-20 visibility / UI-22 hit test)
#   JD row text       -> ""        (UI-21 empty text)
#   status row colour -> its own effective background (UI-23 contrast = 1.0)
# Expectation: rc=10 AND *exactly* UI-17/20/21/22/23 FAIL, everything else OK.
# UI-18/UI-19 stay green by design (their geometry snapshot is taken on the
# next tick, after the layout polish has restored the height) -- their
# correctness is carried by the UI-24 pure-logic leg instead.
if (-not $DynOnly) {
$mustFlip = @("UI-17", "UI-20", "UI-21", "UI-22", "UI-23")
for ($i = 1; $i -le $NegRuns; $i++) {
    $rc = Run-Suite -Name "NEGCTL" -Var "STELQUICK_TIME_UI_CHECK" `
                    -OutName "negctl-run$i" `
                    -Extra @{ "STELQUICK_TIME_VISUAL_NEGCTL" = "1" }
    $so = Join-Path $dir "negctl-run$i.out.txt"
    $v = Get-CriterionVerdicts -File $so
    $fails = @($v.Keys | Where-Object { $v[$_] -eq "FAIL" })
    $missing = @($mustFlip | Where-Object { -not ($fails -contains $_) })
    $unexpected = @($fails | Where-Object { $mustFlip -notcontains $_ })
    $ok = ($rc -eq 10) -and ($missing.Count -eq 0) -and ($unexpected.Count -eq 0)
    if (-not $ok) { $script:bad++ }
    "  NEGCTL run${i}: rc=$rc  fails=[$($fails -join ',')]  " +
    "missing=[$($missing -join ',')]  unexpected=[$($unexpected -join ',')]  -> " +
    $(if ($ok) { "OK" } else { "BAD" }) | Out-File -Encoding ascii -Append $sum
}
}

# ---- DYN: two-way discriminating control ---------------------------------
# The producer MUST be given explicitly. The old comment ("engine producer is
# chosen internally") was wrong: main.cpp only installs the real engine when
# qgetenv("STELQUICK_DYN_PRODUCER") == "engine"; leaving it unset means the
# stub. The previous 3 runs were all stubs while labelled "DYN(engine)" -- the
# same defect as tools/t29-verify.sh (see CORRECTION.md).
if (-not $NegOnly) {
foreach ($k in @("engine", "test")) {
    for ($i = 1; $i -le 3; $i++) {
        Run-Suite -Name "DYN-$k" -Var "STELQUICK_DYN_CHECK" `
                  -OutName "dyn-$k-run$i" -Producer $k
    }
}
}

"[done] " + (Get-Date -Format o) | Out-File -Encoding ascii -Append $sum
if ($script:mismatch -gt 0) {
    "FATAL producer read-back mismatch x$($script:mismatch) -- the discriminating " +
    "control did not control what it claimed" | Out-File -Encoding ascii -Append $sum
    exit 98
}
if ($script:bad -gt 0) {
    "FATAL $($script:bad) expectation check(s) failed (TIMEUI / NEGCTL)" |
        Out-File -Encoding ascii -Append $sum
    exit 97
}

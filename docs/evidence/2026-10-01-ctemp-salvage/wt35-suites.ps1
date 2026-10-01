# wt35-suites.ps1 -- Windows cross-platform verification for T34 + T35 in ONE trip.
#
# WHY BOTH AT ONCE: T33's W-run proved a cross-platform re-run can EXPOSE a real
# Judging-criteria defect (that box's config.ini sets
# localization/time_zone=Asia/Shanghai -> StelCore sets flagUseCTZ -> the
# timezone linkage in setObserver is skipped BY DESIGN -> LC-03a went red on
# every run). T34 (real toolbar, QML layout + style) and T35 (time link, wall
# clock + frame pump) carry the same class of risk, and one trip amortises the
# cost.
#
# WHAT T34/T35 ADD
#   T34  src/app/ToolbarProbe.*   STELQUICK_TOOL_PROBE=1        (readings only)
#        src/app/ToolbarCheck.*   STELQUICK_TOOL_CHECK=1        TB-01..TB-12
#        neg switches: STELQUICK_TOOL_REV_OFF / _TOKEN_OFF / _CLICK_OFF /
#                      _LAYOUT_BREAK   (FOUR groups, red sets pairwise distinct)
#   T35  src/app/TimeLinkProbe.*  STELQUICK_TIMELINK_PROBE=1    (readings only)
#        src/app/TimeLinkCheck.*  STELQUICK_TIMELINK_CHECK=1    TL-01..TL-07
#        neg switches: STELQUICK_TIMELINK_BREAK / _RATE_IGNORED / _FREEZE_LEAK
#
# EXPECTED RED SETS (measured on mac, 2026-09-30; a DIFFERENT set here is either
# a calibration finding or a real defect -- it is NEVER washed into PASS)
#   TB neg A REV_OFF       rc=10  judge  9/12  red exactly [TB-07,TB-09,TB-12]
#   TB neg B TOKEN_OFF     rc=10  judge 10/12  red exactly [TB-09,TB-12]
#   TB neg C CLICK_OFF     rc=10  judge 11/12  red exactly [TB-10]
#   TB neg D LAYOUT_BREAK  rc=10  judge 11/12  red exactly [TB-10]
#                          AND the landing-coverage line must read covered=false
#   TL neg A BREAK         rc=10  judge  6/7   red exactly [TL-01]
#   TL neg B RATE_IGNORED  rc=10  judge  4/7   red exactly [TL-01,TL-02,TL-05]
#   TL neg C FREEZE_LEAK   rc=10  judge  6/7   red exactly [TL-04]
#
# POSITIVE CASES
#   TOOLBARCHECK rc=0  judge 12/12  VERDICT=PASS  no red  TB-09 self-evidence
#                ("12/12 buttons found") present  + coverage line covered=true
#   TIMELINKCHECK rc=0 judge  7/7   VERDICT=PASS  no red  zero placeholder leaks
#                ("%1" / "%.4f")  restore line present  AND the script
#                INDEPENDENTLY recomputes TL-01's ratio and TL-07's residual
#                from the raw readings (never re-reads the check's own PASS).
#
# WINDOW READINESS GATE (T35): TimeLinkCheck re-arms a window whose measured
#   width exceeds 1.25x the nominal value (up to 3 retries) and retests the SAME
#   step. Its log lines are the only TIMELINKCHECK lines containing "25%", so
#   the gate-hit count is reported as a READING. A nonzero count is NOT a
#   failure -- but if the gate fires on EVERY window, the numbers below are
#   suspect and the budget needs re-calibration on this platform.
#
# Must be delivered through schtasks /it: every suite below creates a real
# QQuickWindow, and a GUI program started from an SSH session has no desktop
# session (the Qt scene graph never initializes).
#
#   schtasks /create /tn StelQC_t35w_suites /sc once /st 23:59 /it /f ^
#     /tr "powershell -NoProfile -ExecutionPolicy Bypass -File C:\temp\t35w-suites.ps1"
#   schtasks /run /tn StelQC_t35w_suites
#   schtasks /delete /tn StelQC_t35w_suites /f          <-- ALWAYS delete after
#   (and check for leftovers: schtasks /query /fo csv | Select-String "StelQC")
#
# Encoding: Start-Process -RedirectStandardOutput writes the raw byte stream to
#   disk -- no PowerShell transcoding, so the files hold exactly the UTF-8 bytes
#   the exe produced (compiled with MSVC /utf-8). Do NOT use `*> file` here.
#
# ASCII-only on purpose: PS 5.1 reads a BOM-less .ps1 using the ANSI codepage,
#   so a Chinese comment or a Chinese regex literal would be mangled into stray
#   quotes (PARSE-ERR). The check/cross prefixes are therefore built at runtime
#   from [char]0x2713 / [char]0x2717, never written as literals. The count word
#   that follows them in the criterion lines is NOT matched at all -- it is
#   skipped by a generic "\s+[^\s]+\s+" (see Get-Judge below); an earlier
#   revision did match it and that is precisely what turned into the silent
#   regression documented further down. Every criterion id (TB-xx, TL-xx,
#   LC-xx, IT-xx) is ASCII so no id ever appears as a literal.
param(
    [string]$Repo      = "E:\Qt_demo\stellarium-vulkan",
    [string]$QtDir     = "E:\Qt\6.11.2\msvc2022_64",
    [string]$VulkanBin = "E:\Vulkan\SDK\Bin",
    [string]$Tag       = "t35w",
    [int]$PosRuns      = 3,
    [int]$TbNegRuns    = 1,
    [int]$TlNegRuns    = 1,
    [int]$DynRuns      = 3,
    [switch]$ProbeOnly,
    [switch]$NegOnly,
    [switch]$DynOnly
)

$ErrorActionPreference = "Continue"
$script:mismatch = 0
$script:bad = 0

# Criterion-line prefixes, built from their code points so this file stays
# ASCII-only.
#
# !!! NEVER NAME THESE "$OK" / "$BAD". PowerShell variable names are
#     CASE-INSENSITIVE, so a per-iteration verdict boolean called "$ok"
#     silently OVERWRITES the check-mark character "$OK". Measured 2026-09-29
#     (W-T31): "$integrity" was right on iteration 1, then "$ok = $true" turned
#     the match pattern into "INTERACTCHECK: True INTERACT-INTEGRITY*" for every
#     later run -- integrityOK=0 from run 2 on, while the logs on disk stayed
#     byte-identical. "$MARK_OK" cannot collide with "$pass".
$MARK_OK  = [char]0x2713                 # check mark prefix used by criteria lines
$MARK_BAD = [char]0x2717                 # cross mark prefix
$DELTA    = [char]0x0394                 # Greek capital delta (in the readings)

# !!! AND THE SAME TRAP ONCE MORE, MEASURED ON THIS VERY BOX 2026-09-30 !!!
#     The first revision of this file had a token "$JUDGE" (built from code
#     points, = the count word) AND, in the caller, "$judge = Get-Judge ...".
#     PowerShell variable names are CASE-INSENSITIVE, so the second call onwards
#     saw $JUDGE = "12/12|PASS" -- the token became the previous RESULT. The
#     judge regex then degenerated into
#         ^TOOLBARCHECK:\s*12/12|PASS\s*([0-9]+)/([0-9]+)...
#     i.e. an ALTERNATION whose right branch is UNANCHORED, so it matched
#     nonsense and the captures came back empty. Observed in the batch SUMMARY:
#         TOOLBAR run1: judge=12/12|PASS -> pos-pass     (first call, token intact)
#         TOOLBAR run2: judge=|PASS      -> BAD          (token overwritten)
#         TOOLBAR run3: judge=/|PASS     -> BAD          ("/" = both captures $null)
#     The red-id sets, the independent recomputations and the probes were all
#     unaffected -- ONLY the count comparison broke, which is why the batch looked
#     like a product regression when it was purely an instrument defect.
#     FIX (two independent measures, on purpose):
#       1. the count word is no longer needed at all -- the matcher is now
#          ASCII-ONLY (see $reJ in Get-Judge), so no non-ASCII token exists;
#       2. the caller's result variable is "$judgeTxt", which cannot collide.

$TB_RED = "TOOLBARCHECK:\s+"    + $MARK_BAD + "\s+(TB-[0-9]+)"
$TL_RED = "TIMELINKCHECK:\s+"   + $MARK_BAD + "\s+(TL-[0-9]+)"
$LC_RED = "LOCATIONCHECK:\s+"   + $MARK_BAD + "\s+(LC-[0-9]+[ab]?)"
$LC_OK  = "LOCATIONCHECK:\s+"   + $MARK_OK  + "\s+(LC-[0-9]+[ab]?)"
$LOCRED = $LC_RED
$LOCOK  = $LC_OK

$exe = Join-Path $Repo "build-win\src\ui\Release\stelQuickUI.exe"
$dir = "C:\temp\$Tag-suites"
New-Item -ItemType Directory -Force -Path $dir | Out-Null

$sum = Join-Path $dir "SUMMARY.txt"
"[$Tag] suites start " + (Get-Date -Format o) | Out-File -Encoding ascii $sum
"repo HEAD = " + (& git -C $Repo rev-parse --short HEAD) | Out-File -Encoding ascii -Append $sum
# The instrument's own hash. This batch is normally launched from a COPY at
# C:\temp, and the E: checkout is updated by SCP because this box often cannot
# reach github, so a repo HEAD alone cannot prove which revision produced these
# numbers. Print it and compare against the repo copy by hand.
"script md5 = " + (Get-FileHash $MyInvocation.MyCommand.Path -Algorithm MD5).Hash.ToLower() |
    Out-File -Encoding ascii -Append $sum
foreach ($rel in @("src\app\ToolbarCheck.hpp",  "src\app\ToolbarCheck.cpp",
                   "src\app\ToolbarProbe.cpp",  "src\app\TimeLinkCheck.hpp",
                   "src\app\TimeLinkCheck.cpp", "src\app\TimeLinkProbe.cpp",
                   "src\app\AppFacade.cpp",     "src\ui\LiveSkyRuntime.cpp",
                   "src\ui\main.cpp",           "src\ui\qml\Toolbar.qml",
                   "src\ui\qml\MainWindow.qml")) {
    $s = Join-Path $Repo $rel
    if (Test-Path $s) {
        "SRC $rel md5=" + (Get-FileHash $s -Algorithm MD5).Hash.ToLower() |
            Out-File -Encoding ascii -Append $sum
    } else {
        "SRC MISSING $rel" | Out-File -Encoding ascii -Append $sum
    }
}
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
        [string]$Var,
        [string]$OutName = $Name,
        [string]$Producer = "",
        [hashtable]$Extra = @{},
        [string]$ExePath = ""
    )
    if (-not $ExePath) { $ExePath = $exe }
    $so = Join-Path $dir "$OutName.out.txt"
    $se = Join-Path $dir "$OutName.err.txt"
    Remove-Item $so, $se -ErrorAction SilentlyContinue

    if ($Var) { Set-Item -Path ("Env:" + $Var) -Value "1" }
    if ($Producer) { Set-Item -Path "Env:STELQUICK_DYN_PRODUCER" -Value $Producer }
    foreach ($k in $Extra.Keys) { Set-Item -Path ("Env:" + $k) -Value $Extra[$k] }

    # Absorb any leftover instance from the previous suite before starting a new
    # one. Continuous start/stop is what makes the engine drop the Metal/Vulkan
    # device on mac; on Windows the risk is a stale process holding the DLLs.
    Get-Process stelQuickUI -ErrorAction SilentlyContinue | ForEach-Object {
        $_.WaitForExit(8000) | Out-Null
    }

    $t0 = Get-Date
    $p = Start-Process -FilePath $ExePath -WorkingDirectory (Split-Path $ExePath) `
         -RedirectStandardOutput $so -RedirectStandardError $se `
         -NoNewWindow -Wait -PassThru
    $rc = $p.ExitCode
    $t1 = Get-Date

    Get-ChildItem Env: | Where-Object { $_.Name -like "STELQUICK_*" } | ForEach-Object {
        Remove-Item ("Env:" + $_.Name) -ErrorAction SilentlyContinue
    }

    $el = [math]::Round(($t1 - $t0).TotalSeconds, 1)
    "SUITE $OutName  rc=$rc  elapsed=${el}s  var=$Var  producer=$Producer" |
        Out-File -Encoding ascii -Append $sum

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

# Read a suite's output file once it has SETTLED (two consecutive reads with an
# unchanged size). Bounded: if it never settles we hand back the last snapshot
# and the caller still judges it BAD -- a real defect must not hide behind a wait.
#
# !!! DO NOT "return ,$lines" HERE. The comma idiom only works when the CALLER
#     assigns the result directly; every caller below wraps with @( ... ), and
#     @() does NOT flatten a nested array -- so ",$lines" plus "@( ... )" yields
#     a 1-element array whose single element IS the line array. $txt then has
#     Count 1 and every downstream Where-Object sees the whole array as $_
#     (an array -like <pattern> is truthy whenever ANY line matches), which
#     silently reports "1 line matched". Measured 2026-09-29 (W-T31):
#     unavailNamed=1(5) while the files on disk were byte-identical.
function Read-Lines {
    param([string]$File, [int]$Tries = 8, [int]$SettleMs = 250)
    $script:rlAttempts = 0
    $prev = -1
    $lines = @()
    for ($t = 1; $t -le $Tries; $t++) {
        $script:rlAttempts = $t
        $len = -1
        if (Test-Path $File) { $len = (Get-Item $File).Length }
        $lines = @(Get-Content $File -Encoding UTF8 -ErrorAction SilentlyContinue)
        if ($len -gt 0 -and $len -eq $prev) { return $lines }   # size settled
        $prev = $len
        Start-Sleep -Milliseconds $SettleMs
    }
    return $lines
}

# Collect the criterion ids that went red, matched by the given ASCII pattern
# (the check/cross prefix inside it comes from a code point, never a literal).
function Get-RedIds {
    param([string]$File, [string]$Pattern)
    $ids = @()
    foreach ($l in @(Read-Lines -File $File)) {
        if ($l -match $Pattern) { $ids += $Matches[1] }
    }
    return @($ids | Sort-Object -Unique)
}

# Judge line -> "N/M|VERDICT". Works for both shapes measured on mac:
#   "TOOLBARCHECK: <judge-word> 12/12  VERDICT=PASS"   (criterion count + verdict on one line)
#   "LOCATIONCHECK: <judge-word> 10/10"                (verdict on its own line)
#
# ASCII-ONLY on purpose. The first revision matched the Chinese count word with a
# token built from code points -- which is exactly what turned the $JUDGE/$judge
# name collision (see the header) into a silent, product-looking regression. The
# word is NOT needed: "\s+[^\s]+\s+" skips whatever single non-space token sits
# between the tag and the count, and stays immune to any variable name.
function Get-Judge {
    param([string]$File, [string]$TagName)
    $jm = ""
    $vd = ""
    $reJ = "^" + $TagName + ":\s+[^\s]+\s+([0-9]+)/([0-9]+)(\s+VERDICT=([A-Z]+))?"
    $reV = "^" + $TagName + ": VERDICT=([A-Z]+)"
    foreach ($l in @(Read-Lines -File $File)) {
        if ($jm -eq "" -and $l -match $reJ) {
            $jm = "$($Matches[1])/$($Matches[2])"
            if ($Matches[4]) { $vd = $Matches[4] }
        }
        if ($vd -eq "" -and $l -match $reV) { $vd = $Matches[1] }
    }
    return "$jm|$vd"
}

# Count lines that start with a given suite prefix AND match an ASCII regex.
function Count-Match {
    param([string]$File, [string]$Prefix, [string]$Regex)
    $n = 0
    foreach ($l in @(Read-Lines -File $File)) {
        if ($l.StartsWith($Prefix) -and $l -match $Regex) { $n++ }
    }
    return $n
}

# Count criteria lines carrying the cross prefix for a given suite.
function Count-Cross {
    param([string]$File, [string]$TagName)
    $n = 0
    $pre = $TagName + ": " + $MARK_BAD
    foreach ($l in @(Read-Lines -File $File)) {
        if ($l.StartsWith($pre)) { $n++ }
    }
    return $n
}

# The T35 recheck helpers. These RE-DERIVE the physics from the printed raw
# values instead of re-reading the check's own verdict -- "the control quantity
# must come from a different source" (T33's worst false-green).
#
#   TL-01:  predicted dJD = W * rate * scale, with scale printed as 1 here
#   TL-07:  dLST  ==  dJD * 360.9856091 deg   (sidereal rate, deg/day)
function Recheck-TL01 {
    param([string]$File)
    $line = ""
    foreach ($l in @(Read-Lines -File $File)) {
        if ($l -match ("^TIMELINKCHECK:.*" + $MARK_OK + "\s+TL-01")) { $line = $l; break }
    }
    if ($line -eq "") { return "PARSE-FAIL(no TL-01 ok line)" }
    $patJD = $DELTA + "JD=([0-9.]+)"
    if (-not ($line -match "W=([0-9.]+)s"))     { return "PARSE-FAIL(W)" }
    $w = [double]$Matches[1]
    if (-not ($line -match $patJD))             { return "PARSE-FAIL(dJD)" }
    $d = [double]$Matches[1]
    if (-not ($line -match "rate=([0-9.eE+-]+)")) { return "PARSE-FAIL(rate)" }
    $r = [double]$Matches[1]
    if ($w -le 0 -or $r -le 0) { return "BAD-PRED w=$w r=$r" }
    $pred = $w * $r
    $rel  = [math]::Abs($d - $pred) / $pred
    $verdict = if ($rel -le 0.15) { "OK" } else { "BAD" }
    return ("W=$w dJD=$d rate=$r pred=$pred ratio=" + [math]::Round($d / $pred, 4) +
            " rel_dev=" + [math]::Round($rel * 100, 2) + "% " + $verdict)
}

function Recheck-TL07 {
    param([string]$File)
    $line = ""
    foreach ($l in @(Read-Lines -File $File)) {
        if ($l -match ("^TIMELINKCHECK:.*" + $MARK_OK + "\s+TL-07")) { $line = $l; break }
    }
    if ($line -eq "") { return "PARSE-FAIL(no TL-07 ok line)" }
    $patJD  = $DELTA + "JD=([0-9.]+)"
    $patLST = $DELTA + "LST=([+-][0-9.]+)"
    if (-not ($line -match $patJD))  { return "PARSE-FAIL(dJD)" }
    $d = [double]$Matches[1]
    if (-not ($line -match $patLST)) { return "PARSE-FAIL(dLST)" }
    $l = [double]$Matches[1]
    $expected = $d * 360.9856091
    $res = [math]::Abs($l - $expected)
    $tol = [math]::Abs($expected) * 0.001
    if ($tol -lt 1.0) { $tol = 1.0 }
    $verdict = if ($res -le $tol) { "OK" } else { "BAD" }
    return ("dJD=$d dLST=$l expected=" + [math]::Round($expected, 4) +
            " residual=" + [math]::Round($res, 4) + " tol=" + [math]::Round($tol, 2) +
            " " + $verdict)
}

# ==========================================================================
# 1. the plain regression suites, in dependency order
# NOTE the three switches must be honoured here too. In W-T31 "-NegOnly" was
# checked only by the probe/DYN blocks, so it silently ran the whole regression.
# ==========================================================================
if (-not $ProbeOnly -and -not $DynOnly -and -not $NegOnly) {
Run-Suite -Name "CLOCK"    -Var "STELQUICK_CLOCK_CHECK"     -OutName "clockcheck"
Run-Suite -Name "ACTION"   -Var "STELQUICK_ACTION_CHECK"    -OutName "actioncheck"
Run-Suite -Name "SEARCH"   -Var "STELQUICK_SEARCH_CHECK"    -OutName "searchcheck"
Run-Suite -Name "LOCATE"   -Var "STELQUICK_LOCATE_CHECK"    -OutName "locatecheck"
Run-Suite -Name "LOCATEUI" -Var "STELQUICK_UI_CHECK"        -OutName "locate-uicheck"
Run-Suite -Name "TIME"     -Var "STELQUICK_TIME_CHECK"      -OutName "timecheck"
Run-Suite -Name "RETURNUI" -Var "STELQUICK_RETURN_UI_CHECK" -OutName "returnuicheck"
Run-Suite -Name "REPLAY"   -Var "STELQUICK_REPLAY_CHECK"    -OutName "replaycheck"
Run-Suite -Name "TIMEUI"   -Var "STELQUICK_TIME_UI_CHECK"   -OutName "timeuicheck"
Run-Suite -Name "LOCATION" -Var "STELQUICK_LOC_CHECK"       -OutName "locationcheck"
Run-Suite -Name "A2"       -Var "STELQUICK_A2_CHECK"        -OutName "a2-vulkan"

# ---- S3 legacy host -- actually RUN it, do not look at the md5 -------------
# The old argument for "the legacy host is unaffected" was "its md5 is
# byte-identical to last round". T33-D measured that md5 CHANGING with no T33
# source in its compile list (MSVC relink is not deterministic), so: run the
# 8-criteria S3 self-check instead -- stronger, and independent of linker
# behaviour.
$s3exe = Join-Path $Repo "build-win\src\Release\stellarium.exe"
if (Test-Path $s3exe) {
    $rc = Run-Suite -Name "S3" -Var "STELA3_CHECK" -OutName "s3-stela3-legacy" -ExePath $s3exe
    $so = Join-Path $dir "s3-stela3-legacy.out.txt"
    $txt = @(Read-Lines -File $so)
    $verd = [bool]($txt | Where-Object { $_ -like "*VERDICT=PASS*" } | Select-Object -First 1)
    $okS3 = ($rc -eq 0) -and $verd
    if (-not $okS3) { $script:bad++ }
    "  S3 legacy: rc=$rc verdictPASS=$verd -> " + $(if ($okS3) { "OK" } else { "BAD" }) |
        Out-File -Encoding ascii -Append $sum
} else {
    "  S3 legacy: SKIP (not built: $s3exe)" | Out-File -Encoding ascii -Append $sum
}
}

# ==========================================================================
# 2. T34 positive: TOOLBARCHECK x PosRuns (12 criteria)
#    rc=0 + "12/12" + VERDICT=PASS + no red + TB-09 self-evidence ("12/12
#    buttons found") + the landing-coverage line reading covered=true.
#    The coverage line is the INSTRUMENT'S OWN geometric evidence: without it a
#    12/12 could be a toolbar that was never actually clicked.
# ==========================================================================
if (-not $ProbeOnly -and -not $DynOnly -and -not $NegOnly) {
$tbPass = 0; $tbSkip = 0; $tbBad = 0
for ($i = 1; $i -le $PosRuns; $i++) {
    $rc = Run-Suite -Name "TOOLBAR" -Var "STELQUICK_TOOL_CHECK" -OutName "toolbarcheck-run$i"
    $so = Join-Path $dir "toolbarcheck-run$i.out.txt"
    $reds  = Get-RedIds -File $so -Pattern $TB_RED
    $judgeTxt = Get-Judge  -File $so -TagName "TOOLBARCHECK"
    $found = Count-Match -File $so -Prefix "TOOLBARCHECK:" -Regex "TB-09.*12/12"
    $cov   = Count-Match -File $so -Prefix "TOOLBARCHECK:" -Regex "covered=(\*{2})?true"
    $kind = "BAD"
    if ($judgeTxt -eq "12/12|PASS" -and $rc -eq 0 -and $reds.Count -eq 0 -and $found -ge 1 -and $cov -ge 1) {
        $kind = "pos-pass"; $tbPass++
    } elseif ($rc -eq 6) {
        $kind = "env-skip"; $tbSkip++
    } else { $script:bad++; $tbBad++ }
    "  TOOLBAR run${i}: rc=$rc judge=$judgeTxt red=[$($reds -join ',')] tb09found=$found(>=1) covered=$cov(>=1) -> $kind" |
        Out-File -Encoding ascii -Append $sum
}
"  TOOLBAR summary: pos-pass=$tbPass env-skip=$tbSkip bad=$tbBad of $PosRuns" |
    Out-File -Encoding ascii -Append $sum
if ($tbPass -eq 0) {
    "FATAL TOOLBARCHECK never produced a full 12/12 run -- positive case not exercised" |
        Out-File -Encoding ascii -Append $sum
    $script:bad++
}
}

# ==========================================================================
# 3. T35 positive: TIMELINKCHECK x PosRuns (7 criteria)
#    rc=0 + "7/7" + VERDICT=PASS + no red + ZERO placeholder leaks (a Qt
#    QString::arg() written in printf style ("%.4f") is silently NOT replaced,
#    so the reading line would still hold the literal -- measured 2026-09-30)
#    + the restore line + the two independent recomputations.
#    The window-gate hit count is reported as a READING only.
# ==========================================================================
if (-not $ProbeOnly -and -not $DynOnly -and -not $NegOnly) {
$tlPass = 0; $tlSkip = 0; $tlBad = 0; $tlGate = 0
for ($i = 1; $i -le $PosRuns; $i++) {
    $rc = Run-Suite -Name "TIMELINK" -Var "STELQUICK_TIMELINK_CHECK" -OutName "timelinkcheck-run$i"
    $so = Join-Path $dir "timelinkcheck-run$i.out.txt"
    $reds    = Get-RedIds -File $so -Pattern $TL_RED
    $judgeTxt = Get-Judge  -File $so -TagName "TIMELINKCHECK"
    $leak    = Count-Match -File $so -Prefix "TIMELINKCHECK:" -Regex "(%[0-9]|%\.[0-9])"
    $restore = Count-Match -File $so -Prefix "TIMELINKCHECK:" -Regex "rate=[0-9.]+ scale=[0-9.]+$"
    $gate    = Count-Match -File $so -Prefix "TIMELINKCHECK:" -Regex "25%"
    $tlGate += $gate
    $r1 = Recheck-TL01 -File $so
    $r7 = Recheck-TL07 -File $so
    $ok1 = $r1 -match "\sOK$"
    $ok7 = $r7 -match "\sOK$"
    $kind = "BAD"
    if ($judgeTxt -eq "7/7|PASS" -and $rc -eq 0 -and $reds.Count -eq 0 -and $leak -eq 0 -and
        $restore -ge 1 -and $ok1 -and $ok7) {
        $kind = "pos-pass"; $tlPass++
    } elseif ($rc -eq 6) {
        $kind = "env-skip"; $tlSkip++
    } else { $script:bad++; $tlBad++ }
    "  TIMELINK run${i}: rc=$rc judge=$judgeTxt red=[$($reds -join ',')] leak=$leak(0) restore=$restore(>=1) gate=$gate recomputedTL01=$ok1 recomputedTL07=$ok7 -> $kind" |
        Out-File -Encoding ascii -Append $sum
    "    recheck TL-01: $r1" | Out-File -Encoding ascii -Append $sum
    "    recheck TL-07: $r7" | Out-File -Encoding ascii -Append $sum
}
"  TIMELINK summary: pos-pass=$tlPass env-skip=$tlSkip bad=$tlBad of $PosRuns ; window-gate hits=$tlGate (reading only)" |
    Out-File -Encoding ascii -Append $sum
if ($tlPass -eq 0) {
    "FATAL TIMELINKCHECK never produced a full 7/7 run -- positive case not exercised" |
        Out-File -Encoding ascii -Append $sum
    $script:bad++
}
}

# ==========================================================================
# 4. T34 negative controls -- FOUR groups, red sets pairwise distinct
#    A REV_OFF      -> [TB-07,TB-09,TB-12]  (Facade never subscribes)
#    B TOKEN_OFF    -> [TB-09,TB-12]        (binding does not read the token)
#    C CLICK_OFF    -> [TB-10]              (button onClicked never dispatches)
#    D LAYOUT_BREAK -> [TB-10] + covered=false (same red set as C, different
#                      EVIDENCE: C is "the call never happened", D is "the click
#                      landed nowhere")
#    D must NOT drag TB-12 down: the engine never moved, so "binding agrees with
#    the engine" still holds.
# ==========================================================================
if (-not $ProbeOnly -and -not $DynOnly) {
$tbNeg = @(
    @{ Id="A"; Sw="STELQUICK_TOOL_REV_OFF";     Judge="9/12";  Reds="TB-07,TB-09,TB-12"; Cov=$null },
    @{ Id="B"; Sw="STELQUICK_TOOL_TOKEN_OFF";   Judge="10/12"; Reds="TB-09,TB-12";       Cov=$null },
    @{ Id="C"; Sw="STELQUICK_TOOL_CLICK_OFF";   Judge="11/12"; Reds="TB-10";             Cov=$null },
    @{ Id="D"; Sw="STELQUICK_TOOL_LAYOUT_BREAK";Judge="11/12"; Reds="TB-10";             Cov=$false }
)
foreach ($g in $tbNeg) {
    for ($i = 1; $i -le $TbNegRuns; $i++) {
        $out = "toolbneg-" + $g.Id.ToLower() + "-run$i"
        $rc = Run-Suite -Name ("TOOLBAR-NEG" + $g.Id) -Var "STELQUICK_TOOL_CHECK" -OutName $out `
                        -Extra @{ $g.Sw = "1" }
        $so = Join-Path $dir "$out.out.txt"
        $reds  = Get-RedIds -File $so -Pattern $TB_RED
        $judgeTxt = Get-Judge  -File $so -TagName "TOOLBARCHECK"
        $join  = ($reds -join ",")
        $covNote = ""
        $covOk = $true
        if ($g.Cov -eq $false) {
            $covF = Count-Match -File $so -Prefix "TOOLBARCHECK:" -Regex "covered=(\*{2})?false"
            $covT = Count-Match -File $so -Prefix "TOOLBARCHECK:" -Regex "covered=(\*{2})?true"
            $covNote = " coveredFalse=$covF(>=1) coveredTrue=$covT(0)"
            $covOk = ($covF -ge 1) -and ($covT -eq 0)
        }
        $ok = ($rc -eq 10) -and ($judgeTxt -eq ($g.Judge + "|FAIL")) -and ($join -eq $g.Reds) -and $covOk
        if (-not $ok) { $script:bad++ }
        "  TOOLBAR-NEG-" + $g.Id + " run${i}: rc=$rc judge=$judgeTxt red=[$join] expect=[$($g.Reds)]$covNote -> " +
        $(if ($ok) { "OK" } else { "BAD" }) | Out-File -Encoding ascii -Append $sum
    }
}
}

# ==========================================================================
# 5. T35 negative controls -- THREE groups
#    A BREAK        -> [TL-01]              (frame pump feeds dt*0.5)
#    B RATE_IGNORED -> [TL-01,TL-02,TL-05]  (the link advances at a pinned 0.1
#                                            while the readings stay honest)
#    C FREEZE_LEAK  -> [TL-04]              (advances during the "frozen" window,
#                                            then writes the reading back to 0)
#    TL-04's acceptance band is [0.4,1.6] on the POST-freeze ratio: a real freeze
#    leaks -> ~2, a dead link -> ~0, and B's pinned rate lands at ~0.5 (measured
#    0.4990 vs 0.5002) which is why the lower bound is 0.4 and not 0.5.
# ==========================================================================
if (-not $ProbeOnly -and -not $DynOnly) {
$tlNeg = @(
    @{ Id="A"; Sw="STELQUICK_TIMELINK_BREAK";        Judge="6/7"; Reds="TL-01" },
    @{ Id="B"; Sw="STELQUICK_TIMELINK_RATE_IGNORED"; Judge="4/7"; Reds="TL-01,TL-02,TL-05" },
    @{ Id="C"; Sw="STELQUICK_TIMELINK_FREEZE_LEAK";  Judge="6/7"; Reds="TL-04" }
)
foreach ($g in $tlNeg) {
    for ($i = 1; $i -le $TlNegRuns; $i++) {
        $out = "timelinkneg-" + $g.Id.ToLower() + "-run$i"
        $rc = Run-Suite -Name ("TIMELINK-NEG" + $g.Id) -Var "STELQUICK_TIMELINK_CHECK" -OutName $out `
                        -Extra @{ $g.Sw = "1" }
        $so = Join-Path $dir "$out.out.txt"
        $reds  = Get-RedIds -File $so -Pattern $TL_RED
        $judgeTxt = Get-Judge  -File $so -TagName "TIMELINKCHECK"
        $join  = ($reds -join ",")
        $gate  = Count-Match -File $so -Prefix "TIMELINKCHECK:" -Regex "25%"
        $ok = ($rc -eq 10) -and ($judgeTxt -eq ($g.Judge + "|FAIL")) -and ($join -eq $g.Reds)
        if (-not $ok) { $script:bad++ }
        "  TIMELINK-NEG-" + $g.Id + " run${i}: rc=$rc judge=$judgeTxt red=[$join] expect=[$($g.Reds)] gate=$gate -> " +
        $(if ($ok) { "OK" } else { "BAD" }) | Out-File -Encoding ascii -Append $sum
    }
}
}

# ==========================================================================
# 6. probes -- readings only, never a PASS/FAIL for the product
# ==========================================================================
if (-not $DynOnly -and -not $NegOnly) {
$rc = Run-Suite -Name "TOOLBARPROBE" -Var "STELQUICK_TOOL_PROBE" -OutName "probe-toolbar"
$so = Join-Path $dir "probe-toolbar.out.txt"
$txt = @(Read-Lines -File $so)
$done = [bool]($txt | Where-Object { $_ -like "*TOOLBARPROBE: VERDICT=DONE*" } | Select-Object -First 1)
# The probe prints "<TAG>:" then a RUN of spaces then the Q-id -- measured: one
# space for TOOLBARPROBE, THREE for TIMELINKPROBE. Never hard-code one space;
# match "\s+" (this bit me in local validation: "TIMELINKPROBE: Q1 *" matched
# nothing and would have failed the whole batch on the remote box).
$q1 = @($txt | Where-Object { $_ -match "^TOOLBARPROBE:\s+Q1 " }).Count
$q2 = @($txt | Where-Object { $_ -match "^TOOLBARPROBE:\s+Q2 action" }).Count
$okP = ($rc -eq 0) -and $done -and ($q1 -ge 1) -and ($q2 -ge 12)
if (-not $okP) { $script:bad++ }
"  TOOLBARPROBE: rc=$rc verdictDONE=$done Q1lines=$q1(>=1) Q2candidates=$q2(>=12) -> " +
$(if ($okP) { "OK" } else { "BAD" }) | Out-File -Encoding ascii -Append $sum

$rc = Run-Suite -Name "TIMELINKPROBE" -Var "STELQUICK_TIMELINK_PROBE" -OutName "probe-timelink"
$so = Join-Path $dir "probe-timelink.out.txt"
$txt = @(Read-Lines -File $so)
$done = [bool]($txt | Where-Object { $_ -like "*TIMELINKPROBE: VERDICT=DONE*" } | Select-Object -First 1)
$leak = @($txt | Where-Object { $_ -match "^TIMELINKPROBE:" -and $_ -match "(%[0-9]|%\.[0-9])" }).Count
# Same seven anchors the mac script gates on: the probe must have PRINTED its
# reading for every question, or a "DONE" would be an empty run.
$qa = @{}
foreach ($q in @("Q1 ", "Q2 ", "Q3 ", "Q4 increaseTimeSpeed", "Q5 ", "Q6b ", "Q6c ")) {
    $qa[$q] = @($txt | Where-Object { $_ -match ("^TIMELINKPROBE:\s+" + [regex]::Escape($q)) }).Count
}
$miss = @($qa.Keys | Where-Object { $qa[$_] -lt 1 })
$okP = ($rc -eq 0) -and $done -and ($leak -eq 0) -and ($miss.Count -eq 0)
if (-not $okP) { $script:bad++ }
"  TIMELINKPROBE: rc=$rc verdictDONE=$done placeholderLeak=$leak(0) missingAnchors=[$($miss -join ',')] -> " +
$(if ($okP) { "OK" } else { "BAD" }) | Out-File -Encoding ascii -Append $sum
}

# ==========================================================================
# 7. INTERACTCHECK positive x PosRuns (18 criteria)
#    MainWindow.qml now mounts the real toolbar, so this suite is a genuine
#    neighbour of the change. Two acceptable outcomes, both recorded and NEITHER
#    washed into PASS:
#      rc=0 + "18/18" + act-note   -> full run, the window was active (pos-pass)
#      rc=6 + "UNAVAILABLE"        -> the activation gate failed (env-skip)
# ==========================================================================
if (-not $ProbeOnly -and -not $DynOnly -and -not $NegOnly) {
$ipass = 0; $iskip = 0
for ($i = 1; $i -le $PosRuns; $i++) {
    $rc = Run-Suite -Name "INTERACT" -Var "STELQUICK_INTERACT_UI_CHECK" `
                    -OutName "interactcheck-run$i" `
                    -Extra @{ "STELQUICK_INTERACT_REQUEST_ACTIVATE" = "1" }
    $so = Join-Path $dir "interactcheck-run$i.out.txt"
    $txt = @(Read-Lines -File $so)
    $crosses = Count-Cross -File $so -TagName "INTERACTCHECK"
    $armed = @($txt | Where-Object { $_ -like "*act-note*" }).Count
    $full  = [bool]($txt | Where-Object { $_ -like "*18/18*" } | Select-Object -First 1)
    $degr  = [bool]($txt | Where-Object { $_ -like "*UNAVAILABLE*" } | Select-Object -First 1)
    $kind = "BAD"
    if ($rc -eq 0 -and $full -and $crosses -eq 0 -and $armed -eq 1) { $kind = "pos-pass"; $ipass++ }
    elseif ($rc -eq 6 -and $degr -and $crosses -eq 0) { $kind = "env-skip"; $iskip++ }
    else { $script:bad++ }
    "  INTERACT run${i}: rc=$rc armed=$armed full18=$full degraded=$degr crosses=$crosses -> $kind" |
        Out-File -Encoding ascii -Append $sum
}
"  INTERACT summary: pos-pass=$ipass env-skip=$iskip of $PosRuns" |
    Out-File -Encoding ascii -Append $sum
if ($ipass -eq 0) {
    "FATAL INTERACTCHECK never completed a full 18/18 run -- positive case not exercised" |
        Out-File -Encoding ascii -Append $sum
    $script:bad++
}
}

# ---- INTERACT probe: the activation-dependency boundary -------------------
# THE BOUNDARY IS PLATFORM-SPECIFIC. W-T31/W-T32 measured Windows crosses
# IT-05/06/13/16/17/18 (macOS: IT-06/13/14/17/18) -- with the window inactive
# forceActiveFocus() cannot give keySink active focus here, so an injected key
# never reaches the QML chain. Set WT35_PROBE_EXPECT to the expected list to
# turn it into an assertion; left unset it is a READING (rc=10 is expected --
# there ARE cross lines: this is a probe).
if (-not $ProbeOnly -and -not $DynOnly -and -not $NegOnly) {
$rc = Run-Suite -Name "INTERACT-PROBE" -Var "STELQUICK_INTERACT_UI_CHECK" `
                -OutName "probe-interact-inactive" `
                -Extra @{ "STELQUICK_INTERACT_FORCE_INACTIVE" = "1";
                          "STELQUICK_INTERACT_PROBE_ACTIVATION" = "1" }
$so = Join-Path $dir "probe-interact-inactive.out.txt"
$txt = @(Read-Lines -File $so)
$ids = @()
foreach ($l in $txt) {
    if ($l -like ("INTERACTCHECK: " + $MARK_BAD + " IT-*")) {
        $ids += (($l -split " ")[2])
    }
}
$ids = @($ids | Sort-Object -Unique)
$expect = @($env:WT35_PROBE_EXPECT -split ',' | Where-Object { $_ -ne "" })
$okI = $true
if ($expect.Count -gt 0) {
    $okI = ($ids.Count -eq $expect.Count) -and (-not (Compare-Object $ids $expect))
} else {
    $expect = @("<not asserted>")
}
if (-not $okI) { $script:bad++ }
"  INTERACT-PROBE: rc=$rc crosses=[$($ids -join ',')] expect=[$($expect -join ',')] -> " +
$(if ($okI) { "OK" } else { "BAD" }) | Out-File -Encoding ascii -Append $sum
}

# ---- DYN: two-way discriminating control ---------------------------------
if (-not $NegOnly -and -not $ProbeOnly) {
foreach ($k in @("engine", "test")) {
    for ($i = 1; $i -le $DynRuns; $i++) {
        Run-Suite -Name "DYN-$k" -Var "STELQUICK_DYN_CHECK" `
                  -OutName "dyn-$k-run$i" -Producer $k
    }
}
}

"[done] " + (Get-Date -Format o) | Out-File -Encoding ascii -Append $sum
if ($script:mismatch -gt 0) {
    "FATAL producer read-back mismatch x$($script:mismatch)" |
        Out-File -Encoding ascii -Append $sum
    exit 98
}
if ($script:bad -gt 0) {
    "FATAL $($script:bad) expectation check(s) failed" |
        Out-File -Encoding ascii -Append $sum
    exit 97
}

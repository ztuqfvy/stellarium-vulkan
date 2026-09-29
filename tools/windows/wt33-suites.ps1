# wt33-suites.ps1 -- run the full self-check batch on Windows for T33.
#
# T33 = the observation-location page (the only "must" item of the A-alpha exit
# criteria that had never been implemented). New code: src/app/LocationProbe.*
# (probe), src/app/LocationCheck.* (criteria), the location write face in
# src/app/AppFacade.*, src/ui/qml/LocationPage.qml (+ MainWindow.qml mounts it),
# and two new phases in src/ui/main.cpp. None of that is built into the legacy
# host (src/app/* + src/ui/* belong to the stelQuickUI target only; the legacy
# host is src/main.cpp + stelMain), so it SHOULD be unaffected -- but its md5
# still changed this round (MSVC relink is not deterministic). Do not trust md5;
# section 8 below actually RUNS the legacy host self-check (S3).
#
# New suite: LOCATIONCHECK, 10 criteria (LC-01..LC-08; LC-03 and LC-04 each have
# a discriminating leg). The headline criterion is a physical identity:
# "altitude of the celestial pole == observer latitude", measured with the
# true equinox pole (NOT Polaris, which sits 0.74 deg off the pole and jitters
# by ~1.5 deg peak-to-peak -- it cannot resolve Paris<->Abbeville, 1.25 deg).
#
# Must be delivered through schtasks /it: every suite below creates a real
# QQuickWindow, and a GUI program started from an SSH session has no desktop
# session (the Qt scene graph never initializes).
#
#   schtasks /create /tn StelQC_t33w_suites /sc once /st 23:59 /it /f ^
#     /tr "powershell -NoProfile -ExecutionPolicy Bypass -File C:\temp\t33w-suites.ps1"
#   schtasks /run /tn StelQC_t33w_suites
#   schtasks /delete /tn StelQC_t33w_suites /f          <-- ALWAYS delete after
#
# Encoding: Start-Process -RedirectStandardOutput writes the raw byte stream to
#   disk -- no PowerShell transcoding, so the files hold exactly the UTF-8 bytes
#   the exe produced (compiled with MSVC /utf-8). Do NOT use `*> file` here.
#
# ASCII-only on purpose: PS 5.1 reads a BOM-less .ps1 using the ANSI codepage,
#   so a Chinese comment or a Chinese regex literal would be mangled into stray
#   quotes (PARSE-ERR). All matching below keys on ASCII anchors, or on the
#   check/cross prefixes built at runtime from their code points
#   ([char]0x2713 / [char]0x2717) so the FILE stays pure ASCII while the MATCH
#   still works. Every LC-xx id is ASCII, so no id ever appears as a literal.
param(
    [string]$Repo      = "E:\Qt_demo\stellarium-vulkan",
    [string]$QtDir     = "E:\Qt\6.11.2\msvc2022_64",
    [string]$VulkanBin = "E:\Vulkan\SDK\Bin",
    [string]$Tag       = "t33w",
    [int]$PosRuns      = 5,
    [int]$NegARuns     = 2,
    [int]$NegBRuns     = 2,
    [int]$NegRuns      = 3,
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
$MARK_OK  = [char]0x2713   # check mark prefix used by the criteria lines
$MARK_BAD = [char]0x2717   # cross mark prefix

# One cross line reads  "LOCATIONCHECK:   <cross> LC-04 <chinese text>"
# (id first, then the description). Anchor on the exact ASCII id token.
$LOCRED = "LOCATIONCHECK:\s+" + $MARK_BAD + "\s+(LC-[0-9]+[ab]?)"

# One OK line reads  "LOCATIONCHECK:   <check> LC-01 <chinese text>"
$LOCOK = "LOCATIONCHECK:\s+" + $MARK_OK + "\s+(LC-[0-9]+[ab]?)"

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
foreach ($rel in @("src\app\LocationCheck.hpp", "src\app\LocationCheck.cpp",
                   "src\app\LocationProbe.cpp", "src\app\AppFacade.cpp",
                   "src\ui\main.cpp", "src\ui\qml\LocationPage.qml",
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
# HISTORY (kept honest): this helper was born from a WRONG theory (redirect lag /
# flash race). The real cause of the W-T31 negative-control failures was a
# variable-name collision -- "$ok" overwriting "$OK". See the note at $MARK_OK.
#
# !!! DO NOT "return ,$lines" HERE. The comma idiom only works when the CALLER
#     assigns the result directly; every caller below wraps with @( ... ), and
#     @() does NOT flatten a nested array -- so ",$lines" plus "@( ... )" yields
#     a 1-element array whose single element IS the line array. $txt then has
#     Count 1 and every downstream Where-Object sees the whole array as $_
#     (an array -like <pattern> is truthy whenever ANY line matches), which
#     silently reports "1 line matched". Measured: unavailNamed=1(5) while the
#     files on disk were byte-identical to the fully-passing round.
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

# Collect the criterion ids that went red in a LOCATIONCHECK log (sorted, unique).
function Get-LocRed {
    param([string]$File)
    $ids = @()
    foreach ($l in @(Read-Lines -File $File)) {
        if ($l -match $LOCRED) { $ids += $Matches[1] }
    }
    return @($ids | Sort-Object -Unique)
}

# Collect the criterion ids that went green (used only for the LC-01 two-leg check).
function Get-LocOk {
    param([string]$File)
    $ids = @()
    foreach ($l in @(Read-Lines -File $File)) {
        if ($l -match $LOCOK) { $ids += $Matches[1] }
    }
    return @($ids | Sort-Object -Unique)
}

# The criteria-count line reads "LOCATIONCHECK: <chinese> 10/10". Key on the
# trailing "N/M" pair; that shape appears on no other LOCATIONCHECK line.
function Get-LocJudge {
    param([string]$File)
    foreach ($l in @(Read-Lines -File $File)) {
        if ($l -match "^LOCATIONCHECK:.*?(\d+)/(\d+)\s*$") { return "$($Matches[1])/$($Matches[2])" }
    }
    return ""
}

# Count criteria lines carrying the cross prefix (INTERACTCHECK uses the same
# mark, so this stays shared).
function Count-Cross {
    param([string]$File)
    $n = 0
    foreach ($l in @(Read-Lines -File $File)) {
        if ($l.StartsWith("INTERACTCHECK: $MARK_BAD")) { $n++ }
    }
    return $n
}

# ---- the plain regression suites, in dependency order --------------------
# NOTE the three switches must be honoured here too. In W-T31 "-NegOnly" was
# checked only by the probe/DYN blocks, so it silently ran the whole regression
# (measured 2026-09-29).
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
Run-Suite -Name "A2"       -Var "STELQUICK_A2_CHECK"        -OutName "a2-vulkan"

# ---- S3 legacy host -- actually RUN it, do not look at the md5 -------------
# Why this is here now: the old argument for "the legacy host is unaffected" was
# "its md5 is byte-identical to last round". This round that md5 CHANGED
# (66C51B61... -> F6E9CE85..., same size 27634176) while the build log shows no
# T33 source compiled into it -- src/app/* only goes into stelQuickUI
# (src/ui/CMakeLists.txt). The cause is that MSVC linking is NOT deterministic
# (it re-rolls the timestamp / PDB signature on every relink), and this round the
# _deps libs (ShowMySky and friends) were relinked, dragging the legacy host
# along. So: run the 8-criteria S3 self-check instead -- stronger, and it does
# not depend on linker behaviour.
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

# ---- T33 positive case: LOCATIONCHECK x PosRuns (10 criteria) -------------
#   rc=0 + "10/10" + VERDICT=PASS + 3 readiness-gate lines + 1 catalogue-lat
#     line                                                    -> pos-pass
#   rc=6 + UNAVAILABLE                                        -> env-skip
# Anything else is BAD. The gate/catalogue lines are the INSTRUMENT'S OWN
# evidence: without them a 10/10 could be an empty run. The catalogue-lat line
# is the only LOCATIONCHECK line holding both 48.8534 and 50.1052 (Paris and
# Abbeville latitudes straight out of the location file) -- ASCII anchor.
$posPass = 0
$envSkip = 0
for ($i = 1; $i -le $PosRuns; $i++) {
    $rc = Run-Suite -Name "LOC" -Var "STELQUICK_LOC_CHECK" -OutName "loccheck-run$i"
    $so = Join-Path $dir "loccheck-run$i.out.txt"
    $txt = @(Read-Lines -File $so)
    $reds = Get-LocRed -File $so
    $judge = Get-LocJudge -File $so
    $passV = [bool]($txt | Where-Object { $_ -like "*LOCATIONCHECK: VERDICT=PASS*" } | Select-Object -First 1)
    $degr  = [bool]($txt | Where-Object { $_ -like "*LOCATIONCHECK: VERDICT=UNAVAILABLE*" } | Select-Object -First 1)
    $gates = @($txt | Where-Object { $_ -like "*3000 ms*" }).Count
    $cat   = @($txt | Where-Object { $_ -like "*48.8534*" -and $_ -like "*50.1052*" }).Count
    # Premise self-evidence: the check must PRINT its `flagUseCTZ` state. THIS IS THE
    # BUG THIS LINE EXISTS FOR: this box's config.ini has
    # `localization/time_zone = Asia/Shanghai` -> StelCore sets flagUseCTZ=true
    # (StelCore.cpp:240-243) -> setObserver's timezone linkage is skipped BY DESIGN
    # (condition `!getUseCustomTimeZone()`) -> LC-03a goes red every run (dUTCOffset=0.00)
    # while LC-03b ("timezone UNCHANGED") goes green for the WRONG reason.
    # The check now forces it off and restores it; without this line the instrument
    # is the old one and a 10/10 would not mean anything.
    $ctz = @($txt | Where-Object { $_ -like "*flagUseCTZ*" }).Count
    $kind = "BAD"
    if ($rc -eq 0 -and $judge -eq "10/10" -and $reds.Count -eq 0 -and $passV -and $gates -eq 3 -and $cat -eq 1 -and $ctz -ge 1) {
        $kind = "pos-pass"; $posPass++
    } elseif ($rc -eq 6 -and $degr -and $reds.Count -eq 0) {
        $kind = "env-skip"; $envSkip++
    } else { $script:bad++ }
    "  LOC run${i}: rc=$rc  judge=$judge  red=[$($reds -join ',')]  gates=$gates(3)  catlat=$cat(1)  ctz=$ctz(>=1)  -> $kind" |
        Out-File -Encoding ascii -Append $sum
}
"  LOC summary: pos-pass=$posPass env-skip=$envSkip of $PosRuns" |
    Out-File -Encoding ascii -Append $sum
if ($posPass -eq 0) {
    "FATAL LOCATIONCHECK never produced a full 10/10 run -- positive case not exercised" |
        Out-File -Encoding ascii -Append $sum
    $script:bad++
}
}

# ---- T33 negative controls -------------------------------------------------
# FOUR switches, each must turn a DIFFERENT set of criteria red:
#   A NODELAY + GATE_OFF -> LC-04 / LC-04b   (reproduces the race we fixed)
#   B NODELAY only       -> nothing          (the readiness gate absorbs it)
#   C RANGE_GATE_OFF     -> LC-05 / LC-08    (gate CALL SITE removed; the pure
#                                             predicate is untouched, so LC-01
#                                             must stay GREEN -- that is the
#                                             proof the two legs are separate)
#   D WRITE_NOOP         -> LC-03a/03b/04/04b (write silently does nothing)
#
# !!! A IS PROBABILISTIC -- DO NOT DEMAND "red every run". The defect it
#     reproduces IS a race (the read step and the write step are separated by a
#     single event-loop turn), so the frame pump sometimes ticks first and the
#     run legitimately reads the NEW value and goes fully green. Three batches on
#     the SAME bit-identical macOS binary measured 5/5 red, 5/5 red and 3/5 red.
#     So: assert only (a) no red id outside {LC-04, LC-04b}, and (b) at least one
#     red run in the batch. Zero red runs = INCONCLUSIVE (neither washed into
#     PASS nor failed) -- a coin landing the other way is not a product defect.
#
# !!! B MUST NOT REQUIRE NON-ZERO GATE POLLS either. With the write delay removed
#     the pump can tick between the two steps, in which case the gate reads a
#     fresh value on its first look and correctly polls ZERO times (measured:
#     such a run still reports 10/10). Poll counts are INFORMATION.
if (-not $ProbeOnly -and -not $DynOnly) {
$aOk = 0; $aBad = 0; $aRed = 0
for ($i = 1; $i -le $NegARuns; $i++) {
    $rc = Run-Suite -Name "LOC-NEGA" -Var "STELQUICK_LOC_CHECK" -OutName "locneg-a-run$i" `
                    -Extra @{ "STELQUICK_LOC_NODELAY" = "1"; "STELQUICK_LOC_GATE_OFF" = "1" }
    $so = Join-Path $dir "locneg-a-run$i.out.txt"
    $reds = Get-LocRed -File $so
    $judge = Get-LocJudge -File $so
    $join = ($reds -join ',')
    if ($join -eq "LC-04,LC-04b") {
        "  LOC-NEG-A run${i}: rc=$rc judge=$judge red=[$join] -> red (race reproduced)" |
            Out-File -Encoding ascii -Append $sum
        $aRed++
    } elseif ($reds.Count -eq 0 -and $rc -eq 0 -and $judge -eq "10/10") {
        "  LOC-NEG-A run${i}: rc=$rc judge=$judge red=[] -> green (race did NOT reproduce; coin landed the other way)" |
            Out-File -Encoding ascii -Append $sum
    } else {
        "  LOC-NEG-A run${i}: rc=$rc judge=$judge red=[$join] -> BAD (unexpected red set)" |
            Out-File -Encoding ascii -Append $sum
        $aBad++
    }
}
if ($aBad -gt 0) {
    $script:bad++
    "  LOC-NEG-A: BAD -- $aBad run(s) had an unexpected red set" | Out-File -Encoding ascii -Append $sum
} elseif ($aRed -ge 1) {
    $aOk = 1
    "  LOC-NEG-A: OK -- race reproduced in $aRed/$NegARuns run(s), red set NEVER left {LC-04,LC-04b}" |
        Out-File -Encoding ascii -Append $sum
} else {
    "  LOC-NEG-A: INCONCLUSIVE -- race never reproduced in $NegARuns run(s); no discriminating evidence this batch (not washed into PASS, not failed)" |
        Out-File -Encoding ascii -Append $sum
}

for ($i = 1; $i -le $NegBRuns; $i++) {
    $rc = Run-Suite -Name "LOC-NEGB" -Var "STELQUICK_LOC_CHECK" -OutName "locneg-b-run$i" `
                    -Extra @{ "STELQUICK_LOC_NODELAY" = "1" }
    $so = Join-Path $dir "locneg-b-run$i.out.txt"
    $txt = @(Read-Lines -File $so)
    $reds = Get-LocRed -File $so
    $judge = Get-LocJudge -File $so
    # Gate poll counts are deliberately NOT part of the verdict; print them as
    # information only (see the header note: a run that polls 0 times is correct).
    $pollLines = @($txt | Where-Object { $_ -like "*3000 ms*" }).Count
    $ok = ($rc -eq 0) -and ($judge -eq "10/10") -and ($reds.Count -eq 0)
    if (-not $ok) { $script:bad++ }
    "  LOC-NEG-B run${i}: rc=$rc judge=$judge red=[$($reds -join ',')] gateLines=$pollLines -> " +
    $(if ($ok) { "OK (gate absorbed the race)" } else { "BAD" }) | Out-File -Encoding ascii -Append $sum
}

$rc = Run-Suite -Name "LOC-NEGC" -Var "STELQUICK_LOC_CHECK" -OutName "locneg-c" `
                -Extra @{ "STELQUICK_LOC_RANGE_GATE_OFF" = "1" }
$so = Join-Path $dir "locneg-c.out.txt"
$reds = Get-LocRed -File $so
$oks  = Get-LocOk  -File $so
$judge = Get-LocJudge -File $so
$lc01green = [bool]($oks | Where-Object { $_ -eq "LC-01" } | Select-Object -First 1)
$okC = ($rc -eq 10) -and ($judge -eq "8/10") -and (($reds -join ',') -eq "LC-05,LC-08") -and $lc01green
if (-not $okC) { $script:bad++ }
"  LOC-NEG-C: rc=$rc judge=$judge red=[$($reds -join ',')] LC01green=$lc01green -> " +
$(if ($okC) { "OK (gate call site removed, predicate still green => two legs are separate)" } else { "BAD" }) |
    Out-File -Encoding ascii -Append $sum

$rc = Run-Suite -Name "LOC-NEGD" -Var "STELQUICK_LOC_CHECK" -OutName "locneg-d" `
                -Extra @{ "STELQUICK_LOC_WRITE_NOOP" = "1" }
$so = Join-Path $dir "locneg-d.out.txt"
$reds = Get-LocRed -File $so
$judge = Get-LocJudge -File $so
$okD = ($rc -eq 10) -and ($judge -eq "6/10") -and (($reds -join ',') -eq "LC-03a,LC-03b,LC-04,LC-04b")
if (-not $okD) { $script:bad++ }
"  LOC-NEG-D: rc=$rc judge=$judge red=[$($reds -join ',')] -> " +
$(if ($okD) { "OK (live-engine legs are load-bearing)" } else { "BAD" }) |
    Out-File -Encoding ascii -Append $sum
}

# ---- T33 probe: the data face (readings only, never a PASS/FAIL) ----------
if (-not $DynOnly -and -not $NegOnly) {
$rc = Run-Suite -Name "LOCPROBE" -Var "STELQUICK_LOC_PROBE" -OutName "locprobe"
$so = Join-Path $dir "locprobe.out.txt"
$txt = @(Read-Lines -File $so)
$done = [bool]($txt | Where-Object { $_ -like "*LOCPROBE: VERDICT=DONE*" } | Select-Object -First 1)
$scale = @($txt | Where-Object { $_ -like "*33501*" }).Count
$okP = ($rc -eq 0) -and $done -and ($scale -ge 1)
if (-not $okP) { $script:bad++ }
"  LOCPROBE: rc=$rc verdictDONE=$done catalogScaleLines=$scale -> " +
$(if ($okP) { "OK" } else { "BAD" }) | Out-File -Encoding ascii -Append $sum
}

# ---- INTERACTCHECK positive x PosRuns (18 criteria) ----------------------
# MainWindow.qml gained the location page + navLocationButton, so this suite is
# a genuine neighbour of the change. Two acceptable outcomes, both recorded,
# NEITHER washed into PASS:
#   rc=0 + "18/18" + act-note  -> full run, window was active   (pos-pass)
#   rc=6 + "UNAVAILABLE"       -> gate failed, degradation ran  (env-skip)
if (-not $ProbeOnly -and -not $DynOnly -and -not $NegOnly) {
$ipass = 0
$iskip = 0
for ($i = 1; $i -le $PosRuns; $i++) {
    $rc = Run-Suite -Name "INTERACT" -Var "STELQUICK_INTERACT_UI_CHECK" `
                    -OutName "interactcheck-run$i" `
                    -Extra @{ "STELQUICK_INTERACT_REQUEST_ACTIVATE" = "1" }
    $so = Join-Path $dir "interactcheck-run$i.out.txt"
    $txt = @(Read-Lines -File $so)
    $crosses = Count-Cross -File $so
    $armed = @($txt | Where-Object { $_ -like "*act-note*" }).Count
    $full = [bool]($txt | Where-Object { $_ -like "*18/18*" } | Select-Object -First 1)
    $degr = [bool]($txt | Where-Object { $_ -like "*UNAVAILABLE*" } | Select-Object -First 1)
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
# never reaches the QML chain. Set T33_PROBE_EXPECT to the expected list.
# rc=10 here is EXPECTED (there are cross lines): this is a probe.
if (-not $ProbeOnly -and -not $DynOnly -and -not $NegOnly) {
$rc = Run-Suite -Name "INTERACT-PROBE" -Var "STELQUICK_INTERACT_UI_CHECK" `
                -OutName "probe-inactive" `
                -Extra @{ "STELQUICK_INTERACT_FORCE_INACTIVE" = "1";
                          "STELQUICK_INTERACT_PROBE_ACTIVATION" = "1" }
$so = Join-Path $dir "probe-inactive.out.txt"
$txt = @(Read-Lines -File $so)
$ids = @()
foreach ($l in $txt) {
    if ($l -like "INTERACTCHECK: $MARK_BAD IT-*") {
        $ids += (($l -split " ")[2])
    }
}
$ids = @($ids | Sort-Object -Unique)
$expect = @($env:T33_PROBE_EXPECT -split ',' | Where-Object { $_ -ne "" })
$okI = $true
if ($expect.Count -gt 0) {
    $okI = ($ids.Count -eq $expect.Count) -and (-not (Compare-Object $ids $expect))
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

# wt37-41-suites.ps1 -- Windows cross-platform verification for T37..T41 (+ Q-WIN) in ONE trip.
#
# WHY ONE TRIP: T33's W-run exposed a real judging-criteria defect (that box's
#   config.ini sets localization/time_zone=Asia/Shanghai -> StelCore sets
#   flagUseCTZ -> the timezone linkage in setObserver is skipped BY DESIGN ->
#   LC-03a went red on every run). T37..T41 all sit on per-platform surfaces
#   (.qsb shaders, config paths, font metrics, native key text, config key
#   counts) and carry exactly that class of risk. One trip amortises the cost.
#
# WHAT THIS COVERS
#   T37 night mode   STELQUICK_NIGHT_CHECK=1     NC-x  neg: NIGHT_EFFECT_OFF
#   T38 display      STELQUICK_DISPLAY_CHECK=1   DP-x  neg: DISPLAY_GATE_OFF, DISPLAY_FWD_OFF
#   T39 hi-dpi       STELQUICK_HIDPI_CHECK=1     HP-x  neg: (shares BOTH T38 switches --
#                                                        HiDpiCheck.hpp reuses them on purpose)
#   T40 shortcuts    STELQUICK_SHORTCUT_CHECK=1  SC-x  neg: SHORTCUT_WRITE_OFF, SHORTCUT_SAVE_OFF
#   T41 help         STELQUICK_HELP_CHECK=1      HC-x  neg: HELP_TAKEOVER_OFF, HELP_LICENSE_OFF
#   Q-WIN            STELQUICK_WINDOW_TEST=1     Q-WIN-01..04
#                    STELQUICK_AUTOTEST_SECONDS=5 Q-WIN-05
#
# Q-WIN-06 IS DELIBERATELY ABSENT HERE -- it is a MACOS-ONLY criterion.
#   applyMacOsVulkanWorkaround() in src/ui/main.cpp is entirely inside
#   #ifdef Q_OS_MACOS, so on Windows the three modes (none / transaction-off /
#   basic-loop) are all no-ops and the run measures nothing. Measured on mac
#   2026-10-01: with the Metal backend ALL THREE modes PASS including `none`
#   (stall 52ms) -- i.e. the criterion only bites on the MoltenVK/Vulkan path.
#   Do not "restore" it here.
#
# EXPECTED (measured on mac 2026-10-01, binary md5 4cf585dd)
#   POS  NIGHTCHECK    rc=0  judge  6/6   VERDICT=PASS
#        DISPLAYCHECK  rc=0  judge 14/14  VERDICT=PASS
#        HIDPICHECK    rc=0  judge 12/12  VERDICT=PASS
#        SHORTCUTCHECK rc=0  judge 15/15  VERDICT=PASS  (15 ids, 14 criteria)
#        HELPCHECK     rc=0  judge 17/17  VERDICT=PASS
#   NEG  T37 EFFECT_OFF     rc=10 judge  5/6   red exactly {NC-03(2)}
#        T38 GATE_OFF       rc=10 judge 12/14  red exactly {DP-02,DP-04}
#        T38 FWD_OFF        rc=10 judge 13/14  red exactly {DP-07}
#        T39 GATE_OFF       rc=10 judge  9/12  red exactly {HP-02,HP-03,HP-04}
#        T39 FWD_OFF        rc=10 judge 10/12  red exactly {HP-07,HP-08}
#        T40 WRITE_OFF      rc=10 judge  8/15  red {SC-05a,SC-05b,SC-07,SC-08,SC-09,SC-11,SC-13}
#        T40 SAVE_OFF       rc=10 judge 12/15  red {SC-05b,SC-09,SC-11}
#        T41 TAKEOVER_OFF   rc=10 judge 14/17  red {HC-14,HC-15,HC-16}
#        T41 LICENSE_OFF    rc=10 judge 15/17  red {HC-04,HC-13}
#
# A DIFFERENT RED SET ON THIS BOX IS A FINDING, NEVER A FAILURE TO WASH GREEN.
#   The two legal interpretations: (a) a real platform defect in the product,
#   or (b) a criterion whose calibration is platform-bound. Both get written
#   down; neither gets retried until it looks right. A MISSING id (judge count
#   HIGHER than mac) is the dangerous direction -- it means the criterion
#   stopped biting -- and is flagged as FINDING-MISSING.
#
# Must be delivered through schtasks /it: every suite below creates a real
#   QQuickWindow, and a GUI program started from an SSH session has no desktop
#   session (the Qt scene graph never initializes).
#
#   schtasks /create /tn StelQC_t37w_suites /sc once /st 23:59 /it /f ^
#     /tr "powershell -NoProfile -ExecutionPolicy Bypass -File C:\temp\wt37-41-launch.ps1"
#   schtasks /run /tn StelQC_t37w_suites
#   schtasks /delete /tn StelQC_t37w_suites /f          <-- ALWAYS delete after
#   (then sweep: schtasks /query /fo csv | Select-String "StelQC")
#
# Encoding: Start-Process -RedirectStandardOutput writes the raw byte stream to
#   disk -- no PowerShell transcoding, so the files hold exactly the UTF-8 bytes
#   the exe produced (compiled with MSVC /utf-8). Do NOT use `*> file`.
#
# ASCII-only on purpose: PS 5.1 reads a BOM-less .ps1 using the ANSI codepage,
#   so a Chinese comment OR a Chinese regex literal is mangled into stray
#   quotes (PARSE-ERR). Every criterion id (NC-/DP-/HP-/SC-/HC-) is ASCII, and
#   the count word between "judge" and the digits is skipped with a generic
#   "\s+[^\s]+\s+" so no non-ASCII token is ever written here.
#
# !!! VARIABLE NAMES ARE CASE-INSENSITIVE IN POWERSHELL !!!
#   W-T31 lost a round to "$ok" overwriting "$OK" (the check-mark char), and
#   W-T35 lost another to "$judge" overwriting "$JUDGE" (the count token).
#   Hence the deliberately un-collidable names below: $MARK_OK, $reJudge,
#   $judgeTxt, $redTxt. Do not "simplify" them.

param(
    [string]$Repo      = "E:\Qt_demo\stellarium-vulkan",
    [string]$QtDir     = "E:\Qt\6.11.2\msvc2022_64",
    [string]$VulkanBin = "E:\Vulkan\SDK\Bin",
    [string]$Tag       = "t37w",
    [int]$PosRuns      = 2,
    [int]$SuiteTimeoutSec = 120,
    [switch]$PosOnly,
    [switch]$NegOnly,
    [switch]$QWinOnly
)

$ErrorActionPreference = "Continue"

# Console output encoding for THIS script's own Write-Host. NOTE: this does NOT
# affect the child's redirected output -- Start-Process ignores it (measured
# 2026-10-01: every Chinese char still came back as '?'). Child capture is
# handled explicitly by Invoke-Captured's StandardOutputEncoding.
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$OutputEncoding = [System.Text.Encoding]::UTF8

$script:bad      = 0
$script:findings = 0

$MARK_OK  = [char]0x2713
$MARK_BAD = [char]0x2717

$exe = Join-Path $Repo "build-win\src\ui\Release\stelQuickUI.exe"
$dir = "C:\temp\$Tag-suites"
New-Item -ItemType Directory -Force -Path $dir | Out-Null
$sum = Join-Path $dir "SUMMARY.txt"

"[$Tag] suites start " + (Get-Date -Format o) | Out-File -Encoding ascii $sum
"repo HEAD = " + (& git -C $Repo rev-parse --short HEAD) | Out-File -Encoding ascii -Append $sum
"script md5 = " + (Get-FileHash $MyInvocation.MyCommand.Path -Algorithm MD5).Hash.ToLower() |
    Out-File -Encoding ascii -Append $sum
"THIS BOX'S repo HEAD CAN LIE (no github reachability -> tree updated by scp)." |
    Out-File -Encoding ascii -Append $sum
"Trust the SRC md5 lines below, not the HEAD above." |
    Out-File -Encoding ascii -Append $sum

foreach ($rel in @("src\app\NightModeCheck.cpp",   "src\app\NightModeCheck.hpp",
                   "src\app\NightModeProbe.cpp",   "src\app\NightModeProbe.hpp",
                   "src\ui\shaders\nightmode.frag",
                   "src\app\DisplayCheck.cpp",    "src\app\DisplayCheck.hpp",
                   "src\app\HiDpiCheck.cpp",      "src\app\HiDpiCheck.hpp",
                   "src\app\ShortcutCheck.cpp",   "src\app\ShortcutCheck.hpp",
                   "src\app\HelpCheck.cpp",       "src\app\HelpCheck.hpp",
                   "src\app\AppFacade.cpp",       "src\app\AppFacade.hpp",
                   "src\ui\quick\BackendInfo.cpp", "src\ui\quick\BackendInfo.hpp",
                   "src\ui\main.cpp",             "src\ui\LiveSkyRuntime.cpp",
                   "src\ui\qml\DisplayPage.qml",
                   "src\ui\qml\DiagnosticPage.qml", "src\ui\qml\ShortcutsPage.qml",
                   "src\ui\qml\HelpPage.qml",     "src\ui\qml\AboutPage.qml",
                   "src\ui\qml\Toolbar.qml",      "src\ui\qml\MainWindow.qml",
                   "src\ui\CMakeLists.txt")) {
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
    "exe size = $($f.Length) B   mtime = $($f.LastWriteTime.ToString('o'))" |
        Out-File -Encoding ascii -Append $sum
    "exe md5  = " + (Get-FileHash $exe -Algorithm MD5).Hash | Out-File -Encoding ascii -Append $sum
} else {
    "FATAL exe not found: $exe" | Out-File -Encoding ascii -Append $sum
    exit 99
}

$env:PATH = (Join-Path $QtDir "bin") + ";" + $VulkanBin + ";" + (Join-Path $Repo "util\spout2\x64") + ";" + $env:PATH
Set-Location (Split-Path $exe)

function Clear-StelEnv {
    Get-ChildItem Env: | Where-Object { $_.Name -like "STELQUICK_*" } | ForEach-Object {
        Remove-Item ("Env:" + $_.Name) -ErrorAction SilentlyContinue
    }
}
Clear-StelEnv

function Write-Sum { param([string]$Text) $Text | Out-File -Encoding ascii -Append $sum }

# Run $exe once with the CURRENT environment, under a watchdog, and return
# @{ rc; tmo; sec; out; err }.
#
# WHY NOT Start-Process (both reasons measured 2026-10-01 on this box):
#  (1) rc was EMPTY on every single run. Start-Process -PassThru only populates
#      the returned object's ExitCode when -Wait is ALSO given, and -Wait is
#      unusable here (see below) -- so every suite and every control read as BAD
#      while its stdout was in fact perfectly fine.
#  (2) redirection decoded the child's raw UTF-8 bytes through the console code
#      page (936/GBK), so every Chinese char in the archived log became '?'.
# An explicit UTF-8 StandardOutputEncoding fixes both. Reads are async so a
# chatty child can never dead-lock on a full pipe buffer.
function Invoke-Captured {
    param([int]$TimeoutSec)
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName               = $exe
    $psi.WorkingDirectory       = Split-Path $exe
    $psi.UseShellExecute        = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError  = $true
    $enc = New-Object System.Text.UTF8Encoding($false)
    $psi.StandardOutputEncoding = $enc
    $psi.StandardErrorEncoding  = $enc

    $t0 = Get-Date
    $p = [System.Diagnostics.Process]::Start($psi)
    $outTask = $p.StandardOutput.ReadToEndAsync()
    $errTask = $p.StandardError.ReadToEndAsync()

    # WATCHDOG -- Start-Process -Wait is NOT usable on this box: a control that
    # falls into the non-live path is a plain GUI app that NEVER exits, and
    # -Wait blocked the batch 13+ min on the first one. Over budget => kill and
    # report rc=124, the same semantics tools/qwin-check.sh run_gui uses on mac.
    $tmo = $false
    if (-not $p.WaitForExit($TimeoutSec * 1000)) {
        $tmo = $true
        try { $p.Kill() } catch {}
        $p.WaitForExit(10000) | Out-Null
    }
    # Drain the async readers. After a Kill they can fault -- guard both.
    $out = ""
    $err = ""
    try { $out = $outTask.GetAwaiter().GetResult() } catch {}
    try { $err = $errTask.GetAwaiter().GetResult() } catch {}
    $sec = [math]::Round(((Get-Date) - $t0).TotalSeconds, 1)
    $rc  = if ($tmo) { 124 } else { $p.ExitCode }
    return @{ rc = $rc; tmo = $tmo; sec = $sec; out = $out; err = $err }
}

# Run one suite. Mirrors wt35's Run-Suite: raw redirection, env wipe between
# runs, and absorb a leftover process before starting a new one (a stale
# process holding the Qt DLLs is this platform's equivalent of mac's dropped
# Metal device -- trap 50 family).
function Run-Suite {
    param(
        [string]$Name,
        [string]$Var,
        [hashtable]$Extra = @{},
        [switch]$ExpectTimeout
    )

    if ($Var) { Set-Item -Path ("Env:" + $Var) -Value "1" }
    foreach ($k in $Extra.Keys) { Set-Item -Path ("Env:" + $k) -Value $Extra[$k] }

    Get-Process stelQuickUI -ErrorAction SilentlyContinue | ForEach-Object {
        if (-not $_.WaitForExit(8000)) { try { $_.Kill() } catch {} }
    }

    $r = Invoke-Captured -TimeoutSec $SuiteTimeoutSec
    Clear-StelEnv

    $so = Join-Path $dir "$Name.out.txt"
    $se = Join-Path $dir "$Name.err.txt"
    $enc = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($so, $r.out, $enc)
    [System.IO.File]::WriteAllText($se, $r.err, $enc)

    $tag = if ($r.tmo) { "  TMO(>${SuiteTimeoutSec}s => KILLED, rc=124)" } else { "" }
    Write-Sum ("SUITE {0}  rc={1}  elapsed={2}s  var={3}{4}" -f $Name, $r.rc, $r.sec, $Var, $tag)
    if ($r.tmo) {
        Write-Sum "    TMO -- last 5 stdout lines (what it was doing when it hung):"
        ($r.out -split "`r?`n") | Select-Object -Last 5 | ForEach-Object { Write-Sum ("      " + $_) }
    }
    return $r.rc
}

# "judge N/M" -- the word between the check name and the digits is non-ASCII on
# purpose ("criteria"), so it is skipped generically rather than matched.
function Get-Judge {
    param([string]$File, [string]$CheckName)
    $reJudge = "^" + $CheckName + ":.*?([0-9]+)/([0-9]+)"
    foreach ($ln in (Get-Content $File -Encoding UTF8 -ErrorAction SilentlyContinue)) {
        if ($ln -match $reJudge) { return "$($Matches[1])/$($Matches[2])" }
    }
    return "-/-"
}

function Get-Verdict {
    param([string]$File, [string]$CheckName)
    $reV = "^" + $CheckName + ": VERDICT=([A-Za-z]+)"
    foreach ($ln in (Get-Content $File -Encoding UTF8 -ErrorAction SilentlyContinue)) {
        if ($ln -match $reV) { return $Matches[1] }
    }
    return "NONE"
}

# Red-id set, in first-appearance order, duplicates preserved (a criterion that
# appears twice is a two-part criterion -- collapsing it would hide part of it).
function Get-RedIds {
    param([string]$File, [string]$CheckName)
    $reRed = "^" + $CheckName + ":\s+\[FAIL\]\s+(\S+)"
    $ids = @()
    foreach ($ln in (Get-Content $File -Encoding UTF8 -ErrorAction SilentlyContinue)) {
        if ($ln -match $reRed) { $ids += $Matches[1] }
    }
    return $ids
}

function Compare-RedSet {
    param([string]$Name, [string[]]$Got, [string[]]$Want)
    $gU = @($Got  | Select-Object -Unique)
    $wU = @($Want | Select-Object -Unique)
    $missing = @($wU | Where-Object { $gU -notcontains $_ })
    $extra   = @($gU | Where-Object { $wU -notcontains $_ })
    if ($missing.Count -eq 0 -and $extra.Count -eq 0) {
        Write-Sum ("  REDSET-OK   {0}  {{{1}}}" -f $Name, ($gU -join ","))
        return
    }
    $script:findings++
    if ($missing.Count -gt 0) {
        Write-Sum ("  FINDING-MISSING {0}  expected-but-not-red {{{1}}}  <-- criterion stopped biting" -f $Name, ($missing -join ","))
    }
    if ($extra.Count -gt 0) {
        Write-Sum ("  FINDING-EXTRA   {0}  red-but-not-on-mac {{{1}}}  <-- platform-bound or real defect" -f $Name, ($extra -join ","))
    }
}

# ---- positive suites ------------------------------------------------------
$posSuites = @(
    @{ T="T37"; Name="nightcheck";    Check="NIGHTCHECK";    Var="STELQUICK_NIGHT_CHECK";    M=6  },
    @{ T="T38"; Name="displaycheck";  Check="DISPLAYCHECK";  Var="STELQUICK_DISPLAY_CHECK";  M=14 },
    @{ T="T39"; Name="hidpicheck";    Check="HIDPICHECK";    Var="STELQUICK_HIDPI_CHECK";    M=12 },
    @{ T="T40"; Name="shortcutcheck"; Check="SHORTCUTCHECK"; Var="STELQUICK_SHORTCUT_CHECK"; M=15 },
    @{ T="T41"; Name="helpcheck";     Check="HELPCHECK";     Var="STELQUICK_HELP_CHECK";     M=17 }
)

if (-not $NegOnly -and -not $QWinOnly) {
    Write-Sum "---- positive suites ----"
    foreach ($s in $posSuites) {
        for ($i = 1; $i -le $PosRuns; $i++) {
            $rc = Run-Suite -Name "$($s.Name)-pos-run$i" -Var $s.Var
            $f  = Join-Path $dir "$($s.Name)-pos-run$i.out.txt"
            $j  = Get-Judge   -File $f -CheckName $s.Check
            $v  = Get-Verdict -File $f -CheckName $s.Check
            # @() is load-bearing: a PowerShell function that returns @() hands
            # back $null, and a single-element array comes back unwrapped. The
            # wrapper normalises both so .Count is never "the length of a
            # string". (wt35 lost a round to the mirror-image of this.)
            $reds = @(Get-RedIds -File $f -CheckName $s.Check)
            $n = ($j -split "/")[0]
            $verdict = "BAD"
            if ($rc -eq 0 -and $v -eq "PASS" -and $n -eq "$($s.M)" -and $reds.Count -eq 0) {
                $verdict = "OK"
            } else {
                $script:bad++
            }
            Write-Sum ("  {0} run{1}: rc={2} judge={3} verdict={4} reds={5} -> {6}" -f `
                       $s.Check, $i, $rc, $j, $v, $reds.Count, $verdict)
            if ($verdict -ne "OK") {
                Write-Sum ("    red ids: {{{0}}}" -f ($reds -join ","))
                Write-Sum ("    expected: rc=0 judge=$($s.M)/$($s.M) VERDICT=PASS reds=0")
            }
        }
    }
}

# ---- negative controls ----------------------------------------------------
# Every negative control needs BOTH switches: Main (the suite's own switch, or
# the run never brings the engine up) AND Var (the "break it" switch).
# MEASURED 2026-10-01 -- the first version of this file set ONLY `Var`. Result:
# no LIVESKY line, no criteria, a plain GUI app that never exits (it fell into
# the non-live A2 path and sat in the event loop), and `Start-Process -Wait`
# blocked 13+ min per control. The whole negative-control section tested NOTHING.
$negs = @(
    @{ Name="nightcheck-effoff";   Check="NIGHTCHECK";    Main="STELQUICK_NIGHT_CHECK";    Var="STELQUICK_NIGHT_EFFECT_OFF";
       # The mac baseline red id is NC-03 followed by a CIRCLED DIGIT TWO (U+2461)
       # -- NC-03 is a two-part criterion and the product prints the part marker
       # as part of the id. Built from its code point because this file must stay
       # pure ASCII (see header). Writing plain "NC-03" here would report a
       # bogus FINDING-MISSING + FINDING-EXTRA pair on EVERY run.
       Judge="5/6";   Red=@("NC-03" + [char]0x2461) },
    @{ Name="displaycheck-gateoff"; Check="DISPLAYCHECK"; Main="STELQUICK_DISPLAY_CHECK";  Var="STELQUICK_DISPLAY_GATE_OFF";
       Judge="12/14"; Red=@("DP-02","DP-04") },
    @{ Name="displaycheck-fwdoff";  Check="DISPLAYCHECK"; Main="STELQUICK_DISPLAY_CHECK";  Var="STELQUICK_DISPLAY_FWD_OFF";
       Judge="13/14"; Red=@("DP-07") },
    @{ Name="hidpicheck-gateoff";   Check="HIDPICHECK";    Main="STELQUICK_HIDPI_CHECK";    Var="STELQUICK_DISPLAY_GATE_OFF";
       Judge="9/12";  Red=@("HP-02","HP-03","HP-04") },
    @{ Name="hidpicheck-fwdoff";    Check="HIDPICHECK";    Main="STELQUICK_HIDPI_CHECK";    Var="STELQUICK_DISPLAY_FWD_OFF";
       Judge="10/12"; Red=@("HP-07","HP-08") },
    @{ Name="shortcutcheck-writeoff"; Check="SHORTCUTCHECK"; Main="STELQUICK_SHORTCUT_CHECK"; Var="STELQUICK_SHORTCUT_WRITE_OFF";
       Judge="8/15";  Red=@("SC-05a","SC-05b","SC-07","SC-08","SC-09","SC-11","SC-13") },
    @{ Name="shortcutcheck-saveoff";  Check="SHORTCUTCHECK"; Main="STELQUICK_SHORTCUT_CHECK"; Var="STELQUICK_SHORTCUT_SAVE_OFF";
       Judge="12/15"; Red=@("SC-05b","SC-09","SC-11") },
    @{ Name="helpcheck-takeoveroff";  Check="HELPCHECK";     Main="STELQUICK_HELP_CHECK";     Var="STELQUICK_HELP_TAKEOVER_OFF";
       Judge="14/17"; Red=@("HC-14","HC-15","HC-16") },
    @{ Name="helpcheck-licenseoff";   Check="HELPCHECK";     Main="STELQUICK_HELP_CHECK";     Var="STELQUICK_HELP_LICENSE_OFF";
       Judge="15/17"; Red=@("HC-04","HC-13") }
)

if (-not $PosOnly -and -not $QWinOnly) {
    Write-Sum ""
    Write-Sum "---- negative controls (mac baseline; a different red set is a FINDING) ----"
    foreach ($n in $negs) {
        $rc = Run-Suite -Name "neg-$($n.Name)" -Var $n.Main -Extra @{ $n.Var = "1" }
        $f  = Join-Path $dir "neg-$($n.Name).out.txt"
        $j  = Get-Judge   -File $f -CheckName $n.Check
        $v  = Get-Verdict -File $f -CheckName $n.Check
        $reds = @(Get-RedIds -File $f -CheckName $n.Check)
        # INSTRUMENT SELF-PROOF: a control that never brought the engine up would
        # execute ZERO criteria and would "not go red" for entirely the wrong
        # reason. Require the LIVESKY marker before trusting any line below.
        # (See the $negs header -- this exact failure happened on 2026-10-01.)
        $live = @(Get-Content $f -Encoding UTF8 -ErrorAction SilentlyContinue |
                  Select-String -Pattern "^LIVESKY:")
        Write-Sum ("  {0}: rc={1} judge={2} verdict={3} reds={4}  [mac: rc=10 judge={5} verdict=FAIL] live={6}" -f `
                   $n.Name, $rc, $j, $v, $reds.Count, $n.Judge, $live.Count)
        if ($live.Count -eq 0) {
            $script:bad++
            Write-Sum "    BAD: no LIVESKY line -- the engine never came up, so this control tested NOTHING"
        }
        Write-Sum ("    red ids: {{{0}}}" -f ($reds -join ","))
        if ($rc -eq 124) {
            $script:bad++
            Write-Sum "    BAD(TMO): never exited -- killed by the watchdog. This is NOT a clean red; it is a platform FINDING (see TMO tail above)."
        } elseif ($rc -ne 10 -or $v -ne "FAIL") {
            $script:bad++
            Write-Sum "    BAD: the negative control did not go red at all -- the criterion is not biting here"
        }
        Compare-RedSet -Name $n.Name -Got $reds -Want $n.Red
    }
}

# ---- Q-WIN ----------------------------------------------------------------
# Q-WIN-01..04: one run of STELQUICK_WINDOW_TEST=1 prints the three metrics and
#   their shared 250ms verdict. The four ids are NOT separable in the product's
#   current output (stall/resize/gap are whole-run aggregates, not per-phase) --
#   reported as one line with the three readings, never faked into four.
# Q-WIN-05: STELQUICK_AUTOTEST_SECONDS=5 -> wall clock ~5s, extra overhead <=1s.
# Q-WIN-06: NOT HERE. macOS-only (see header).
if (-not $PosOnly -and -not $NegOnly) {
    Write-Sum ""
    Write-Sum "---- Q-WIN-01..05 (window interaction regression, test doc 6.4) ----"

    $rc = Run-Suite -Name "qwin-windowtest" -Var "STELQUICK_WINDOW_TEST"
    $f  = Join-Path $dir "qwin-windowtest.out.txt"
    $line = (Get-Content $f -Encoding UTF8 -ErrorAction SilentlyContinue |
             Select-String -Pattern "^WINDOWTEST:.*maxStallMs=.*VERDICT=" |
             Select-Object -First 1).Line
    Write-Sum ("  qwin-windowtest rc={0}" -f $rc)
    if ($line) {
        $msg = "    " + $line.Trim()
        Write-Sum $msg
        $m1 = [regex]::Match($line, "maxStallMs=([0-9]+)")
        $m2 = [regex]::Match($line, "maxResizeMs=([0-9]+)")
        $m3 = [regex]::Match($line, "maxFrameGapMs=([0-9]+)")
        # ASCII-only anchor: the real line reads
        #   "... maxFrameGapMs=61 <CN>250ms VERDICT=PASS"
        # so "(digits)ms VERDICT=" pins the threshold without naming the
        # Chinese word, and Groups[1] is still the threshold.
        $m4 = [regex]::Match($line, "([0-9]+)ms VERDICT=([A-Z]+)")
        Write-Sum ("    Q-WIN-01/02 maxStallMs={0} maxResizeMs={1}  (threshold {2}ms)" -f $m1.Groups[1].Value, $m2.Groups[1].Value, $m4.Groups[1].Value)
        Write-Sum ("    Q-WIN-04 maxFrameGapMs={0}  (threshold {1}ms)" -f $m3.Groups[1].Value, $m4.Groups[1].Value)
        if ($rc -ne 0) {
            $script:bad++
            Write-Sum "    BAD: WINDOWTEST did not return 0"
        }
        # phase lines prove all three phases actually ran (Q-WIN-03 needs phase 3)
        # Phase-completion lines are "WINDOWTEST: <CN word> 1 <CN word> ...".
        # Matched by ASCII shape (name, non-space token, the digit, non-space)
        # so this file stays pure ASCII -- see the header note.
        foreach ($phDigit in @("1", "2")) {
            $rePh = "^WINDOWTEST:\s+\S+\s+" + $phDigit + "\s"
            if (-not (Get-Content $f -Encoding UTF8 | Select-String -Pattern $rePh)) {
                $script:bad++
                Write-Sum ("    BAD: missing phase {0} completion line" -f $phDigit)
            }
        }
    } else {
        $script:bad++
        Write-Sum "    BAD: no WINDOWTEST result line -- the suite did not complete"
    }

    # Q-WIN-05 wall clock. Measured on mac: 5.37s. Allow 5..7s (the 1s overhead
    # budget plus generous startup slack; the assertion is "auto-exit works",
    # not "this box is fast").
    $q5  = Join-Path $dir "qwin-autotest.out.txt"
    $q5e = Join-Path $dir "qwin-autotest.err.txt"
    Clear-StelEnv
    Set-Item -Path "Env:STELQUICK_AUTOTEST_SECONDS" -Value "5"
    # Same capture path as the suites -- one implementation, one set of bugs.
    $r5 = Invoke-Captured -TimeoutSec 60
    Clear-StelEnv
    $enc5 = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($q5,  $r5.out, $enc5)
    [System.IO.File]::WriteAllText($q5e, $r5.err, $enc5)
    $tag5 = if ($r5.tmo) { "  TMO(>60s => KILLED)" } else { "" }
    Write-Sum ("  qwin-autotest rc={0} wall={1}s  (mac: 5.37s, budget 5..7s){2}" -f $r5.rc, $r5.sec, $tag5)
    if ($r5.rc -ne 0 -or $r5.sec -lt 5 -or $r5.sec -gt 7) {
        $script:bad++
        Write-Sum ("    BAD: Q-WIN-05 out of budget (rc={0} wall={1}s)" -f $r5.rc, $r5.sec)
    }
}

Write-Sum ""
Write-Sum ("VERDICT: bad={0} findings={1}" -f $script:bad, $script:findings)
if ($script:findings -gt 0) {
    Write-Sum "NOTE: findings are cross-platform red-set differences -- REPORT them, do not retry them away."
}
Write-Sum "done " + (Get-Date -Format o)

Get-Content $sum | Write-Host
if ($script:bad -gt 0) { exit 8 }
exit 0

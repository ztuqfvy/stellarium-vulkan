# wt31-suites.ps1 -- run the full self-check batch on Windows for T31, including
# the new INTERACTCHECK environment-gate degradation and its two probes.
#
# Why this file exists: T31 changes how INTERACTCHECK reacts when the activation
# gate fails. Old behaviour: record IT-06 FAIL and exit(10) immediately -- the
# remaining 9 criteria never ran and the report looked like a *product* defect.
# New behaviour: record the affected criteria as UNAVAILABLE (a third verdict,
# distinct from PASS and FAIL), keep running every criterion that does not need
# window activation, and exit(6). src/ui/main.cpp is the host layer, so per the
# W-branch rule the whole batch is re-run here on native Vulkan.
#
# Must be delivered through schtasks /it: every suite below creates a real
# QQuickWindow, and a GUI program started from an SSH session has no desktop
# session (the Qt scene graph never initializes). Such a program is also NOT the
# foreground window -- which is exactly the situation T31 handles gracefully.
#
#   schtasks /create /tn StelQC_t31w_suites /sc once /st 23:59 /it /f ^
#     /tr "powershell -NoProfile -ExecutionPolicy Bypass -File C:\temp\t31w-suites.ps1"
#   schtasks /run /tn StelQC_t31w_suites
#
# Encoding: Start-Process -RedirectStandardOutput writes the raw byte stream to
#   disk -- no PowerShell transcoding, so the files hold exactly the UTF-8 bytes
#   the exe produced (compiled with MSVC /utf-8). Do NOT use `*> file` here.
#
# ASCII-only on purpose: PS 5.1 reads a BOM-less .ps1 using the ANSI codepage,
#   so a Chinese comment or a Chinese regex literal would be mangled into stray
#   quotes (PARSE-ERR). All matching below keys on ASCII anchors. Where a symbol
#   is unavoidable (the check/cross prefixes in the criteria lines) it is built
#   at runtime from its code point -- [char]0x2713 (check) / [char]0x2717 (cross)
#   -- so the FILE stays pure ASCII while the MATCH still works.
param(
    [string]$Repo        = "E:\Qt_demo\stellarium-vulkan",
    [string]$QtDir       = "E:\Qt\6.11.2\msvc2022_64",
    [string]$VulkanBin   = "E:\Vulkan\SDK\Bin",
    [string]$Tag         = "t31w",
    [int]$PosRuns        = 5,
    [int]$NegRuns        = 3,
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
#     silently OVERWRITES the check-mark character "$OK". Measured 2026-09-29:
#     the negative-control block computed "$integrity" correctly on iteration 1,
#     then set "$ok = $true"; from iteration 2 on the pattern had become
#     "INTERACTCHECK: True INTERACT-INTEGRITY*" and matched nothing. So runs
#     2..N reported integrityOK=0 while every log file on disk was byte-identical
#     to run 1 -> it read exactly like a product regression and burned a full
#     investigation (file-lag / flash-race / stale-artifact theories all died).
#     The underscore is the whole fix: "$MARK_OK" cannot collide with "$pass".
#     Same trap if a future "$bad" ever appears next to "$MARK_BAD".
$MARK_OK  = [char]0x2713   # check mark prefix used by the criteria lines
$MARK_BAD = [char]0x2717   # cross mark prefix

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
        [string]$Var,
        [string]$OutName = $Name,
        [string]$Producer = "",
        [hashtable]$Extra = @{}
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

# Read a suite's output file once it has SETTLED. Start-Process -Wait returns
# when the process exits, and the redirect writer could in principle still be
# flushing for a few milliseconds after that -- so wait for two consecutive
# reads with an unchanged size. Bounded: if it never settles we hand back the
# last snapshot and the caller still judges it BAD, because a real defect must
# not be able to hide behind a wait.
#
# HISTORY -- this helper was born from a WRONG theory, keep the record honest:
# the T31 negative control first reported integrityOK=0 on 2 of 3 runs while the
# files on disk did contain the line. It was blamed on redirect lag, then on a
# "flash race": the round-1 fix anchored the retry on the LAST line
# ("*VERDICT=*") and did NOT help -- the failures stayed on exactly runs 2 and 3
# and the diag line showed the anchor present while a line *before* it was
# reported missing, which is impossible for a strictly sequential write.
# The actual cause was a variable-name collision: "$ok" (per-run verdict)
# overwriting "$OK" (the check mark). See the note at $MARK_OK. So treat the
# settle-wait below as cheap insurance, NOT as the fix for those failures, and
# do not resurrect the file-lag theory without fresh evidence.
#
# !!! DO NOT "return ,$lines" HERE. That idiom (comma = wrap in a 1-element
#     array so the pipeline does not unroll the collection) is only correct when
#     the CALLER assigns the result directly. Every caller below wraps with
#     @( ... ), and @() does NOT flatten a nested array -- so ",$lines" plus
#     "@( ... )" yields a 1-element array whose single element IS the array of
#     lines. $txt then has Count 1 and every downstream Where-Object sees the
#     whole array as $_ (an array -like <pattern> is truthy whenever ANY line
#     matches), which silently reports "1 line matched" and turns the probe's
#     ID extraction into garbage. Measured 2026-09-29: with the comma the batch
#     printed unavailNamed=1(5) and crosses=[=] while the log files on disk were
#     BYTE-IDENTICAL to the fully-passing round -- i.e. the instrument lied.
#     Plain "return $lines" is right: with @() at the call site a 1-element
#     array unrolls to its element and is re-wrapped, 0 elements stays empty.
#     This one WAS a real bug and the comma removal measurably fixed it
#     (unavailNamed went 1(5) -> 5(5)).
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

# Count criteria lines carrying the cross prefix. The prefix is built from its
# code point so this file stays ASCII-only.
function Count-Cross {
    param([string]$File)
    $n = 0
    foreach ($l in (Get-Content $File -Encoding UTF8)) {
        if ($l.StartsWith("INTERACTCHECK: $MARK_BAD")) { $n++ }
    }
    return $n
}

# ---- the plain regression suites, in dependency order --------------------
# NOTE -NegOnly must be honoured here too. It used to be checked only by the
# probe and the DYN blocks, so "-NegOnly" silently ran the whole 10-suite
# regression plus the 5 positive INTERACT runs -- measured 2026-09-29 when a
# supposedly negctl-only diagnostic batch pushed the full SUMMARY over the top.
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

# ---- T31 positive case: INTERACTCHECK x PosRuns ---------------------------
# Two acceptable outcomes, both recorded, NEITHER washed into PASS:
#   rc=0 + "16/16" + act-note        -> full run, window was active      (pos-pass)
#   rc=6 + "12/12" + 5 UNAVAILABLE   -> gate failed, degradation worked  (env-skip)
# Anything else is BAD. At least one pos-pass is required: Windows delivers via
# schtasks /it, i.e. the process is NOT the foreground window, so the bounded
# activation gate has to win the focus -- if it never wins, T31's positive case
# was not actually exercised here. REQUEST_ACTIVATE is what gives it a chance
# (macOS measured 3/3 pos-pass with it, 0/5 without).
$posPass = 0
$envSkip = 0
for ($i = 1; $i -le $PosRuns; $i++) {
    # REQUEST_ACTIVATE makes the app raise itself to the foreground (three
    # requestActivate shots at 0.15/0.40/0.90s, see main.cpp). Without it the
    # bounded activation gate can never win the focus here, and the positive
    # case is never actually exercised.
    $rc = Run-Suite -Name "INTERACT" -Var "STELQUICK_INTERACT_UI_CHECK" `
                    -OutName "interactcheck-run$i" `
                    -Extra @{ "STELQUICK_INTERACT_REQUEST_ACTIVATE" = "1" }
    $so = Join-Path $dir "interactcheck-run$i.out.txt"
    $crosses = Count-Cross -File $so
    $txt = @(Read-Lines -File $so)
    $armed = @($txt | Where-Object { $_ -like "*act-note*" }).Count
    $full = [bool]($txt | Where-Object { $_ -like "*16/16*" } | Select-Object -First 1)
    $degr = [bool]($txt | Where-Object { $_ -like "*12/12*" } | Select-Object -First 1)
    $kind = "BAD"
    if ($rc -eq 0 -and $full -and $crosses -eq 0 -and $armed -eq 1) { $kind = "pos-pass"; $posPass++ }
    elseif ($rc -eq 6 -and $degr -and $crosses -eq 0) { $kind = "env-skip"; $envSkip++ }
    else { $script:bad++ }
    "  INTERACT run${i}: rc=$rc  armed=$armed  full16=$full  degraded12=$degr  crosses=$crosses  -> $kind" |
        Out-File -Encoding ascii -Append $sum
}
"  INTERACT summary: pos-pass=$posPass env-skip=$envSkip of $PosRuns" |
    Out-File -Encoding ascii -Append $sum
if ($posPass -eq 0) {
    "FATAL INTERACTCHECK never completed a full 16/16 run -- positive case not exercised" |
        Out-File -Encoding ascii -Append $sum
    $script:bad++
}
}

# ---- T31 negative control: force the gate to fail -------------------------
# STELQUICK_INTERACT_FORCE_FOCUSGATE_FAIL=1 sets the degradation flag at the
# same single point a *real* gate failure uses. Expectation:
#   rc=6, VERDICT=UNAVAILABLE, criteria "12/12", the five gated ids named one by
#   one (IT-06/13/14/15/16), INTERACT-INTEGRITY OK, IT-07..IT-12 actually ran,
#   and ZERO crosses anywhere.
# Discriminating power: under the OLD logic the log stops at IT-06, rc=10, and
# nothing after IT-06 exists at all.
if (-not $DynOnly -and -not $ProbeOnly) {
for ($i = 1; $i -le $NegRuns; $i++) {
    $rc = Run-Suite -Name "NEGCTL" -Var "STELQUICK_INTERACT_UI_CHECK" `
                    -OutName "negctl-run$i" `
                    -Extra @{ "STELQUICK_INTERACT_FORCE_FOCUSGATE_FAIL" = "1" }
    $so = Join-Path $dir "negctl-run$i.out.txt"
    $txt = @(Read-Lines -File $so)
    $crosses = Count-Cross -File $so
    # Diagnostic: the round-1/2 failures claimed integrityOK=0 while the anchor
    # line and the lines BEFORE it were all visible, which is self-contradictory
    # for a strictly sequential write. Print what the parser actually saw so the
    # next batch can settle it with data instead of theories.
    $diagLen = -1
    if (Test-Path $so) { $diagLen = (Get-Item $so).Length }
    "  NEGCTL-DIAG run${i}: attempts=$script:rlAttempts bytes=$diagLen " +
    "txtCount=$($txt.Count) plainIntegrity=$(@($txt | Where-Object { $_ -like '*INTERACT-INTEGRITY*' }).Count) " +
    "verdictLines=$(@($txt | Where-Object { $_ -like '*VERDICT=*' }).Count)" |
        Out-File -Encoding ascii -Append $sum
    # The emitted line reads  "INTERACTCHECK: <circled-slash> IT-06 UNAVAILABLE:<why>"
    # -- id FIRST, then the keyword. Anchor on that exact ASCII order; anchoring
    # on "UNAVAILABLE: IT-06" matches nothing (and silently yields 0).
    $unavail = @($txt | Where-Object { $_ -like "*IT-06 UNAVAILABLE*" -or
                                       $_ -like "*IT-13 UNAVAILABLE*" -or
                                       $_ -like "*IT-14 UNAVAILABLE*" -or
                                       $_ -like "*IT-15 UNAVAILABLE*" -or
                                       $_ -like "*IT-16 UNAVAILABLE*" }).Count
    $integrity = @($txt | Where-Object { $_ -like "INTERACTCHECK: $MARK_OK INTERACT-INTEGRITY*" }).Count
    $ran = 0
    foreach ($id in @("IT-07","IT-08","IT-09","IT-10","IT-11","IT-12")) {
        if (@($txt | Where-Object { $_ -like "* $id *" }).Count -gt 0) { $ran++ }
    }
    $judge  = [bool]($txt | Where-Object { $_ -like "*12/12*" } | Select-Object -First 1)
    $verdic = [bool]($txt | Where-Object { $_ -like "*VERDICT=UNAVAILABLE*" } | Select-Object -First 1)
    $pass = ($rc -eq 6) -and $judge -and $verdic -and ($unavail -eq 5) -and
          ($integrity -eq 1) -and ($ran -eq 6) -and ($crosses -eq 0)
    if (-not $pass) { $script:bad++ }
    "  NEGCTL run${i}: rc=$rc  judge12=$judge  verdictUNAVAIL=$verdic  " +
    "unavailNamed=$unavail(5)  integrityOK=$integrity(1)  ranIT07to12=$ran(6)  " +
    "crosses=$crosses(0)  -> " + $(if ($pass) { "OK" } else { "BAD" }) |
        Out-File -Encoding ascii -Append $sum
}
}

# ---- T31 probe: the measured basis for the dependency table ---------------
# FORCE_INACTIVE + PROBE_ACTIVATION = real inactive window, gate failure but
# EVERY criterion still runs -> one pass yields the whole activation-dependency
# boundary.
#
# !!! THE BOUNDARY IS PLATFORM-SPECIFIC -- this is the whole point of running
#     the probe on Windows. Measured: macOS "13/16" with crosses IT-06/IT-13/
#     IT-14, but Windows "12/16" with crosses IT-05/IT-06/IT-13/IT-16.
#     IT-05 (and IT-16) are activation-DEPENDENT here although they are not on
#     macOS: with the window inactive, forceActiveFocus() cannot give keySink
#     active focus, so the injected key never reaches the QML chain
#     (dispatched=0, lastActionId=""). IT-14 stays green here for the same
#     reason it does on macOS -- its premise (focus inside the text field) is
#     absent, so it passes by accident. The shipped table is the UNION of both
#     platforms: {IT-05, IT-06, IT-13, IT-14, IT-15, IT-16}.
# rc=10 here is EXPECTED (there are cross lines); this is a probe, not a
# regression suite.
if (-not $DynOnly -and -not $NegOnly) {
$rc = Run-Suite -Name "PROBE" -Var "STELQUICK_INTERACT_UI_CHECK" `
                -OutName "probe-inactive" `
                -Extra @{ "STELQUICK_INTERACT_FORCE_INACTIVE" = "1";
                          "STELQUICK_INTERACT_PROBE_ACTIVATION" = "1" }
$so = Join-Path $dir "probe-inactive.out.txt"
$txt = @(Read-Lines -File $so)
$ids = @()
foreach ($l in $txt) {
    if ($l -like "INTERACTCHECK: $MARK_BAD IT-*") {
        # "INTERACTCHECK: <cross> IT-06 ..." -> token[0]=prefix, [1]=symbol, [2]=id
        $ids += (($l -split " ")[2])
    }
}
$ids = @($ids | Sort-Object -Unique)
$judge = [bool]($txt | Where-Object { $_ -like "*12/16*" } | Select-Object -First 1)
$expect = @("IT-05","IT-06","IT-13","IT-16")
$pass = $judge -and ($ids.Count -eq 4) -and (-not (Compare-Object $ids $expect))
if (-not $pass) { $script:bad++ }
"  PROBE: rc=$rc  judge12of16=$judge  crosses=[$($ids -join ',')] expect=[IT-05,IT-06,IT-13,IT-16]  -> " +
$(if ($pass) { "OK" } else { "BAD" }) | Out-File -Encoding ascii -Append $sum
}

# ---- DYN: two-way discriminating control ---------------------------------
if (-not $NegOnly -and -not $ProbeOnly) {
foreach ($k in @("engine", "test")) {
    for ($i = 1; $i -le 3; $i++) {
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
    "FATAL $($script:bad) expectation check(s) failed (INTERACT pos/neg/probe)" |
        Out-File -Encoding ascii -Append $sum
    exit 97
}

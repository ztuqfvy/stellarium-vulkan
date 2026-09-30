# defs.ps1 -- AUTO-EXTRACTED from tools/windows/wt35-suites.ps1 (do not edit by hand).
# Segment A: token definitions.  Segment B: pure parsers.  Anything that
# touches the remote box (Run-Suite) is included ONLY because the parsers
# call helpers defined beside it; it is never invoked here.
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

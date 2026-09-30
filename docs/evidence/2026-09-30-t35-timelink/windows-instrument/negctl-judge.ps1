# negctl-judge.ps1 -- NEGATIVE CONTROL for the REPEATED CALLS section of test.ps1.
#
# Purpose: prove that the "call#2 identical" style checks in test.ps1 are
# LOAD-BEARING -- i.e. that they would really have caught the instrument defect
# that broke the 2026-09-30 Windows batch. If this file reproduces the failure,
# the green result in test.ps1 carries information; if it does not, test.ps1 was
# decoration.
#
# Reconstruction: the INSTRUMENT AS IT WAS (code-point token + caller result
# variable whose name only differs in case). Nothing else is changed -- the input
# files, the regex intent and the two call sites are the same.
$ErrorActionPreference = "Stop"

$MARK_OK  = [char]0x2713
$MARK_BAD = [char]0x2717
$JUDGE    = [char]0x5224 + [char]0x636E        # the count word, as before

function Read-Lines {
    param([string]$File)
    $lines = @()
    foreach ($l in [System.IO.File]::ReadAllLines($File)) { $lines += $l }
    return $lines
}

function Get-JudgeBroken {
    param([string]$File, [string]$TagName)
    $jm = ""
    $vd = ""
    $reJ = "^" + $TagName + ":\s*" + $JUDGE + "\s*([0-9]+)/([0-9]+)(\s+VERDICT=([A-Z]+))?"
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

$EV  = "/Users/ztuqfvy/qt_demo/stellarium_vulkan/docs/evidence"
$f   = "$EV/2026-09-30-t34-toolbar/mac/toolbarcheck-mac-run1.txt"

# --- the caller exactly as the first revision had it: "$judge = ..." ----------
$judge = Get-JudgeBroken -File $f -TagName "TOOLBARCHECK"
$r1 = $judge
$judge = Get-JudgeBroken -File $f -TagName "TOOLBARCHECK"
$r2 = $judge
$judge = Get-JudgeBroken -File $f -TagName "TOOLBARCHECK"
$r3 = $judge

"negctl-judge: call#1 = [$r1]"
"negctl-judge: call#2 = [$r2]"
"negctl-judge: call#3 = [$r3]"

$script:caught = 0
$script:n = 0
function Expect-Broken {
    param([string]$Name, $Got, $Want)
    $script:n++
    if ("$Got" -ne "$Want") { "  DETECTED  $Name got=[$Got] want=[$Want]"; $script:caught++ }
    else { "  MISSED    $Name = [$Got] (the defect did not reproduce here)" }
}

# The instrument is broken iff a repeat call stops agreeing with the first.
Expect-Broken "call#2 differs from call#1" $r2 $r1
Expect-Broken "call#3 differs from call#1" $r3 $r1
# And the signature failure: empty captures, rendered as "/|PASS" (both $null).
Expect-Broken "call#3 is the empty-capture form" $r3 "12/12|PASS"

""
if ($script:caught -gt 0) {
    "NEGCTL OK -- the old instrument reproduces $($script:caught)/$($script:n) signatures."
    "=> test.ps1's REPEATED CALLS section is load-bearing."
    exit 0
} else {
    "NEGCTL FAILED -- the old instrument did NOT reproduce; the checks may be decoration."
    exit 1
}

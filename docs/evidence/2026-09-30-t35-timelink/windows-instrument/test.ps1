# test.ps1 -- run wt35-suites.ps1's PARSER logic against the REAL mac logs.
# Rationale: the parsers (Get-Judge / Get-RedIds / Count-Match / Recheck-*)
# encode every ASCII anchor the Windows batch depends on. If an anchor is wrong
# the batch reports a FALSE defect on the remote box, and iterating over a flaky
# SSH tunnel to find that out is expensive. So validate them here first.
$ErrorActionPreference = "Stop"
. "/tmp/pstest35/defs.ps1"

$EV = "/Users/ztuqfvy/qt_demo/stellarium_vulkan/docs/evidence"
$T34 = "$EV/2026-09-30-t34-toolbar/mac"
$T35 = "$EV/2026-09-30-t35-timelink/mac"

$script:fail = 0
$script:n = 0
function Check {
    param([string]$Name, $Got, $Want)
    $script:n++
    if ("$Got" -eq "$Want") {
        "  PASS  $Name = [$Got]"
    } else {
        $script:fail++
        "  FAIL  $Name got=[$Got] want=[$Want]"
    }
}
function CheckGe {
    param([string]$Name, $Got, [int]$Min)
    $script:n++
    if ([int]$Got -ge $Min) { "  PASS  $Name = [$Got] >= $Min" }
    else { $script:fail++; "  FAIL  $Name got=[$Got] want>=$Min" }
}

"=== T34 TOOLBARCHECK positive (5 mac runs) ==="
foreach ($i in 1..5) {
    $f = "$T34/toolbarcheck-mac-run$i.txt"
    Check   "run$i judge"   (Get-Judge -File $f -TagName "TOOLBARCHECK") "12/12|PASS"
    Check   "run$i reds"    ((Get-RedIds -File $f -Pattern $TB_RED) -join ",") ""
    CheckGe "run$i tb09"    (Count-Match -File $f -Prefix "TOOLBARCHECK:" -Regex "TB-09.*12/12") 1
    CheckGe "run$i cov"     (Count-Match -File $f -Prefix "TOOLBARCHECK:" -Regex "covered=(\*{2})?true") 1
}

"=== T34 negative controls ==="
$f = "$T34/negctl-A-rev-off.txt"
Check "A judge" (Get-Judge -File $f -TagName "TOOLBARCHECK") "9/12|FAIL"
Check "A reds"  ((Get-RedIds -File $f -Pattern $TB_RED) -join ",") "TB-07,TB-09,TB-12"
$f = "$T34/negctl-B-token-off.txt"
Check "B judge" (Get-Judge -File $f -TagName "TOOLBARCHECK") "10/12|FAIL"
Check "B reds"  ((Get-RedIds -File $f -Pattern $TB_RED) -join ",") "TB-09,TB-12"
$f = "$T34/negctl-C-click-off.txt"
Check "C judge" (Get-Judge -File $f -TagName "TOOLBARCHECK") "11/12|FAIL"
Check "C reds"  ((Get-RedIds -File $f -Pattern $TB_RED) -join ",") "TB-10"
$f = "$T34/negctl-D-layout-break.txt"
Check   "D judge"        (Get-Judge -File $f -TagName "TOOLBARCHECK") "11/12|FAIL"
Check   "D reds"         ((Get-RedIds -File $f -Pattern $TB_RED) -join ",") "TB-10"
CheckGe "D coveredFalse" (Count-Match -File $f -Prefix "TOOLBARCHECK:" -Regex "covered=(\*{2})?false") 1
Check   "D coveredTrue"  (Count-Match -File $f -Prefix "TOOLBARCHECK:" -Regex "covered=(\*{2})?true") 0

"=== T35 TIMELINKCHECK positive (5 mac runs) ==="
foreach ($i in 1..5) {
    $f = "$T35/timelinkcheck-mac-run$i.txt"
    Check   "run$i judge"   (Get-Judge -File $f -TagName "TIMELINKCHECK") "7/7|PASS"
    Check   "run$i reds"    ((Get-RedIds -File $f -Pattern $TL_RED) -join ",") ""
    Check   "run$i leak"    (Count-Match -File $f -Prefix "TIMELINKCHECK:" -Regex "(%[0-9]|%\.[0-9])") 0
    CheckGe "run$i restore" (Count-Match -File $f -Prefix "TIMELINKCHECK:" -Regex "rate=[0-9.]+ scale=[0-9.]+$") 1
    Check   "run$i gate"    (Count-Match -File $f -Prefix "TIMELINKCHECK:" -Regex "25%") 0
    $r1 = Recheck-TL01 -File $f
    $r7 = Recheck-TL07 -File $f
    "        TL01: $r1"
    "        TL07: $r7"
    Check   "run$i recheckTL01" ($r1 -match "\sOK$") $true
    Check   "run$i recheckTL07" ($r7 -match "\sOK$") $true
}

"=== T35 negative controls ==="
$f = "$T35/negctl-A-break.txt"
Check "A judge" (Get-Judge -File $f -TagName "TIMELINKCHECK") "6/7|FAIL"
Check "A reds"  ((Get-RedIds -File $f -Pattern $TL_RED) -join ",") "TL-01"
$f = "$T35/negctl-B-rate-ignored.txt"
Check "B judge" (Get-Judge -File $f -TagName "TIMELINKCHECK") "4/7|FAIL"
Check "B reds"  ((Get-RedIds -File $f -Pattern $TL_RED) -join ",") "TL-01,TL-02,TL-05"
$f = "$T35/negctl-C-freeze-leak.txt"
Check "C judge" (Get-Judge -File $f -TagName "TIMELINKCHECK") "6/7|FAIL"
Check "C reds"  ((Get-RedIds -File $f -Pattern $TL_RED) -join ",") "TL-04"

"=== neighbour suites ==="
$f = "$T35/regression-locationcheck.txt"
Check "LOC judge" (Get-Judge -File $f -TagName "LOCATIONCHECK") "10/10|PASS"
$f = "$T35/regression-interactcheck.txt"
Check "IT judge" (Get-Judge -File $f -TagName "INTERACTCHECK") "18/18|PASS"
Check "IT crosses" (Count-Cross -File $f -TagName "INTERACTCHECK") 0

"=== probes ==="
# These mirror the script's probe gates VERBATIM (including the "\s+" prefix
# match). Local validation caught a real bug here: TIMELINKPROBE prints THREE
# spaces after the colon, so a hard-coded single space matched nothing.
$f = "$T34/probe-tool-data-mac.txt"
$txt = @(Read-Lines -File $f)
Check   "TB probe done" (($txt | Where-Object { $_ -like "*TOOLBARPROBE: VERDICT=DONE*" } | Select-Object -First 1) -ne $null) $true
CheckGe "TB probe Q1"   (@($txt | Where-Object { $_ -match "^TOOLBARPROBE:\s+Q1 " }).Count) 1
CheckGe "TB probe Q2"   (@($txt | Where-Object { $_ -match "^TOOLBARPROBE:\s+Q2 action" }).Count) 12
$f = "$T35/probe-timelink-mac.txt"
$txt = @(Read-Lines -File $f)
Check   "TL probe done" (($txt | Where-Object { $_ -like "*TIMELINKPROBE: VERDICT=DONE*" } | Select-Object -First 1) -ne $null) $true
Check   "TL probe leak" (@($txt | Where-Object { $_ -match "^TIMELINKPROBE:" -and $_ -match "(%[0-9]|%\.[0-9])" }).Count) 0
foreach ($q in @("Q1 ", "Q2 ", "Q3 ", "Q4 increaseTimeSpeed", "Q5 ", "Q6b ", "Q6c ")) {
    CheckGe ("TL probe anchor " + $q.Trim()) (@($txt | Where-Object { $_ -match ("^TIMELINKPROBE:\s+" + [regex]::Escape($q)) }).Count) 1
}

"=== REPEATED CALLS -- regression for the $JUDGE / $judge collision ==="
# The broken revision returned the right answer on call #1 and EMPTY CAPTURES from
# call #2 on, because the caller's result variable ("$judge") overwrote the
# code-point token ("$JUDGE") that the matcher itself used -- PowerShell variable
# names are case-insensitive. On the real box that looked like: run1 12/12|PASS,
# run2 |PASS, run3 /|PASS, all three logged "-> BAD". These checks are the
# load-bearing ones: if they pass, the instrument cannot repeat that failure.
$f  = "$T34/toolbarcheck-mac-run1.txt"
$f7 = "$T35/timelinkcheck-mac-run1.txt"
$c1 = Get-Judge -File $f  -TagName "TOOLBARCHECK"
$c2 = Get-Judge -File $f  -TagName "TOOLBARCHECK"
$c3 = Get-Judge -File $f  -TagName "TOOLBARCHECK"
Check "call#1" $c1 "12/12|PASS"
Check "call#2 identical" $c2 $c1
Check "call#3 identical" $c3 $c1
# Interleave a SECOND tag between calls -- the real batch alternates TOOLBARCHECK
# and TIMELINKCHECK inside one script scope, which is when the overwrite bit.
$m1 = Get-Judge -File $f7 -TagName "TIMELINKCHECK"
$c4 = Get-Judge -File $f  -TagName "TOOLBARCHECK"
Check "call after other tag" $c4 "12/12|PASS"
Check "other tag intact"     $m1 "7/7|PASS"
# Direct proof the matcher no longer depends on ANY variable case-matching the
# caller's result name: poison a case-variant of the old token, then re-read.
$JuDgE = "POISON"
$judgeTxt = Get-Judge -File $f -TagName "TOOLBARCHECK"
Check "immune to JUDGE poisoning" $judgeTxt "12/12|PASS"
$JUDGE = "POISON-AGAIN"
$judgeTxt = Get-Judge -File $f -TagName "TOOLBARCHECK"
Check "immune, upper case too"    $judgeTxt "12/12|PASS"

"=== NEGATIVE INPUTS (must NOT be washed into PASS) ==="
# truncated log: the judge line is missing entirely
$bad = "/tmp/pstest35/truncated.txt"
Get-Content "$T35/timelinkcheck-mac-run1.txt" -TotalCount 20 | Set-Content $bad -Encoding utf8
Check "truncated judge" (Get-Judge -File $bad -TagName "TIMELINKCHECK") "|"
Check "truncated leaking through?" ($(Get-Judge -File $bad -TagName "TIMELINKCHECK") -eq "7/7|PASS") $false
# a log whose reading line still holds unreplaced printf placeholders
$leak = "/tmp/pstest35/leak.txt"
Get-Content "$T35/timelinkcheck-mac-run1.txt" | Set-Content $leak -Encoding utf8
Add-Content $leak 'TIMELINKCHECK:   OK TL-01 W=%.4fs dJD=%1 天' -Encoding utf8
Check "leak detected" (Count-Match -File $leak -Prefix "TIMELINKCHECK:" -Regex "(%[0-9]|%\.[0-9])") 1
Check "clean log leak" (Count-Match -File "$T35/timelinkcheck-mac-run1.txt" -Prefix "TIMELINKCHECK:" -Regex "(%[0-9]|%\.[0-9])") 0

""
"================================"
if ($script:fail -eq 0) { "ALL $($script:n) CHECKS PASSED" } else { "$($script:fail) of $($script:n) CHECKS FAILED" }
"================================"
if ($script:fail -gt 0) { exit 1 } else { exit 0 }

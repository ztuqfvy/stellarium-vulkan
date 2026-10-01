# judge-diag.ps1 -- WHY does Get-Judge return an empty count on this box?
#
# Measured facts so far: the Windows log DOES contain
#   "TOOLBARCHECK: " + <utf8 for the count word> + " 12/12  VERDICT=PASS" + CRLF
# (verified byte-by-byte on the mac: E5 88 A4 E6 8D AE), and the red-id matcher --
# which uses [char]0x2717 built the same way -- works fine here. So the suspect is
# the JUDGE string construction itself under Windows PowerShell 5.1.
#
# Rule being followed: when an instrument reports a negative on input that
# demonstrably contains the token, DO NOT invent a story about the producer --
# print what the PARSER actually sees. (T33 blood lesson.)
$ErrorActionPreference = "Continue"
$Log = "C:\temp\t35w-suites\toolbarcheck-run1.out.txt"

"=== ENGINE ==="
"  PSVersion = " + $PSVersionTable.PSVersion.ToString()
"  Edition   = " + $PSVersionTable.PSEdition

"=== JUDGE CONSTRUCTION CANDIDATES ==="
$J1 = [char]0x5224 + [char]0x636E
$J2 = [string]::Concat([char]0x5224, [char]0x636E)
$J3 = -join ([char]0x5224, [char]0x636E)
$J4 = [System.Text.Encoding]::UTF8.GetString([byte[]]@(0xE5,0x88,0xA4,0xE6,0x8D,0xAE))

$cands = New-Object System.Collections.ArrayList
[void]$cands.Add(@("A plus", $J1))
[void]$cands.Add(@("B Concat", $J2))
[void]$cands.Add(@("C join", $J3))
[void]$cands.Add(@("D utf8bytes", $J4))
foreach ($p in $cands) {
    $name = $p[0]; $v = $p[1]
    $t = "null"
    if ($null -ne $v) { $t = $v.GetType().Name }
    $cp = ""
    $len = -1
    if ($t -eq "String") {
        $len = $v.Length
        $cp = (($v.ToCharArray() | ForEach-Object { [int]$_ }) -join ",")
    }
    "  " + $name.PadRight(12) + " type=" + $t + " len=" + $len + " cps=[" + $cp + "] value=[" + $v + "]"
}

"=== LOG ==="
if (-not (Test-Path $Log)) { "  MISSING $Log"; exit 1 }
$lines = @(Get-Content $Log -Encoding UTF8)
"  lines = " + $lines.Count
$seen = 0
foreach ($l in $lines) {
    if ($l.StartsWith("TOOLBARCHECK:")) {
        $seen++
        if ($seen -le 2) {
            $cp = (($l.ToCharArray() | ForEach-Object { [int]$_ }) -join ",")
            "  line" + $seen + " len=" + $l.Length + " cps=" + $cp
        }
    }
}
"  total TOOLBARCHECK lines = " + $seen

"=== REGEX ATTEMPTS (code-point built judge string) ==="
foreach ($p in $cands) {
    $name = $p[0]; $j = $p[1]
    $reJ = "^TOOLBARCHECK:\s*" + $j + "\s*([0-9]+)/([0-9]+)(\s+VERDICT=([A-Z]+))?"
    $hit = 0; $first = ""
    foreach ($l in $lines) {
        if ($l -match $reJ) {
            $hit++
            if ($first -eq "") { $first = $Matches[1] + "/" + $Matches[2] + " verdict=" + $Matches[4] }
        }
    }
    "  " + $name.PadRight(12) + " hits=" + $hit + " first=[" + $first + "]"
}

"=== ASCII-ONLY JUDGE-FREE CANDIDATES ==="
$cands2 = New-Object System.Collections.ArrayList
[void]$cands2.Add(@("E token-then-nums",  "^TOOLBARCHECK:\s+[^\s]+\s+([0-9]+)/([0-9]+)(\s+VERDICT=([A-Z]+))?\s*$"))
[void]$cands2.Add(@("F lazy-then-eol",    "^TOOLBARCHECK:.*?([0-9]+)/([0-9]+)\s*$"))
[void]$cands2.Add(@("G lazy-then-verd",   "^TOOLBARCHECK:.*?([0-9]+)/([0-9]+)\s+VERDICT=([A-Z]+)\s*$"))
foreach ($p in $cands2) {
    $name = $p[0]; $re = $p[1]
    $hit = 0; $first = ""
    foreach ($l in $lines) {
        if ($l -match $re) {
            $hit++
            if ($first -eq "") { $first = $Matches[1] + "/" + $Matches[2] + " g3=" + $Matches[3] + " g4=" + $Matches[4] }
        }
    }
    "  " + $name.PadRight(20) + " hits=" + $hit + " first=[" + $first + "]"
}

"=== raw bytes of the first count line, as the file holds them ==="
$bytes = [System.IO.File]::ReadAllBytes($Log)
$txt = [System.Text.Encoding]::UTF8.GetString($bytes)
$idx = $txt.IndexOf("TOOLBARCHECK:")
if ($idx -ge 0) {
    $n = [Math]::Min(60, $txt.Length - $idx)
    $seg = $txt.Substring($idx, $n)
    $b = [System.Text.Encoding]::UTF8.GetBytes($seg)
    "  " + (($b | ForEach-Object { $_.ToString("X2") }) -join " ")
} else {
    "  TOOLBARCHECK: not found in raw text"
}

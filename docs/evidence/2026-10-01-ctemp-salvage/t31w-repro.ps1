$ErrorActionPreference = "Continue"
$OK = [char]0x2713   # check mark
$dir = "C:\temp\t31w-suites"

function Read-Lines {
    param([string]$File, [string]$Anchor = "*VERDICT=*", [int]$Tries = 3)
    for ($t = 1; $t -le $Tries; $t++) {
        $lines = @(Get-Content $File -Encoding UTF8)
        if ($Anchor -eq "") { return $lines }
        if (@($lines | Where-Object { $_ -like $Anchor }).Count -gt 0) { return $lines }
        Start-Sleep -Milliseconds 400
    }
    return $lines
}

function Dump([string]$label, [string]$s, [int]$n) {
    $chars = $s.ToCharArray() | Select-Object -First $n
    $codes = ($chars | ForEach-Object { [int]$_ }) -join ","
    Write-Output ("{0}: len={1} first{2}codepoints=[{3}]" -f $label, $s.Length, $n, $codes)
}

Dump "OK-pattern-prefix" ("INTERACTCHECK: $OK INTERACT-INTEGRITY*") 30
Write-Output ("OK char code = {0}" -f [int]$OK)

for ($i = 1; $i -le 3; $i++) {
    $f = Join-Path $dir "negctl-run$i.out.txt"
    Write-Output ("=== run{0} exists={1} ===" -f $i, (Test-Path $f))
    $lines = @(Get-Content $f -Encoding UTF8)
    Write-Output ("  raw Get-Content line count = {0}" -f $lines.Count)
    $hits = @($lines | Where-Object { $_ -like "*INTERACT-INTEGRITY*" })
    Write-Output ("  plain *INTERACT-INTEGRITY* hits = {0}" -f $hits.Count)
    if ($hits.Count -gt 0) { Dump "    line" $hits[0] 30 }
    $m1 = @($lines | Where-Object { $_ -like "INTERACTCHECK: $OK INTERACT-INTEGRITY*" }).Count
    Write-Output ("  exact pattern hits = {0}" -f $m1)
    $m2 = @($lines | Where-Object { $_.StartsWith("INTERACTCHECK: $OK INTERACT-INTEGRITY") }).Count
    Write-Output ("  StartsWith hits = {0}" -f $m2)

    $txt = @(Read-Lines -File $f)
    Write-Output ("  Read-Lines count = {0}" -f $txt.Count)
    $m3 = @($txt | Where-Object { $_ -like "INTERACTCHECK: $OK INTERACT-INTEGRITY*" }).Count
    Write-Output ("  Read-Lines exact hits = {0}" -f $m3)
    $m4 = @($txt | Where-Object { $_ -like "*VERDICT=*" }).Count
    Write-Output ("  Read-Lines anchor VERDICT hits = {0}" -f $m4)
    Write-Output ("  txt element types = {0}" -f (($txt | ForEach-Object { $_.GetType().Name } | Select-Object -Unique) -join "/"))
}

# win-temp-audit.ps1 -- read-only inventory of C:\temp for archive reconciliation.
# Output format (one line per entry):
#   F|<name>|<bytes>|<MD5>
#   D|<name>|0|<DIR>
# Deliberately pure ASCII and no here-strings: PS 5.1 + ssh both mangle them.
$ErrorActionPreference = 'Continue'
$all = @(Get-ChildItem 'C:\temp' -Force)

Write-Output ("TOTAL={0} DIRS={1} FILES={2} BYTES={3}" -f `
    $all.Count,
    (@($all | Where-Object { $_.PSIsContainer }).Count),
    (@($all | Where-Object { -not $_.PSIsContainer }).Count),
    ((@($all | Where-Object { -not $_.PSIsContainer }) | Measure-Object Length -Sum).Sum))

# ---- top-level entries ----
foreach ($e in ($all | Sort-Object Name)) {
    if ($e.PSIsContainer) {
        $sub = @(Get-ChildItem $e.FullName -Recurse -File -ErrorAction SilentlyContinue)
        $sb  = ($sub | Measure-Object Length -Sum).Sum
        if ($null -eq $sb) { $sb = 0 }
        Write-Output ("D|{0}|{1}|{2}" -f $e.Name, $sb, $sub.Count)
    } else {
        $h = ''
        try { $h = (Get-FileHash $e.FullName -Algorithm MD5).Hash } catch { $h = 'ERR' }
        Write-Output ("F|{0}|{1}|{2}" -f $e.Name, $e.Length, $h)
    }
}

# ---- every file inside the subdirectories, path relative to C:\temp ----
Write-Output '---RECURSE---'
foreach ($f in (Get-ChildItem 'C:\temp' -Recurse -File -ErrorAction SilentlyContinue |
                Sort-Object FullName)) {
    $rel = $f.FullName.Substring('C:\temp\'.Length)
    $h = ''
    try { $h = (Get-FileHash $f.FullName -Algorithm MD5).Hash } catch { $h = 'ERR' }
    Write-Output ("f|{0}|{1}|{2}" -f $rel, $f.Length, $h)
}
Write-Output '---END---'

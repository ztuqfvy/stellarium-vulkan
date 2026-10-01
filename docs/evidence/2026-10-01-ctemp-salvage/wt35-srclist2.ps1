# wt35-srclist2.ps1 -- EOL-NORMALISED content comparison between the mac
# checkout and the E: checkout.
#
# WHY NOT Get-FileHash: this box's checkout converted LF -> CRLF for every file
# it received via git (core.autocrlf), while the files that arrived by SCP from
# the mac are LF. So a raw MD5 compares LINE ENDINGS, not CONTENT -- measured on
# 2026-09-30: src/app/ActionRouter.cpp differed by raw md5 but was byte-identical
# after stripping CR.
#
# `git hash-object <path>` applies the same clean filters as `git add`, so it
# normalises the EOL and hashes the CONTENT. Measured: the mac and the Windows
# side both return fef39003087ff09584b8f46bf893fc8395f68269 for ActionRouter.cpp.
param(
    [string]$Repo = "E:\Qt_demo\stellarium-vulkan",
    [string]$List = "C:\temp\wt35-filelist.txt",
    [string]$Out  = "C:\temp\wt35-win-objhash.txt"
)
$ErrorActionPreference = "Continue"
Set-Location $Repo
Remove-Item $Out -ErrorAction SilentlyContinue
$n = 0; $missing = 0
foreach ($rel in (Get-Content $List)) {
    $rel = $rel.Trim()
    if ($rel -eq "") { continue }
    $n++
    $p = Join-Path $Repo ($rel -replace "/", "\")
    if (-not (Test-Path $p)) {
        ("MISSING" + (" " * 26) + $rel) | Out-File -Encoding ascii -Append $Out
        $missing++
        continue
    }
    $h = (& git hash-object $rel 2>$null)
    if ($LASTEXITCODE -ne 0 -or -not $h) {
        ("HASHERR" + (" " * 26) + $rel) | Out-File -Encoding ascii -Append $Out
        $missing++
    } else {
        (($h.Trim() + "  " + $rel)) | Out-File -Encoding ascii -Append $Out
    }
}
Write-Host ("lines=$n missing_or_err=$missing out=$Out")

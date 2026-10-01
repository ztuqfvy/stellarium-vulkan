# wt35-srclist.ps1 -- hash a list of repo-relative paths on the Windows side.
# Used BEFORE any SCP so we know exactly which files differ between the mac
# checkout and the E: checkout. Inline multi-line `-Command -` proved unreliable
# through this tunnel (produced no output, created no file), so this is a FILE.
param(
    [string]$Repo = "E:\Qt_demo\stellarium-vulkan",
    [string]$List = "C:\temp\wt35-filelist.txt",
    [string]$Out  = "C:\temp\wt35-win-md5.txt"
)
$ErrorActionPreference = "Continue"
Remove-Item $Out -ErrorAction SilentlyContinue
$n = 0; $missing = 0
foreach ($rel in (Get-Content $List)) {
    $rel = $rel.Trim()
    if ($rel -eq "") { continue }
    $p = Join-Path $Repo ($rel -replace "/", "\")
    $n++
    if (Test-Path $p) {
        ((Get-FileHash $p -Algorithm MD5).Hash.ToLower() + "  " + $rel) | Out-File -Encoding ascii -Append $Out
    } else {
        ("MISSING" + (" " * 26) + $rel) | Out-File -Encoding ascii -Append $Out
        $missing++
    }
}
Write-Host ("lines=$n missing=$missing out=$Out")

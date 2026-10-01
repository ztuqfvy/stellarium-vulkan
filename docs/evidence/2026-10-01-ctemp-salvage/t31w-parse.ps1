$b = "E:\Qt_demo\stellarium-vulkan\tools\windows"
foreach ($f in @("wt31-build.ps1","wt31-pull.ps1","wt31-suites.ps1")) {
    $p = Join-Path $b $f
    $e = $null
    [void][System.Management.Automation.Language.Parser]::ParseFile($p, [ref]$null, [ref]$e)
    if ($e.Count -eq 0) { Write-Output "$f PARSE-OK" }
    else {
        Write-Output "$f PARSE-ERR $($e.Count)"
        foreach ($x in $e) { Write-Output ("   " + $x.Message) }
    }
}

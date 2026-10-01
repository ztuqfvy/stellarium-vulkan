$OK = [char]0x2713
foreach ($n in @("negctl-run1","negctl-run2","negctl-run3")) {
    $p = "C:\temp\t31w-suites\$n.out.txt"
    $txt = Get-Content $p -Encoding UTF8
    $c1 = @($txt | Where-Object { $_ -like "INTERACTCHECK: $OK INTERACT-INTEGRITY*" }).Count
    $c2 = @($txt | Where-Object { $_ -like "*INTERACT-INTEGRITY*" }).Count
    $c3 = @($txt | Where-Object { $_ -match "INTERACT-INTEGRITY" }).Count
    $hit = ($txt | Where-Object { $_ -like "*INTERACT-INTEGRITY*" } | Select-Object -First 1)
    Write-Output "$n  likeWithOK=$c1  likePlain=$c2  match=$c3"
    if ($hit) {
        Write-Output ("   firstchar=" + [int][char]$hit[0] + "  len=" + $hit.Length)
        Write-Output ("   prefix=" + $hit.Substring(0, [Math]::Min(40, $hit.Length)))
    }
}

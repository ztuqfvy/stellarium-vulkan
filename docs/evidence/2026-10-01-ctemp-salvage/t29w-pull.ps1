$src = "E:\Qt_demo\stellarium-vulkan"
$log = "C:\temp\t29w-pull.log"
Remove-Item $log -ErrorAction SilentlyContinue
Set-Location $src
"[pull] $(Get-Date -Format o)" | Out-File -Encoding ascii $log
"before=" + (git rev-parse --short HEAD) | Out-File -Encoding ascii -Append $log
git merge --ff-only origin/main *>&1 | Out-File -Encoding utf8 -Append $log
$rc = $LASTEXITCODE
"merge_rc=$rc" | Out-File -Encoding ascii -Append $log
"after=" + (git rev-parse --short HEAD) | Out-File -Encoding ascii -Append $log
"--- status ---" | Out-File -Encoding ascii -Append $log
git status --short *>&1 | Out-File -Encoding utf8 -Append $log
"[ok]" | Out-File -Encoding ascii -Append $log

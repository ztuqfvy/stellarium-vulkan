$src = "E:\Qt_demo\stellarium-vulkan"
$log = "C:\temp\t29w-pull2.log"
Remove-Item $log -ErrorAction SilentlyContinue
Set-Location $src
"before=" + (git rev-parse --short HEAD) | Out-File -Encoding ascii $log
git fetch origin *>&1 | Out-File -Encoding utf8 -Append $log
git merge --ff-only origin/main *>&1 | Out-File -Encoding utf8 -Append $log
"merge_rc=$LASTEXITCODE" | Out-File -Encoding ascii -Append $log
"after=" + (git rev-parse --short HEAD) | Out-File -Encoding ascii -Append $log
git status --short *>&1 | Out-File -Encoding utf8 -Append $log
"[ok]" | Out-File -Encoding ascii -Append $log

$src = "E:\Qt_demo\stellarium-vulkan"
$log = "C:\temp\t35w-pull.log"
Remove-Item $log -ErrorAction SilentlyContinue
Set-Location $src
"[pull] $(Get-Date -Format o)" | Out-File -Encoding ascii $log
"before=" + (git rev-parse --short HEAD) | Out-File -Encoding ascii -Append $log
git fetch origin main *>&1 | Out-File -Encoding utf8 -Append $log
"fetch_rc=$LASTEXITCODE" | Out-File -Encoding ascii -Append $log
# --ff-only on purpose: the 12 junction dirs show up as deleted in git status
# (core.symlinks=false) -- a plain checkout/reset would replace them with real
# empty dirs and break the data/ lookups. See the uu-remote-windows skill.
#
# This box OFTEN cannot reach github ("Recv failure: Connection was reset"), in
# which case fetch_rc != 0 and merge_rc != 0 -- that is EXPECTED. The tree is
# then updated by SCP-ing the changed source files from the mac; wt35-build.ps1
# and wt35-suites.ps1 both hash those files so the built revision is provable
# from CONTENT, not from a (lying) repo HEAD.
#
# W-T35 covers TWO batches at once: T34 (real toolbar + 12 display toggles) and
# T35 (simulation-time link de-coupling). The mac checkouts of the files are the
# authority; compare the "SRC ... md5=" lines in the manifest/SUMMARY against
# `md5 -q <file>` on the mac.
git merge --ff-only origin/main *>&1 | Out-File -Encoding utf8 -Append $log
$rc = $LASTEXITCODE
"merge_rc=$rc" | Out-File -Encoding ascii -Append $log
"after=" + (git rev-parse --short HEAD) | Out-File -Encoding ascii -Append $log
"--- status ---" | Out-File -Encoding ascii -Append $log
git status --short *>&1 | Out-File -Encoding utf8 -Append $log
"[ok]" | Out-File -Encoding ascii -Append $log

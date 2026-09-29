# wt32-build.ps1 -- Windows incremental build for T32 (source: the T32 commit)
# ASCII-only on purpose (avoid codepage corruption on the Windows side).
# Uses VS-bundled cmake: generic CMake does not know "Visual Studio 18 2026".
$ErrorActionPreference = "Continue"

$cmake = "E:\VisualStudio\CanPin\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$src   = "E:\Qt_demo\stellarium-vulkan"
$build = "$src\build-win"
$log   = "C:\temp\t32w-build.log"
$rcf   = "C:\temp\t32w-build.rc"
$mf    = "C:\temp\t32w-build-manifest.txt"

Remove-Item $log -ErrorAction SilentlyContinue
Remove-Item $rcf -ErrorAction SilentlyContinue
Remove-Item $mf  -ErrorAction SilentlyContinue

"[start] $(Get-Date -Format o)"        | Out-File -Encoding ascii $log
"cmake = $cmake"                       | Out-File -Encoding ascii -Append $log
"src   = $src"                         | Out-File -Encoding ascii -Append $log
"build = $build"                       | Out-File -Encoding ascii -Append $log
"head  = " + (& git -C $src rev-parse --short HEAD) | Out-File -Encoding ascii -Append $log
"mode  = incremental ALL_BUILD (Release)" | Out-File -Encoding ascii -Append $log

$t0 = Get-Date
& $cmake --build $build --config Release --parallel *>&1 |
    Out-File -Encoding utf8 -Append $log
$rc = $LASTEXITCODE
$t1 = Get-Date

"rc=$rc" | Out-File -Encoding ascii $rcf
"elapsed_min=" + [math]::Round(($t1 - $t0).TotalMinutes, 2) | Out-File -Encoding ascii -Append $mf

# stelQuickUI.exe is the merged host (it carries the self-check machinery).
# stellarium.exe is the legacy host: T32 touches only src/ui/qml/MainWindow.qml
# plus the INTERACTCHECK instrument in src/ui/main.cpp, and NEITHER is built into
# the legacy host -- so its md5 must stay byte-identical to the W-T31/W-T30
# baseline (66C51B61582BAC065C7A7FE5ACA44A42). That byte-level equality is the
# evidence that S3 (legacy host regression) does not need a re-run.
foreach ($p in @("$build\src\ui\Release\stelQuickUI.exe", "$build\src\Release\stellarium.exe")) {
    if (Test-Path $p) {
        $f = Get-Item $p
        $h = (Get-FileHash $p -Algorithm MD5).Hash
        "$($f.Name)  size=$($f.Length)  md5=$h  mtime=$($f.LastWriteTime.ToString('o'))" |
            Out-File -Encoding ascii -Append $mf
    } else {
        "MISSING $p" | Out-File -Encoding ascii -Append $mf
    }
}

# T32 source self-evidence. This box frequently cannot reach github (git fetch
# dies with "Recv failure: Connection was reset"), so the tree is normally
# updated by SCP-ing the changed files and the repo HEAD therefore LIES about
# what was built. Hash the two files T32 actually touches and compare them
# against the repo copies by hand -- same idea as the "script md5" line in
# wt32-suites.ps1 (T31: the repo HEAD in SUMMARY pointed at a stale commit).
foreach ($p in @("$src\src\ui\qml\MainWindow.qml", "$src\src\ui\main.cpp",
                 "$src\tools\windows\wt32-suites.ps1")) {
    if (Test-Path $p) {
        $f = Get-Item $p
        "SRC $($f.Name)  size=$($f.Length)  md5=$((Get-FileHash $p -Algorithm MD5).Hash.ToLower())" |
            Out-File -Encoding ascii -Append $mf
    } else {
        "SRC MISSING $p" | Out-File -Encoding ascii -Append $mf
    }
}

"[done] $(Get-Date -Format o) rc=$rc" | Out-File -Encoding ascii -Append $log

# wt33-build.ps1 -- Windows incremental build for T33 (observation-location page).
# ASCII-only on purpose (avoid codepage corruption on the Windows side).
# Uses VS-bundled cmake: generic CMake does not know "Visual Studio 18 2026".
$ErrorActionPreference = "Continue"

$cmake = "E:\VisualStudio\CanPin\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$src   = "E:\Qt_demo\stellarium-vulkan"
$build = "$src\build-win"
$log   = "C:\temp\t33w-build.log"
$rcf   = "C:\temp\t33w-build.rc"
$mf    = "C:\temp\t33w-build-manifest.txt"

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
# stellarium.exe is the legacy host. T33 touches src/app/LocationProbe.*,
# src/app/LocationCheck.*, src/app/AppFacade.*, src/ui/main.cpp and two QML files
# -- NONE of which is built into the legacy host (src/app/* is only listed under
# the stelQuickUI target in src/ui/CMakeLists.txt).
#
# CORRECTION (T33-D, 2026-09-29): an earlier revision of this comment claimed
# stellarium.exe's md5 "must stay byte-identical" to the W-T31 baseline
# (66C51B61582BAC065C7A7FE5ACA44A42) and used that as proof S3 need not re-run.
# The T33-D build MEASURED it at F6E9CE856CA77CC5552F3374AB840D99 (same size
# 27634176). The build log shows NO T33 source compiled into the legacy host --
# the cause is MSVC link non-determinism plus the re-linked _deps (ShowMySky
# etc.) dragging a fresh link. So md5 CANNOT be used to assert "legacy host
# untouched". Recorded here as a trap: the md5 is now printed for the record
# only, and S3 is actually RUN (see wt33-suites.ps1 section 8).
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

# T33 source self-evidence. This box frequently cannot reach github (git fetch
# dies with "Recv failure: Connection was reset"), so the tree is normally
# updated by SCP-ing the changed files and the repo HEAD therefore LIES about
# what was built. Hash every file T33 actually touches and compare them against
# the repo copies by hand -- same idea as the "script md5" line in
# wt33-suites.ps1 (T31: the repo HEAD in SUMMARY pointed at a stale commit).
foreach ($rel in @("src\app\LocationProbe.hpp", "src\app\LocationProbe.cpp",
                   "src\app\LocationCheck.hpp", "src\app\LocationCheck.cpp",
                   "src\app\AppFacade.hpp",     "src\app\AppFacade.cpp",
                   "src\ui\main.cpp",           "src\ui\CMakeLists.txt",
                   "src\ui\qml\LocationPage.qml", "src\ui\qml\MainWindow.qml",
                   "tools\windows\wt33-suites.ps1")) {
    $p = Join-Path $src $rel
    if (Test-Path $p) {
        $f = Get-Item $p
        "SRC $rel  size=$($f.Length)  md5=$((Get-FileHash $p -Algorithm MD5).Hash.ToLower())" |
            Out-File -Encoding ascii -Append $mf
    } else {
        "SRC MISSING $p" | Out-File -Encoding ascii -Append $mf
    }
}

"[done] $(Get-Date -Format o) rc=$rc" | Out-File -Encoding ascii -Append $log

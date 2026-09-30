# wt35-build.ps1 -- Windows incremental build covering T34 + T35.
#
# T34 = real toolbar (Toolbar.qml) + 12 display toggles; new instrument
#       src/app/ToolbarProbe.* and src/app/ToolbarCheck.*.
# T35 = simulation-time link de-coupling (T27's leftover lead); new instrument
#       src/app/TimeLinkProbe.* and src/app/TimeLinkCheck.*; main.cpp gained two
#       phases; LiveSkyRuntime.cpp gained three negative-control injections.
#
# ASCII-only on purpose (avoid codepage corruption on the Windows side).
# Uses VS-bundled cmake: generic CMake does not know "Visual Studio 18 2026".
$ErrorActionPreference = "Continue"

$cmake = "E:\VisualStudio\CanPin\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$src   = "E:\Qt_demo\stellarium-vulkan"
$build = "$src\build-win"
$log   = "C:\temp\t35w-build.log"
$rcf   = "C:\temp\t35w-build.rc"
$mf    = "C:\temp\t35w-build-manifest.txt"

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
# stellarium.exe is the legacy host (src/main.cpp + stelMain). src/app/* is only
# listed under the stelQuickUI target in src/ui/CMakeLists.txt, so NEITHER T34
# nor T35 touches the legacy host.
#
# DO NOT treat the legacy md5 as proof of that: the T33-D build measured it
# changing (66C51B61... -> F6E9CE85..., same size) with no T33 source in its
# compile list -- MSVC linking re-rolls the timestamp / PDB signature and the
# re-linked _deps drag the legacy host along. So the md5 is printed for the
# record only and S3 is actually RUN (wt35-suites.ps1, section 2).
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

# T34+T35 source self-evidence. This box frequently cannot reach github (git
# fetch dies with "Recv failure: Connection was reset"), so the tree is normally
# updated by SCP-ing the changed files and the repo HEAD therefore LIES about
# what was built. Hash every file T34/T35 actually touches; compare each line
# against `md5 -q <file>` on the mac.
foreach ($rel in @("src\app\ToolbarProbe.hpp",  "src\app\ToolbarProbe.cpp",
                   "src\app\ToolbarCheck.hpp",  "src\app\ToolbarCheck.cpp",
                   "src\app\TimeLinkProbe.hpp", "src\app\TimeLinkProbe.cpp",
                   "src\app\TimeLinkCheck.hpp", "src\app\TimeLinkCheck.cpp",
                   "src\app\AppFacade.hpp",     "src\app\AppFacade.cpp",
                   "src\ui\LiveSkyRuntime.cpp", "src\ui\main.cpp",
                   "src\ui\CMakeLists.txt",
                   "src\ui\qml\Toolbar.qml",    "src\ui\qml\MainWindow.qml",
                   "src\ui\quick\BackendInfo.hpp", "src\ui\quick\BackendInfo.cpp",
                   "tools\windows\wt35-suites.ps1")) {
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

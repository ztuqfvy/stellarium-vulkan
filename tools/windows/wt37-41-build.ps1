# wt37-41-build.ps1 -- Windows incremental build covering T37..T41.
#
# T37 night mode      src/app/NightCheck.*      + NightOverlay.qml + .qsb shader
# T38 display params  src/app/DisplayCheck.*    + DisplayPage.qml + AppFacade display face
# T39 hi-dpi + diag   src/app/HiDpiCheck.*      + DiagnosticPage.qml + BackendInfo
# T40 shortcut editor src/app/ShortcutCheck.*   + ShortcutsPage.qml
# T41 help/about/lic  src/app/HelpCheck.*       + HelpPage/AboutPage.qml + qrc COPYING
#
# ASCII-only on purpose (PS 5.1 reads a BOM-less .ps1 in the ANSI codepage).
# Uses VS-bundled cmake: generic CMake does not know "Visual Studio 18 2026".
$ErrorActionPreference = "Continue"

$cmake = "E:\VisualStudio\CanPin\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$src   = "E:\Qt_demo\stellarium-vulkan"
$build = "$src\build-win"
$log   = "C:\temp\t37w-build.log"
$rcf   = "C:\temp\t37w-build.rc"
$mf    = "C:\temp\t37w-build-manifest.txt"

Remove-Item $log -ErrorAction SilentlyContinue
Remove-Item $rcf -ErrorAction SilentlyContinue
Remove-Item $mf  -ErrorAction SilentlyContinue

"[start] $(Get-Date -Format o)"          | Out-File -Encoding ascii $log
"cmake = $cmake"                         | Out-File -Encoding ascii -Append $log
"src   = $src"                           | Out-File -Encoding ascii -Append $log
"build = $build"                         | Out-File -Encoding ascii -Append $log
"head  = " + (& git -C $src rev-parse --short HEAD) | Out-File -Encoding ascii -Append $log
"HEAD above is UNRELIABLE on this box (no github -> scp-updated tree)." | Out-File -Encoding ascii -Append $log
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
# listed under the stelQuickUI target in src/ui/CMakeLists.txt, so NONE of
# T37..T41 touches the legacy host.
#
# DO NOT treat the legacy md5 as proof of that: the T33-D build measured it
# changing with no T33 source in its compile list -- MSVC re-linking re-rolls the
# timestamp / PDB signature and the re-linked _deps drag the legacy host along.
# The md5 is printed for the record only.
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

# T37..T41 source self-evidence. This box frequently cannot reach github, so the
# tree is normally updated by SCP-ing the changed files and the repo HEAD
# therefore LIES about what was built. Hash every file T37..T41 actually
# touches; compare each line against `md5 -q <file>` on the mac.
foreach ($rel in @("src\app\NightModeCheck.hpp", "src\app\NightModeCheck.cpp",
                   "src\app\NightModeProbe.hpp", "src\app\NightModeProbe.cpp",
                   "src\ui\shaders\nightmode.frag",
                   "src\app\DisplayCheck.hpp",   "src\app\DisplayCheck.cpp",
                   "src\app\HiDpiCheck.hpp",     "src\app\HiDpiCheck.cpp",
                   "src\app\ShortcutCheck.hpp",  "src\app\ShortcutCheck.cpp",
                   "src\app\HelpCheck.hpp",      "src\app\HelpCheck.cpp",
                   "src\app\AppFacade.hpp",      "src\app\AppFacade.cpp",
                   "src\ui\quick\BackendInfo.hpp", "src\ui\quick\BackendInfo.cpp",
                   "src\ui\LiveSkyRuntime.cpp",  "src\ui\main.cpp",
                   "src\ui\CMakeLists.txt",
                   "src\ui\qml\DisplayPage.qml",
                   "src\ui\qml\DiagnosticPage.qml", "src\ui\qml\ShortcutsPage.qml",
                   "src\ui\qml\HelpPage.qml",    "src\ui\qml\AboutPage.qml",
                   "src\ui\qml\Toolbar.qml",     "src\ui\qml\MainWindow.qml")) {
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

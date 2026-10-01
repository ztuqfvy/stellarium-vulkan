# wt37-41-status.ps1 -- read-only recon of the Windows box before the W-T37..W-T41 trip.
#
# Answers three questions in one round-trip:
#   1. Is the tree already built, and how stale is the binary?
#   2. Which of the T37..T41 source files differ from the mac (md5 compare)?
#   3. Are the toolchain paths still where the scripts expect them?
#
# READ-ONLY. Touches nothing. ASCII-only (PS 5.1 reads BOM-less .ps1 in the ANSI
# codepage; a Chinese comment would be mangled into stray quotes -> PARSE-ERR).
$ErrorActionPreference = "Continue"

$Repo = "E:\Qt_demo\stellarium-vulkan"
$QtDir = "E:\Qt\6.11.2\msvc2022_64"
$VulkanBin = "E:\Vulkan\SDK\Bin"
$cmake = "E:\VisualStudio\CanPin\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

"=== toolchain paths ==="
foreach ($p in @($Repo, $QtDir, $VulkanBin, $cmake)) {
    "{0}  exists={1}" -f $p, (Test-Path $p)
}

""
"=== repo ==="
"HEAD = " + (& git -C $Repo rev-parse --short HEAD 2>&1)
"branch = " + (& git -C $Repo rev-parse --abbrev-ref HEAD 2>&1)
"status (first 12 lines, junctions show as D -- EXPECTED, never checkout/reset):"
& git -C $Repo status --short 2>&1 | Select-Object -First 12 | ForEach-Object { "  $_" }

""
"=== binaries ==="
foreach ($b in @("build-win\src\ui\Release\stelQuickUI.exe",
                 "build-win\src\Release\stellarium.exe")) {
    $p = Join-Path $Repo $b
    if (Test-Path $p) {
        $f = Get-Item $p
        "{0}  size={1}  mtime={2}  md5={3}" -f $b, $f.Length,
            $f.LastWriteTime.ToString('o'),
            (Get-FileHash $p -Algorithm MD5).Hash.ToLower()
    } else {
        "{0}  MISSING" -f $b
    }
}

""
"=== T37..T41 source md5 (compare line-by-line against 'md5 -q <file>' on the mac) ==="
foreach ($rel in @("src\app\NightCheck.hpp",     "src\app\NightCheck.cpp",
                   "src\app\DisplayCheck.hpp",   "src\app\DisplayCheck.cpp",
                   "src\app\HiDpiCheck.hpp",     "src\app\HiDpiCheck.cpp",
                   "src\app\ShortcutCheck.hpp",  "src\app\ShortcutCheck.cpp",
                   "src\app\HelpCheck.hpp",      "src\app\HelpCheck.cpp",
                   "src\app\AppFacade.hpp",      "src\app\AppFacade.cpp",
                   "src\ui\quick\BackendInfo.hpp", "src\ui\quick\BackendInfo.cpp",
                   "src\ui\LiveSkyRuntime.cpp",  "src\ui\main.cpp",
                   "src\ui\CMakeLists.txt",
                   "src\ui\qml\NightOverlay.qml", "src\ui\qml\DisplayPage.qml",
                   "src\ui\qml\DiagnosticPage.qml", "src\ui\qml\ShortcutsPage.qml",
                   "src\ui\qml\HelpPage.qml",    "src\ui\qml\AboutPage.qml",
                   "src\ui\qml\Toolbar.qml",     "src\ui\qml\MainWindow.qml",
                   "src\ui\qml\SearchPage.qml",  "src\ui\qml\TimePage.qml",
                   "src\ui\qml\LocationPage.qml", "src\ui\qml\ErrorPage.qml")) {
    $s = Join-Path $Repo $rel
    if (Test-Path $s) {
        "SRC {0} {1} {2}" -f $rel, (Get-Item $s).Length,
            (Get-FileHash $s -Algorithm MD5).Hash.ToLower()
    } else {
        "SRC {0} MISSING" -f $rel
    }
}

""
"=== leftover scheduled tasks (must be empty; a stale StelQC_* holds a desktop session) ==="
$t = & schtasks /query /fo csv 2>&1 | Select-String "StelQC"
if ($t) { $t | ForEach-Object { "  $_" } } else { "  (none)" }

""
"=== stray stelQuickUI processes ==="
$procs = Get-Process stelQuickUI -ErrorAction SilentlyContinue
if ($procs) { $procs | ForEach-Object { "  pid={0} start={1}" -f $_.Id, $_.StartTime } } else { "  (none)" }

""
"status probe done"

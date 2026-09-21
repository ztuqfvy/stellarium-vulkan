# make-deploy.ps1 - collect a standalone runnable folder for stelQuickUI (build-ui\deploy)
#
# Why not `cmake --build --target deploy` (windeployqt):
#   Measured on Qt 6.11.2 + VS2026 (2026-09-21): windeployqt output is NOT
#   standalone-runnable:
#     1) qml/ QtQuick module tree is incomplete -> qtquickcontrols2plugin.dll
#        dependency chain breaks -> QML components silently fail to load
#        ("main window creation failed", no reason printed);
#     2) styles/ folder missing -> Quick Controls default style plugin absent;
#     3) vcruntime140 / msvcp140 missing.
#   This script collects the actual DLL closure and has been verified to run
#   without any Qt in PATH.
#
# Usage (plain PowerShell, no Qt/VS env needed):
#   powershell -ExecutionPolicy Bypass -File tools\make-deploy.ps1
param(
    [string]$QtDir  = "E:\Qt\6.11.2\msvc2022_64",
    [string]$BuildDir = (Join-Path (Split-Path -Parent $PSScriptRoot) "build-ui")
)

$ErrorActionPreference = "Stop"
$deploy  = Join-Path $BuildDir "deploy"
$release = Join-Path $BuildDir "Release\stelQuickUI.exe"

if (-not (Test-Path $release)) {
    throw "exe not found: $release. Build first: cmake --build build-ui --config Release"
}

Write-Host "[1/4] reset deploy folder"
if (Test-Path $deploy) { Remove-Item $deploy -Recurse -Force }
New-Item -ItemType Directory -Path $deploy | Out-Null

Write-Host "[2/4] copy exe"
Copy-Item $release $deploy

# Qt runtime DLL closure. This is the set actually loaded by exe + QML modules +
# plugins. If new QML modules are added later (QtQuick.Shapes, Qt.labs.*, ...),
# append the corresponding DLLs here.
$qtDlls = @(
    "Qt6Core.dll", "Qt6Gui.dll", "Qt6Qml.dll",
    "Qt6QmlMeta.dll", "Qt6QmlModels.dll", "Qt6QmlWorkerScript.dll",
    "Qt6Quick.dll", "Qt6QuickTemplates2.dll", "Qt6QuickLayouts.dll",
    "Qt6QuickDialogs2.dll", "Qt6QuickDialogs2Utils.dll", "Qt6QuickWidgets.dll",
    "Qt6QuickControls2.dll", "Qt6QuickControls2Impl.dll",
    "Qt6QuickControls2Basic.dll", "Qt6QuickControls2BasicStyleImpl.dll",
    "Qt6QuickControls2Fusion.dll", "Qt6QuickControls2FusionStyleImpl.dll",
    "Qt6QuickControls2Imagine.dll", "Qt6QuickControls2ImagineStyleImpl.dll",
    "Qt6QuickControls2Material.dll", "Qt6QuickControls2MaterialStyleImpl.dll",
    "Qt6QuickControls2Universal.dll", "Qt6QuickControls2UniversalStyleImpl.dll",
    "Qt6QuickControls2WindowsStyleImpl.dll",
    "Qt6Network.dll", "Qt6Svg.dll", "Qt6OpenGL.dll", "Qt6Lottie.dll",
    "Qt6Quick3DUtils.dll", "Qt6QuickVectorImageGenerator.dll",
    "Qt6LottieVectorImageGenerator.dll", "Qt6VirtualKeyboard.dll",
    "Qt6Core5Compat.dll"
)
$copied = 0
foreach ($dll in $qtDlls) {
    $src = Join-Path $QtDir "bin\$dll"
    if (Test-Path $src) { Copy-Item $src $deploy; $copied++ }
}
Write-Host "      collected $copied / $($qtDlls.Count) Qt modules"

Write-Host "[3/4] copy MSVC runtime"
foreach ($dll in @("vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll")) {
    $src = Join-Path $env:SystemRoot "System32\$dll"
    if (Test-Path $src) { Copy-Item $src $deploy }
}

$plugins = @{
    "platforms"             = @("qwindows.dll")
    "styles"                = @("qmodernwindowsstyle.dll")
    "imageformats"          = @("qgif.dll","qico.dll","qjpeg.dll","qsvg.dll","qicns.dll","qtga.dll","qtiff.dll","qwbmp.dll","qwebp.dll")
    "iconengines"           = @("qsvgicon.dll")
    "networkinformation"    = @("qnetworklistmanager.dll")
    "tls"                   = @("qcertonlybackend.dll","qschannelbackend.dll")
}
foreach ($dir in $plugins.Keys) {
    $srcDir = Join-Path $QtDir "plugins\$dir"
    if (-not (Test-Path $srcDir)) { continue }
    $dstDir = Join-Path $deploy $dir
    New-Item -ItemType Directory -Path $dstDir -Force | Out-Null
    foreach ($n in $plugins[$dir]) {
        $s = Join-Path $srcDir $n
        if (Test-Path $s) { Copy-Item $s $dstDir }
    }
}

Write-Host "[4/4] copy QML module trees"
# NOTE: must copy each module INTO qml\<module>, not into qml\ itself,
# otherwise the second call nests them (qml\QtCore\QtQuick\...).
$qmlDst = Join-Path $deploy "qml"
New-Item -ItemType Directory -Path $qmlDst -Force | Out-Null
foreach ($mod in @("QtQuick", "QtCore", "QtQml")) {
    $src = Join-Path $QtDir "qml\$mod"
    if (Test-Path $src) {
        Copy-Item $src $qmlDst -Recurse -Force
    }
}
$d3d = Join-Path $QtDir "bin\D3Dcompiler_47.dll"
if (Test-Path $d3d) { Copy-Item $d3d $deploy }

$count = (Get-ChildItem $deploy -Recurse -File).Count
Write-Host ""
Write-Host "done: $deploy ($count files)"
Write-Host "run  : double-click $deploy\stelQuickUI.exe, or repo-root run_autotest.cmd"

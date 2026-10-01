# t29w-win-build.ps1 -- Windows incremental build for T29 (source 20d4157)
# ASCII-only on purpose (avoid codepage corruption on the Windows side).
# Uses VS-bundled cmake 4.3.1: generic CMake does not know "Visual Studio 18 2026".
$ErrorActionPreference = "Continue"

$cmake = "E:\VisualStudio\CanPin\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$src   = "E:\Qt_demo\stellarium-vulkan"
$build = "$src\build-win"
$log   = "C:\temp\t29w-build.log"
$rcf   = "C:\temp\t29w-build.rc"
$mf    = "C:\temp\t29w-build-manifest.txt"

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

"[done] $(Get-Date -Format o) rc=$rc" | Out-File -Encoding ascii -Append $log

# t17-win-build.ps1 — 在 Windows 侧后台重建（T15+T16 已拉取）。
# 关键点（来自 WINDOWS_BUILD.zh_CN.md 与实测）：
#   · 生成器是 "Visual Studio 18 2026" → 必须用 VS 自带的 cmake 4.3.1，
#     Qt Tools 自带的 3.30.5 不认这个生成器名。
#   · 跑完把退出码落盘（rc 文件），调用方轮询它，不阻塞等待。
#   · 全 ASCII 输出，避免 PS 5.1 重定向的编码混乱。
$ErrorActionPreference = "Continue"

$cmake = "E:\VisualStudio\CanPin\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$src   = "E:\Qt_demo\stellarium-vulkan"
$build = "$src\build-win"
$log   = "C:\temp\t17-win-build.log"
$rcf   = "C:\temp\t17-win-build.rc"

Remove-Item $log -ErrorAction SilentlyContinue
Remove-Item $rcf -ErrorAction SilentlyContinue

"[start] $(Get-Date -Format o)" | Out-File -Encoding ascii $log
"cmake  = $cmake" | Out-File -Encoding ascii -Append $log
"src    = $src"   | Out-File -Encoding ascii -Append $log

& $cmake --build $build --config Release --parallel *>&1 |
    Out-File -Encoding utf8 -Append $log
$rc = $LASTEXITCODE

"rc=$rc" | Out-File -Encoding ascii $rcf
"[done] $(Get-Date -Format o) rc=$rc" | Out-File -Encoding ascii -Append $log

@echo off
rem ============================================================================
rem  stelQuickUI acceptance runner
rem  Usage:
rem    run_autotest.cmd              backend autotest (auto-exit 6s)
rem    run_autotest.cmd a2           A2 12-probe pixel check (default Vulkan)
rem    run_autotest.cmd a2 d3d11     A2 control group, D3D11
rem    run_autotest.cmd a2 opengl    A2 control group, OpenGL
rem    run_autotest.cmd matrix       run A2 on all three backends
rem    run_autotest.cmd window       window interaction test (resize/hide)
rem ============================================================================
chcp 65001 >nul
rem EnableDelayedExpansion 是必需的：matrix 模式要在同一个 for 块内读 !ERRORLEVEL!，
rem 且赋值后立刻使用。旧版漏了这个开关 → !ERRORLEVEL! 不展开，逐后端退出码打印成
rem 字面量，而 RC 又被硬编码为 0 → 无论成败都报 "EXIT CODE = 0"（2026-09-21 修）。
setlocal EnableDelayedExpansion
set "EXE=%~dp0build-ui\deploy\stelQuickUI.exe"

if not exist "%EXE%" (
    echo [ERROR] exe not found: %EXE%
    echo         build first:  cmake --build build-ui --config Release
    goto :end
)

if /i "%~1"=="matrix" goto :matrix

echo ============================================================
if /i "%~1"=="a2" (
    set "STELQUICK_A2_CHECK=1"
    if not "%~2"=="" set "STELQUICK_GRAPHICS_API=%~2"
    echo  A2 pixel check ^(12 probes^)  backend=%~2
) else if /i "%~1"=="window" (
    set "STELQUICK_WINDOW_TEST=1"
    echo  Window interaction test
) else (
    set "STELQUICK_AUTOTEST_SECONDS=6"
    echo  Backend autotest ^(auto-exit in 6s^)
)
echo ============================================================
echo.
"%EXE%"
set RC=%ERRORLEVEL%
goto :report

:matrix
rem 逐后端跑 A2，聚合退出码：任一非 0 就把 RC 记为该码（保留首个失败码），
rem 只有三组全 0 才报 EXIT CODE = 0。旧版无条件 set RC=0，等于永远绿灯。
set "RC=0"
for %%A in (vulkan d3d11 opengl) do (
    echo.
    echo ########## A2 / %%A ##########
    set "STELQUICK_A2_CHECK=1"
    set "STELQUICK_GRAPHICS_API=%%A"
    call "%EXE%"
    set "CODE=!ERRORLEVEL!"
    echo [%%A] exit code = !CODE!
    if not "!CODE!"=="0" if "!RC!"=="0" set "RC=!CODE!"
)
goto :report

:report
echo.
echo ============================================================
echo  EXIT CODE = %RC%
echo   0=pass   2=no-window   3=backend-not-vulkan
echo   4=window-test-fail      5=A2-probe-fail   6=grab-unavailable
echo ============================================================

:end
echo.
pause
endlocal

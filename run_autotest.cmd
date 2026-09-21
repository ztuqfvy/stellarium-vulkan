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
setlocal
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
for %%A in (vulkan d3d11 opengl) do (
    echo.
    echo ########## A2 / %%A ##########
    set "STELQUICK_A2_CHECK=1"
    set "STELQUICK_GRAPHICS_API=%%A"
    call "%EXE%"
    echo [%%A] exit code = !ERRORLEVEL!
)
set RC=0

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

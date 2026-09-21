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
rem
rem  ASCII ONLY. cmd.exe parses .cmd files using the OEM code page (936/GBK on
rem  zh-CN). Non-ASCII bytes in this file (even inside rem comments) get
rem  mis-decoded, can splice across lines, and the leftover fragments are then
rem  EXECUTED as commands -- injecting "'xxx' is not recognized as an internal
rem  or external command" noise into captured stdout. Keep comments in English.
rem  See docs/evidence/README.md for the full write-up.
rem ============================================================================
chcp 65001 >nul
rem EnableDelayedExpansion is REQUIRED: matrix mode reads !ERRORLEVEL! inside a
rem for block and uses the value immediately after assignment. The previous
rem revision omitted this switch, so !ERRORLEVEL! was not expanded and each
rem backend's exit code printed as a literal, while RC was hardcoded to 0 --
rem meaning it always reported "EXIT CODE = 0" regardless of outcome.
rem Fixed 2026-09-21. See docs/evidence/README.md.
setlocal EnableDelayedExpansion
set "EXE=%~dp0build-ui\deploy\stelQuickUI.exe"

if not exist "%EXE%" (
    echo [ERROR] exe not found: %EXE%
    echo         build first:  cmake --build build-ui --config Release
    rem Exit 7, NOT 0. The previous revision jumped straight to :end, which skipped
    rem the report and left ERRORLEVEL at 0 -- so "run the smoke test first" printed a
    rem green light while running nothing. Same class of defect as the old hardcoded
    rem RC=0 in matrix mode. Set the code and fall through to the report.
    set "RC=7"
    goto :report
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
rem Run A2 on each backend, aggregating exit codes. Any non-zero code becomes RC
rem (first failure wins); only three zeros report EXIT CODE = 0. The previous
rem revision unconditionally did set RC=0, which was a permanent green light.
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
echo   0=pass   2=no-window   3=backend-not-vulkan   7=exe-not-found
echo   4=window-test-fail      5=A2-probe-fail   6=grab-unavailable
echo ============================================================

:end
echo.
pause
rem Propagate RC as the PROCESS exit code. Without this line the batch ran off the end
rem of the file, and the trailing "echo." above had already reset ERRORLEVEL to 0 -- so
rem the report could print "EXIT CODE = 7" while the process still returned 0. Measured
rem on Windows 2026-09-21: text said 7, process return code said 0. That matters because
rem tools/evidence/collect.ps1 fills its "dut result" header line from this process code:
rem the field could never go red. Same defect class as d717056 (matrix hardcoded RC=0)
rem and 838b1b7 (exe-not-found branch left ERRORLEVEL at 0), one level further down.
rem %RC% is expanded before endlocal runs, so the value survives the env restore.
endlocal & exit /b %RC%

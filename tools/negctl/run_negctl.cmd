@echo off
rem ============================================================================
rem  Negative control for the matrix exit-code aggregation.
rem
rem  Purpose
rem    The acceptance conclusion "all three backends PASS" is only meaningful if
rem    the aggregation that produces it can actually report failure. A previous
rem    revision of run_autotest.cmd always printed "EXIT CODE = 0" (it read
rem    !ERRORLEVEL! without EnableDelayedExpansion and then unconditionally did
rem    set RC=0), so the green light was unconditional and worthless as evidence.
rem
rem    This script proves the fix empirically. It replicates the exact aggregation
rem    logic from run_autotest.cmd but drives stub backends instead of the real
rem    executable, injecting one failure:
rem        vulkan -> 0   (pass)
rem        d3d11  -> 5   (A2-probe-fail, injected)
rem        opengl -> 0   (pass)
rem    Expected result: EXIT CODE = 5.
rem    If this ever prints 0 again, the aggregation is broken and any green
rem    matrix must not be trusted.
rem
rem  Usage:  run_negctl.cmd
rem
rem  ASCII ONLY - see run_evidence_matrix.cmd RULE 1 for why.
rem ============================================================================
setlocal EnableDelayedExpansion

set "STUB=%~dp0stub_backend.cmd"
set "RC=0"

for %%A in (vulkan d3d11 opengl) do (
    echo.
    echo ########## A2 / %%A ##########
    set "STUB_CODE=0"
    if /i "%%A"=="d3d11" set "STUB_CODE=5"
    set "STELQUICK_A2_CHECK=1"
    set "STELQUICK_GRAPHICS_API=%%A"
    call "%STUB%" %%A !STUB_CODE!
    set "CODE=!ERRORLEVEL!"
    echo [%%A] exit code = !CODE!
    if not "!CODE!"=="0" if "!RC!"=="0" set "RC=!CODE!"
    set "STELQUICK_A2_CHECK="
    set "STELQUICK_GRAPHICS_API="
)

echo.
echo ============================================================
echo  EXIT CODE = !RC!
echo   0=pass   2=no-window   3=backend-not-vulkan
echo   4=window-test-fail      5=A2-probe-fail   6=grab-unavailable
echo ============================================================
echo [negctl] EXPECTED: EXIT CODE = 5   (d3d11 injected fault)
if "!RC!"=="5" (echo [negctl] RESULT: PASS - aggregation caught the fault) else (echo [negctl] RESULT: FAIL - aggregation did NOT catch the fault)

endlocal & exit /b %RC%

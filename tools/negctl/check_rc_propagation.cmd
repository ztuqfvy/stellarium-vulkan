@echo off
rem ============================================================================
rem  Regression check: the runner's PROCESS exit code must equal the code it PRINTS.
rem
rem  WHY THIS EXISTS
rem    2026-09-21, on Windows: run_autotest.cmd reached ":end" -> echo. -> pause ->
rem    endlocal -> ran off the end of the file. The trailing "echo." had already
rem    reset ERRORLEVEL to 0, so a fully red matrix printed "EXIT CODE = 2" and
rem    still RETURNED 0. Measured before/after:
rem        before: script printed EXIT CODE = 2, process return code = 0
rem        after : script printed EXIT CODE = 2, process return code = 2
rem    That matters because tools/evidence/collect.ps1 fills its "dut result"
rem    header line from this process code, so the field could never go red -- a
rem    permanent green light one level above the one d717056 fixed. Fixed by
rem    propagating RC: "endlocal & exit /b %RC%" at ":end".
rem
rem    tools/negctl/run_negctl.cmd covers a DIFFERENT thing: whether the
rem    aggregation logic notices a failing backend. It uses a replica of the logic
rem    and ends with its own "exit /b", which is exactly why it did NOT catch this.
rem    This check therefore runs the REAL run_autotest.cmd.
rem
rem  HOW
rem    Plant a stub DUT that exits non-zero, so the real runner aggregates a red
rem    code without needing a real backend failure. Any exe that exits non-zero
rem    with no arguments works; findstr.exe exits 2 (measured, Windows 10 19045).
rem    The runner is COPIED next to the stub, so %~dp0 resolves there and the real
rem    run_autotest.cmd (not a replica) does the aggregation.
rem
rem  USAGE
rem    tools\negctl\check_rc_propagation.cmd                        (runner = repo root copy)
rem    tools\negctl\check_rc_propagation.cmd <path to run_autotest.cmd>
rem    Exit 0 = PASS, 1 = FAIL. ASCII ONLY, like every other .cmd in this repo.
rem ============================================================================
setlocal EnableDelayedExpansion

set "ROOT=%~dp0..\.."
set "RUNNER=%~1"
if "%RUNNER%"=="" set "RUNNER=%ROOT%\run_autotest.cmd"
if not exist "%RUNNER%" (
    echo [rcprop] FAIL - runner not found: %RUNNER%
    endlocal & exit /b 1
)

set "WORK=%ROOT%\build-ui\_rcprop"
if exist "%WORK%" rd /s /q "%WORK%" >nul 2>&1
mkdir "%WORK%\build-ui\deploy" 2>nul
copy /y "%RUNNER%" "%WORK%\run_autotest.cmd" >nul
copy /y "%SystemRoot%\System32\findstr.exe" "%WORK%\build-ui\deploy\stelQuickUI.exe" >nul

call "%WORK%\run_autotest.cmd" matrix < nul > "%WORK%\out.txt" 2>&1
set "PROC=!ERRORLEVEL!"

set "PRINTED="
for /f "tokens=4" %%L in ('findstr /r /c:"^ EXIT CODE = " "%WORK%\out.txt"') do set "PRINTED=%%L"

echo [rcprop] runner  : %RUNNER%
echo [rcprop] stub DUT: findstr.exe copied as stelQuickUI.exe ^(exits 2^)
echo [rcprop] script printed EXIT CODE = !PRINTED!  /  process return code = !PROC!

set "RC=1"
if "!PRINTED!"=="2" if "!PROC!"=="2" (
    echo [rcprop] RESULT: PASS - process exit code matches the printed code
    set "RC=0"
) else (
    echo [rcprop] RESULT: FAIL - printed=!PRINTED! process=!PROC!  ^(red verdict not propagated^)
)

rd /s /q "%WORK%" >nul 2>&1
endlocal & exit /b %RC%

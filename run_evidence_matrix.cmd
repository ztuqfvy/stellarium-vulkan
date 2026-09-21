@echo off
rem ============================================================================
rem  Evidence collector: run the real acceptance matrix and capture raw stdout.
rem
rem  Usage:  run_evidence_matrix.cmd <output-file>
rem
rem  Why this wrapper exists:
rem    - run_autotest.cmd ends with 'pause'; calling it directly from automation
rem      would hang. This wrapper feeds < nul and terminates cleanly.
rem    - The matrix stdout must be preserved verbatim as an artifact. Re-assembling
rem      it in the calling shell loses/alters bytes (encoding round-trips), so we
rem      redirect inside cmd.exe instead.
rem
rem  ============================ HARD RULES ==================================
rem  RULE 1 - ASCII ONLY. This .cmd file must contain no non-ASCII bytes at all.
rem    cmd.exe parses .cmd files line by line using the OEM code page (936/GBK on
rem    zh-CN). UTF-8 encoded Chinese in this file gets mis-decoded; the mangled
rem    bytes can splice across lines, and the leftover fragments are then EXECUTED
rem    as commands. That was a real defect in the previous revision of this
rem    wrapper: it aborted before writing the output file and, when it did run,
rem    injected "'xxx' is not recognized as an internal or external command" noise
rem    into the captured evidence. Keep every comment in English/ASCII.
rem
rem  RULE 2 - do NOT call chcp inside the capture scope.
rem    chcp makes cmd.exe emit a localized copyright banner (CP936 bytes) into
rem    stdout. That mixes two different encodings into a stream that is otherwise
rem    pure UTF-8, making the evidence file impossible to decode with any single
rem    codec. The console code page must be set to 65001 by the PARENT process
rem    before this wrapper is invoked. See docs/WINDOWS_BUILD.zh_CN.md section 9.
rem  ==========================================================================
rem
rem  The env whitelist is enforced below: every STELQUICK_* variable is cleared
rem  first so the run starts from a clean environment (section 9 discipline).
rem ============================================================================
setlocal EnableDelayedExpansion

set "OUTFILE=%~1"
if "%OUTFILE%"=="" (
    echo [ERROR] usage: run_evidence_matrix.cmd ^<output-file^>
    exit /b 2
)

rem  Clean environment: drop every STELQUICK_* variable before the run.
for /f "delims==" %%V in ('set STELQUICK_ 2^>nul') do set "%%V="

call "%~dp0run_autotest.cmd" matrix < nul > "%OUTFILE%" 2>&1
set "RC=!ERRORLEVEL!"

echo.>> "%OUTFILE%"
echo [wrapper] run_autotest.cmd matrix returncode = !RC!>> "%OUTFILE%"

endlocal & exit /b %RC%

@echo off
rem ============================================================================
rem  Stub backend for the negative control. Simulates one A2 backend run.
rem  Usage: stub_backend.cmd <name> <exitcode>
rem  Prints a token line and exits with the requested code, so the caller can
rem  verify that !ERRORLEVEL! propagates correctly through 'call' and the
rem  surrounding for-block aggregation.
rem  ASCII ONLY -- see run_evidence_matrix.cmd RULE 1 for why.
rem ============================================================================
echo STUB: backend=%1 simulated=1
echo A2CHECK: VERDICT=SIMULATED
exit /b %2

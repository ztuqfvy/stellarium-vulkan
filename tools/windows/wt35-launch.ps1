# wt35-launch.ps1 -- launcher for the W-T35 suite batch (covers T34 + T35). ASCII-only.
#
# Why this file exists: the INTERACT probe needs WT35_PROBE_EXPECT in the
# environment, and the batch must run under schtasks /it (a GUI program started
# from an SSH session has no desktop session). Stuffing "cmd /c set X=... && ..."
# into schtasks /tr means three levels of quoting (zsh -> ssh -> schtasks), which
# is exactly where the W-T31 rounds lost time. One tiny versioned launcher costs
# nothing and is itself hashable for the evidence trail.
#
# The expectation below is a MEASURED platform fact, not a wish:
# W-T31/W-T32 measured Windows crosses at IT-05,IT-06,IT-13,IT-16,IT-17,IT-18
# (macOS: IT-06,IT-13,IT-14,IT-17,IT-18). With the window inactive,
# forceActiveFocus() cannot give keySink active focus here, so an injected key
# never reaches the QML chain. NEITHER T34 NOR T35 CHANGED INTERACTCHECK, so the
# same boundary must still hold -- asserting it is a regression check on the
# INSTRUMENT. If it drifts, that is a finding: either this box's environment
# moved or the instrument did.
#
#   schtasks /create /tn StelQC_t35w_suites /sc once /st 23:59 /it /f ^
#     /tr "powershell -NoProfile -ExecutionPolicy Bypass -File C:\temp\wt35-launch.ps1"
#   schtasks /run /tn StelQC_t35w_suites
#   schtasks /delete /tn StelQC_t35w_suites /f          <-- ALWAYS delete after
#
# !!! NAME PREFIX TRAP (measured 2026-09-29): the SCRIPTS in C:\temp are named
#     wt35-*.ps1 but the OUTPUTS they write are named t35w-*.txt. Mixing the two
#     cost a full round: `-File C:\temp\t35w-launch.ps1` (a name that does not
#     exist) makes powershell die BEFORE the script's first line, so NOTHING is
#     logged anywhere and schtasks only reports "last result = -196608". A
#     missing -File target produces no log, no SUMMARY, no partial output --
#     "$task ran and left zero evidence" is a MISSING FILE, check the path first.

$ErrorActionPreference = "Continue"

$env:WT35_PROBE_EXPECT = "IT-05,IT-06,IT-13,IT-16,IT-17,IT-18"

$SUITE = "C:\temp\wt35-suites.ps1"
$LOG   = "C:\temp\t35w-launch.log"

"wt35-launch.ps1 : suite=$SUITE  WT35_PROBE_EXPECT=$($env:WT35_PROBE_EXPECT)  at $(Get-Date -Format o)" |
    Out-File -Encoding ascii $LOG
if (-not (Test-Path $SUITE)) {
    "FATAL suite script not found: $SUITE" | Out-File -Encoding ascii -Append $LOG
    exit 96
}

# !!! `*>> $LOG` CORRUPTS THE CHILD'S OUTPUT -- MEASURED 2026-09-30, THIS RUN !!!
#     The first revision ended with
#         & powershell ... -File $SUITE *>> $LOG
#     and the 242-byte log it produced contains, between the header and the
#     "launcher exit=0" marker, only a column of "0" NUL-padded triples -- the
#     child's entire console output was destroyed (PS 5.1 re-encodes redirected
#     text through the console code page and writes UTF-16LE). The batch itself
#     was unaffected because the suite script writes SUMMARY.txt straight to disk
#     with Out-File, so this was a loss of a convenience log, not of evidence.
#     FIX: Start-Process with -RedirectStandardOutput/-RedirectStandardError --
#     those write the RAW byte stream, no transcoding. Same lesson as trap 11 in
#     the uu-remote-windows skill; it was already written down and still got hit.
#     NOTE: this fix was NOT exercised by the 2026-09-30 batch (which ran the
#     $LASTEXITCODE revision, md5 c20653ec8f23f298cc83116bd33626fd).
$OUT = "C:\temp\t35w-launch.out.txt"
$ERR = "C:\temp\t35w-launch.err.txt"

$p = Start-Process -FilePath "powershell.exe" `
     -ArgumentList @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $SUITE) `
     -NoNewWindow -PassThru -Wait `
     -RedirectStandardOutput $OUT -RedirectStandardError $ERR
$rc = $p.ExitCode

"launcher child output: $OUT (stdout) / $ERR (stderr)  -- raw bytes, no transcoding" |
    Out-File -Encoding ascii -Append $LOG
"launcher exit=$rc" | Out-File -Encoding ascii -Append $LOG
exit $rc

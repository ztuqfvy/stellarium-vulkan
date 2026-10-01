# wt37-41-launch.ps1 -- launcher for the W-T37..W-T41 suite batch. ASCII-only.
#
# Why this file exists: the batch must run under schtasks /it (a GUI program
# started from an SSH session has no desktop session, so the Qt scene graph
# never initializes). Keeping the schtasks /tr argument to a single -File call
# avoids three levels of quoting (zsh -> ssh -> schtasks) -- that is exactly
# where the W-T31 rounds lost time.
#
# Child output goes through Start-Process -RedirectStandardOutput/-Error, which
# writes the RAW byte stream. Do NOT use `*>> $LOG`: PS 5.1 re-encodes redirected
# text through the console code page and writes UTF-16LE, which DESTROYED the
# child's console output during W-T35 (242-byte log of NUL-padded triples).
#
#   schtasks /create /tn StelQC_t37w_suites /sc once /st 23:59 /it /f ^
#     /tr "powershell -NoProfile -ExecutionPolicy Bypass -File C:\temp\wt37-41-launch.ps1"
#   schtasks /run /tn StelQC_t37w_suites
#   schtasks /delete /tn StelQC_t37w_suites /f          <-- ALWAYS delete after
#
# !!! NAME PREFIX TRAP (measured 2026-09-29): the SCRIPTS in C:\temp are named
#     wt37-*.ps1 but the OUTPUTS they write are named t37w-*.txt. A `-File` path
#     that does not exist makes powershell die BEFORE the script's first line, so
#     NOTHING is logged anywhere and schtasks only reports "last result =
#     -196608". "$task ran and left zero evidence" is usually a MISSING FILE --
#     check the path first.

$ErrorActionPreference = "Continue"

$SUITE = "C:\temp\wt37-41-suites.ps1"
$LOG   = "C:\temp\t37w-launch.log"

"wt37-41-launch.ps1 : suite=$SUITE  at $(Get-Date -Format o)" |
    Out-File -Encoding ascii $LOG
if (-not (Test-Path $SUITE)) {
    "FATAL suite script not found: $SUITE" | Out-File -Encoding ascii -Append $LOG
    exit 96
}

$OUT = "C:\temp\t37w-launch.out.txt"
$ERR = "C:\temp\t37w-launch.err.txt"

$p = Start-Process -FilePath "powershell.exe" `
     -ArgumentList @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $SUITE) `
     -NoNewWindow -PassThru -Wait `
     -RedirectStandardOutput $OUT -RedirectStandardError $ERR
$rc = $p.ExitCode

"launcher child output: $OUT (stdout) / $ERR (stderr)  -- raw bytes, no transcoding" |
    Out-File -Encoding ascii -Append $LOG
"launcher exit=$rc" | Out-File -Encoding ascii -Append $LOG
exit $rc

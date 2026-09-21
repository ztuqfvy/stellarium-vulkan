# collect.ps1 - assemble a self-describing acceptance evidence file for stelQuickUI.
#
# WHY THIS SCRIPT EXISTS
#   The evidence file committed on 2026-09-21 (docs/evidence/*.txt) carries a header
#   (commit / host / CPU / GPU / exe size / exe sha256) and a trailing negative-control
#   segment. Those parts were injected by an ad-hoc command that was never committed,
#   so the artifact could not be reproduced by anyone else. This script is the single
#   canonical generator: it emits the WHOLE file -- header, verbatim matrix stdout,
#   verbatim negative-control stdout, footer.
#
# DESIGN RULES (both learned the hard way; details in docs/evidence/README.md)
#   RULE 1  This file is ASCII ONLY, for the same reason .cmd files are: any non-ASCII
#           byte risks being mis-decoded by the toolchain that reads the file. The
#           emitted evidence is UTF-8 without BOM, written via .NET so no BOM sneaks in.
#   RULE 2  chcp is called HERE, in the parent, before any capture starts. Calling chcp
#           inside a capture scope injects cmd.exe's localized copyright banner (CP936
#           bytes) into an otherwise pure UTF-8 stream and makes the file undecodable.
#
# USAGE (plain PowerShell from the repo root; no Qt/VS environment needed):
#   powershell -ExecutionPolicy Bypass -File tools\evidence\collect.ps1
#   powershell -ExecutionPolicy Bypass -File tools\evidence\collect.ps1 -OutFile docs\evidence\custom.txt
#
# VERIFICATION STATUS (2026-09-21)
#   The assembly path -- header, verbatim segments, completeness checks, footer, and
#   the UTF-8-no-BOM write -- has now actually been EXECUTED on macOS under PowerShell
#   7.6.6, with the three Windows-only calls (Get-CimInstance, nvidia-smi, cmd /c)
#   stubbed out. File format and every check below are therefore exercised, not
#   merely reviewed.
#   Still UNVERIFIED, because it cannot be exercised off-Windows: the chcp/console
#   interaction and the `cmd /c "... > file 2>&1"` capture lines. Treat the first real
#   run as a test of exactly those parts; do not file that first output as evidence.

param(
    [string]$OutFile = "",
    [switch]$SkipNegctl
)

$ErrorActionPreference = "Stop"

# tools/evidence/collect.ps1 -> repo root is two levels up
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location $repoRoot

if ($OutFile -eq "") {
    $OutFile = Join-Path "docs\evidence" ("{0}-run_autotest-matrix-windows-vulkan-d3d11-opengl.txt" -f (Get-Date -Format "yyyy-MM-dd"))
}

# Locate the device under test. build-ui\deploy is the windeployqt staging dir, but
# the plain target output is equally valid here: Vulkan comes from the NVIDIA driver
# (System32\vulkan-1.dll) and the Qt DLLs come from PATH. The deploy target only
# exists at all when windeployqt was found at configure time, so hard-requiring it
# would turn "windeployqt missing" into a wasted round trip. Try in order.
$exeCandidates = @(
    "build-ui\deploy\stelQuickUI.exe",
    "build-ui\Release\stelQuickUI.exe",
    "build-ui\Debug\stelQuickUI.exe",
    "build-ui\stelQuickUI.exe"
)
$exe = $null
$exeRel = $null
foreach ($candidate in $exeCandidates) {
    $probePath = Join-Path $repoRoot $candidate
    if (Test-Path $probePath) { $exe = $probePath; $exeRel = $candidate; break }
}
if (-not $exe) {
    throw ("exe not found. tried:`n  " + ($exeCandidates -join "`n  ") +
           "`nbuild first:  cmake --build build-ui --config Release" +
           "`n(or, for a self-contained dir:  cmake --build build-ui --config Release --target deploy)")
}

# ---- RULE 2: code page is set by the PARENT, before any capture -----------------
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
chcp 65001 | Out-Null

# ---- metadata for the header ----------------------------------------------------
$commit  = (& git rev-parse HEAD).Trim()
# The evidence file this script writes lives under docs/evidence/ and is committed a
# moment later. Counting it as "dirty" would make this flag read YES from the SECOND
# run onwards, forever -- a constant, i.e. a dead alarm that trains the reader to skip
# the one line that says "the tested tree was not clean". Exclude that directory, and
# state the exclusion on the line itself so it is visible rather than silent.
$dirtyLines = @(@(& git status --porcelain) | Where-Object { $_ -notmatch 'docs/evidence/' })
if (@($dirtyLines).Count -gt 0) {
    $dirty = "YES -- $(@($dirtyLines).Count) path(s) differ from HEAD; see 'git status'"
} else {
    $dirty = "no  (docs/evidence/ excluded: that is this script's own output dir)"
}
$host_   = $env:COMPUTERNAME
$osObj   = Get-CimInstance Win32_OperatingSystem
$os      = "$($osObj.Caption) $($osObj.Version)"
$cpu     = (Get-CimInstance Win32_Processor | Select-Object -First 1).Name
$gpu     = "unknown"
try {
    $nv = & nvidia-smi --query-gpu=name,driver_version,memory.total --format=csv,noheader 2>$null
    if ($LASTEXITCODE -eq 0 -and $nv) { $gpu = ($nv | Select-Object -First 1).Trim() }
} catch { }
if ($gpu -eq "unknown") {
    try {
        $vc = Get-CimInstance Win32_VideoController | Select-Object -First 1
        $gpu = "$($vc.Name) driver=$($vc.DriverVersion)"
    } catch { }
}
$exeLen  = (Get-Item $exe).Length
$exeHash = (Get-FileHash $exe -Algorithm SHA256).Hash

# ---- acceptance discipline: refuse to run with forbidden escape hatches ---------
$forbidden = @()
if (Test-Path Env:STELQUICK_A2_IGNORE_SGWAIT)      { $forbidden += "STELQUICK_A2_IGNORE_SGWAIT" }
if ($env:STELQUICK_RENDER_WORKAROUND -eq "basic-loop") { $forbidden += "STELQUICK_RENDER_WORKAROUND=basic-loop" }
if ($forbidden.Count -gt 0) {
    throw "forbidden acceptance variable(s) set: $($forbidden -join ', ')  -- see docs/WINDOWS_BUILD.zh_CN.md section 9"
}

$tmpMatrix = [System.IO.Path]::GetTempFileName()
$tmpNegctl = [System.IO.Path]::GetTempFileName()

Write-Host "[1/3] capturing real matrix (vulkan / d3d11 / opengl) ..."
cmd /c "run_autotest.cmd matrix < nul > `"$tmpMatrix`" 2>&1"
$matrixRc = $LASTEXITCODE

$negctlRc = $null
if (-not $SkipNegctl) {
    Write-Host "[2/3] capturing negative control (injected d3d11 -> 5) ..."
    cmd /c "tools\negctl\run_negctl.cmd < nul > `"$tmpNegctl`" 2>&1"
    $negctlRc = $LASTEXITCODE
} else {
    Write-Host "[2/3] negative control skipped (-SkipNegctl) ..."
}

Write-Host "[3/3] assembling $OutFile ..."

# ---- completeness / control checks ------------------------------------------------
# The generator must be able to say NO. An evidence file that merely *looks* like a
# completed run is worse than no file, because it gets committed and cited. So check
# the shape of both captured segments and stamp the verdict into the header.
# NOTE: assign ReadAllLines() directly -- do NOT wrap in @(). @() re-types the
# result to Object[], and List[string].AddRange() then refuses the binding
# ("Cannot convert System.Object[] to IEnumerable[string]"). ReadAllLines already
# returns string[], including an empty one for an empty file.
$matrixLines = [System.IO.File]::ReadAllLines($tmpMatrix)
$negctlLines = @()
$problems = @()

foreach ($backend in @("vulkan", "d3d11", "opengl")) {
    if (-not ($matrixLines | Where-Object { $_ -like "*########## A2 / $backend ##########*" })) {
        $problems += "segment 1 has no 'A2 / $backend' section (matrix did not run to completion)"
    }
}
if (-not ($matrixLines | Where-Object { $_ -like "*EXIT CODE =*" })) {
    $problems += "segment 1 has no 'EXIT CODE =' line (run_autotest.cmd never reached its report stage)"
}

if (-not $SkipNegctl) {
    $negctlLines = [System.IO.File]::ReadAllLines($tmpNegctl)
    if ($negctlRc -ne 5) {
        $problems += "negative control returned $negctlRc, expected 5 (aggregation cannot be shown to go red)"
    }
    if (-not ($negctlLines | Where-Object { $_ -like "*RESULT: PASS*" })) {
        $problems += "negative control never printed 'RESULT: PASS'"
    }
} else {
    $problems += "negative control skipped (-SkipNegctl): NOT a complete acceptance record"
}

if ($problems.Count -eq 0) {
    $verdict = "COMPLETE - trustworthy record; the DUT verdict is SEGMENT 1's own returncode"
} else {
    $verdict = "NOT USABLE AS EVIDENCE - " + ($problems -join "; ")
}

$rule = "=" * 80
$out = New-Object System.Collections.Generic.List[string]

$out.Add($rule)
$out.Add(" stelQuickUI acceptance evidence -- raw stdout")
$out.Add(" (both body segments are machine-captured verbatim; this header/footer is script-injected)")
$out.Add($rule)
$out.Add(" VERDICT     : $verdict")
$out.Add($rule)
$out.Add(" commit      : $commit")
$out.Add(" worktree    : dirty = $dirty")
$out.Add(" date        : $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss zzz')")
$out.Add(" host        : $host_  /  $os")
$out.Add(" CPU         : $cpu")
$out.Add(" GPU         : $gpu")
$out.Add(" dut exe     : $exeRel  ($exeLen bytes)")
$out.Add(" sha256      : $exeHash")
$out.Add(" dut result  : run_autotest.cmd matrix returncode = $matrixRc")
$out.Add(" negctl      : " + $(if ($SkipNegctl) { "SKIPPED (-SkipNegctl)" } else { "returncode = $negctlRc  (5 expected)" }))
$out.Add(" code page   : 65001 set by parent before capture (no chcp inside the capture scope)")
$out.Add(" generator   : tools/evidence/collect.ps1")
$out.Add(" env policy  : whitelist only (STELQUICK_A2_CHECK / STELQUICK_GRAPHICS_API);")
$out.Add("               forbidden escape hatches checked and refused before the run")
$out.Add($rule)
$out.Add("")
$out.Add("SEGMENT 1 -- run_autotest.cmd matrix (real backends), returncode = $matrixRc")
$out.Add("")
$out.AddRange($matrixLines)

if (-not $SkipNegctl) {
    $out.Add("")
    $out.Add($rule)
    $out.Add("SEGMENT 2 -- exit-code aggregation negative control, returncode = $negctlRc")
    $out.Add(" expected: aggregation reports EXIT CODE = 5 (d3d11 fault injected).")
    $out.Add(" a green matrix is only meaningful if this control can go red.")
    $out.Add($rule)
    $out.Add("")
    $out.AddRange($negctlLines)
}

$out.Add("")
$out.Add("INTERPRETATION")
$out.Add("  matrix returncode 0  => all three backends passed the 12-probe pixel check.")
$out.Add("  negctl returncode 5  => the aggregation really does report failure when a")
$out.Add("                          backend fails; the green light above is not a default.")
$out.Add("  The VERDICT line at the top re-derives both properties from the raw text, so")
$out.Add("  trust it over a human reading of the segments. If it is not COMPLETE, do NOT")
$out.Add("  report this file as evidence -- it records a run that did not finish.")

$enc = New-Object System.Text.UTF8Encoding($false)   # UTF-8, no BOM
$outDir = Split-Path -Parent $OutFile
if ($outDir -and -not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir | Out-Null }
[System.IO.File]::WriteAllLines($OutFile, $out, $enc)

Remove-Item $tmpMatrix, $tmpNegctl -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "written     : $OutFile"
Write-Host "evidence    : $verdict"
Write-Host "dut matrix  : returncode = $matrixRc"
if (-not $SkipNegctl) { Write-Host "negctl      : returncode = $negctlRc" }
Write-Host "next        : decode check must pass, e.g."
Write-Host "              python -c `"open(r'$OutFile','rb').read().decode('utf-8'); print('utf-8 ok')`""
Write-Host ""
Write-Host "NOTE: this script's own exit code reports whether a TRUSTWORTHY RECORD was"
Write-Host "      produced -- it is NOT the DUT verdict. Read 'dut matrix' for that."
Write-Host ""

# Exit 0 = trustworthy record produced (even if the DUT itself failed -- a red matrix
# is a perfectly good finding). Exit 1 = the run did not complete, do not cite it.
if ($problems.Count -eq 0) { exit 0 } else { exit 1 }

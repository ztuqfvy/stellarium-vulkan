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
# NOT VERIFIED ON macOS: this script was authored on macOS (no PowerShell available).
# Syntax is reviewed but it has never been executed. Run it once on Windows and fix
# whatever breaks before trusting the output; do not treat the first run as evidence.

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

$exe = Join-Path $repoRoot "build-ui\deploy\stelQuickUI.exe"
if (-not (Test-Path $exe)) {
    throw "exe not found: $exe  -- build first: cmake --build build-ui --config Release"
}

# ---- RULE 2: code page is set by the PARENT, before any capture -----------------
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
chcp 65001 | Out-Null

# ---- metadata for the header ----------------------------------------------------
$commit  = (& git rev-parse HEAD).Trim()
$dirty   = if ((& git status --porcelain)) { "YES (uncommitted changes present!)" } else { "no" }
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

$rule = "=" * 80
$out = New-Object System.Collections.Generic.List[string]

$out.Add($rule)
$out.Add(" stelQuickUI acceptance evidence -- raw stdout")
$out.Add(" (both body segments are machine-captured verbatim; this header/footer is script-injected)")
$out.Add($rule)
$out.Add(" commit      : $commit")
$out.Add(" worktree    : dirty = $dirty")
$out.Add(" date        : $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss zzz')")
$out.Add(" host        : $host_  /  $os")
$out.Add(" CPU         : $cpu")
$out.Add(" GPU         : $gpu")
$out.Add(" dut exe     : build-ui/deploy/stelQuickUI.exe  ($exeLen bytes)")
$out.Add(" sha256      : $exeHash")
$out.Add(" code page   : 65001 set by parent before capture (no chcp inside the capture scope)")
$out.Add(" generator   : tools/evidence/collect.ps1")
$out.Add(" env policy  : whitelist only (STELQUICK_A2_CHECK / STELQUICK_GRAPHICS_API);")
$out.Add("               forbidden escape hatches checked and refused before the run")
$out.Add($rule)
$out.Add("")
$out.Add("SEGMENT 1 -- run_autotest.cmd matrix (real backends), returncode = $matrixRc")
$out.Add("")
$out.AddRange([System.IO.File]::ReadAllLines($tmpMatrix))

if (-not $SkipNegctl) {
    $out.Add("")
    $out.Add($rule)
    $out.Add("SEGMENT 2 -- exit-code aggregation negative control, returncode = $negctlRc")
    $out.Add(" expected: aggregation reports EXIT CODE = 5 (d3d11 fault injected).")
    $out.Add(" a green matrix is only meaningful if this control can go red.")
    $out.Add($rule)
    $out.Add("")
    $out.AddRange([System.IO.File]::ReadAllLines($tmpNegctl))
}

$out.Add("")
$out.Add("INTERPRETATION")
$out.Add("  matrix returncode 0  => all three backends passed the 12-probe pixel check.")
$out.Add("  negctl returncode 5  => the aggregation really does report failure when a")
$out.Add("                          backend fails; the green light above is not a default.")
$out.Add("  If either line is unexpected, do NOT report this file as evidence.")

$enc = New-Object System.Text.UTF8Encoding($false)   # UTF-8, no BOM
$outDir = Split-Path -Parent $OutFile
if ($outDir -and -not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir | Out-Null }
[System.IO.File]::WriteAllLines($OutFile, $out, $enc)

Remove-Item $tmpMatrix, $tmpNegctl -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "written : $OutFile"
Write-Host "matrix  returncode = $matrixRc"
if (-not $SkipNegctl) { Write-Host "negctl  returncode = $negctlRc" }
Write-Host "next    : decode check must pass, e.g."
Write-Host "          python -c `"open(r'$OutFile','rb').read().decode('utf-8'); print('utf-8 ok')`""

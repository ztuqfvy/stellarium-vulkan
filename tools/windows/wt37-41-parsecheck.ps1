# wt37-41-parsecheck.ps1 -- PS 5.1 syntax check for the uploaded batch scripts.
#
# Why: PS 5.1 reads a BOM-less .ps1 through the ANSI codepage, so a single
# non-ASCII character can turn into a stray quote and produce a PARSE-ERR that
# only shows up AFTER a multi-minute build. This costs nothing and runs first.
#
#   $errs=$null is REQUIRED before [ref]$errs -- without it PowerShell throws
#   "cannot apply [ref] to a non-existent variable" AND THEN KEEPS GOING and
#   reports PARSE-OK, i.e. a false green. (Measured; noted in the skill.)
$ErrorActionPreference = "Continue"

$files = @(
    "C:\temp\wt37-41-suites.ps1",
    "C:\temp\wt37-41-build.ps1",
    "C:\temp\wt37-41-launch.ps1",
    "C:\temp\wt37-41-status.ps1"
)

$bad = 0
foreach ($f in $files) {
    if (-not (Test-Path $f)) {
        "MISSING   $f"
        $bad++
        continue
    }
    $errs = $null
    $null = [System.Management.Automation.Language.Parser]::ParseFile($f, [ref]$null, [ref]$errs)
    if ($errs -and $errs.Count -gt 0) {
        "PARSE-ERR $f  ($($errs.Count) error(s))"
        $errs | ForEach-Object {
            "    line {0}: {1}" -f $_.Extent.StartLineNumber, $_.Message
        }
        $bad++
    } else {
        "PARSE-OK  $f"
    }
}

""
"parsecheck: bad=$bad  (0 = all clean)"

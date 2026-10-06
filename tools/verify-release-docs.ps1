[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string]$Version,

    [switch]$RequireAccepted
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$readmePath = Join-Path $repoRoot "README.md"
$changelogPath = Join-Path $repoRoot "CHANGELOG.md"

foreach ($path in @($readmePath, $changelogPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required documentation file not found: $path"
    }
}

$readme = Get-Content -LiteralPath $readmePath -Raw
$changelog = Get-Content -LiteralPath $changelogPath -Raw
$expectedReleaseLine = "- MCP integration: **v$Version**"

if (-not $readme.Contains($expectedReleaseLine)) {
    throw "README current release does not identify v$Version."
}

$firstVersionHeading = [regex]::Match(
    $changelog,
    '(?m)^## v\d+\.\d+\.\d+\s*$'
)
$expectedHeading = "## v$Version"

if (-not $firstVersionHeading.Success) {
    throw "CHANGELOG contains no release heading."
}

if ($firstVersionHeading.Value.Trim() -ne $expectedHeading) {
    throw "Newest CHANGELOG release is '$($firstVersionHeading.Value.Trim())', expected '$expectedHeading'."
}

$escapedVersion = [regex]::Escape($Version)
$statusMatch = [regex]::Match(
    $changelog,
    "(?ms)^## v$escapedVersion\s*\r?\n\r?\n- Status:\s*([^\r\n]+)"
)

if (-not $statusMatch.Success) {
    throw "CHANGELOG v$Version has no status line."
}

$status = $statusMatch.Groups[1].Value.Trim()

if ($RequireAccepted -and $status -match '(?i)\bpending\b|\bin progress\b|\bready for .*build\b') {
    throw "CHANGELOG v$Version is not accepted: $status"
}

Write-Host "Release documentation verified for v$Version."
Write-Host "Status: $status"

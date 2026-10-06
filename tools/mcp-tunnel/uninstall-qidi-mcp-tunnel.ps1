param(
    [string] $TaskName = 'QIDI Studio MCP Tunnel',
    [switch] $KeepConfiguration
)

$ErrorActionPreference = 'Stop'
$installDirectory = Join-Path $env:LOCALAPPDATA 'QIDIStudio-MCP'

if (Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue) {
    Stop-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
    Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false
}
if (-not $KeepConfiguration -and (Test-Path -LiteralPath $installDirectory)) {
    Remove-Item -LiteralPath $installDirectory -Recurse -Force
}

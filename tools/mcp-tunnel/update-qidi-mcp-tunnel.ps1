param(
    [string] $TaskName = 'QIDI Studio MCP Tunnel'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$installDirectory = Join-Path $env:LOCALAPPDATA 'QIDIStudio-MCP'
$configPath = Join-Path $installDirectory 'tunnel.json'
$installedSupervisor = Join-Path $installDirectory 'qidi-mcp-tunnel.ps1'
$sourceSupervisor = Join-Path $PSScriptRoot 'qidi-mcp-tunnel.ps1'

if (-not (Test-Path -LiteralPath $configPath -PathType Leaf)) {
    throw "Existing tunnel configuration was not found: $configPath"
}
if (-not (Test-Path -LiteralPath $sourceSupervisor -PathType Leaf)) {
    throw "Updated tunnel supervisor was not found: $sourceSupervisor"
}

$task = Get-ScheduledTask -TaskName $TaskName -ErrorAction Stop
if ($task.State -eq 'Running') {
    Stop-ScheduledTask -TaskName $TaskName
    $stopDeadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        Start-Sleep -Milliseconds 200
        $task = Get-ScheduledTask -TaskName $TaskName -ErrorAction Stop
    } while ($task.State -eq 'Running' -and [DateTime]::UtcNow -lt $stopDeadline)
    if ($task.State -eq 'Running') {
        throw "Scheduled task did not stop within 10 seconds: $TaskName"
    }
}

Copy-Item -LiteralPath $sourceSupervisor -Destination $installedSupervisor -Force
Start-ScheduledTask -TaskName $TaskName

[pscustomobject]@{
    TaskName        = $TaskName
    ConfigPreserved = $true
    SupervisorPath  = $installedSupervisor
    StatusPath      = (Join-Path $installDirectory 'tunnel-status.json')
}

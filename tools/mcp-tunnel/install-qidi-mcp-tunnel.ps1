param(
    [Parameter(Mandatory = $true)]
    [string] $Executable,

    [Parameter(Mandatory = $true)]
    [string] $ArgumentLine,

    [string] $SecretPath,
    [string] $SecretEnvironmentVariable,
    [string] $ProfileName,
    [string] $TunnelId,
    [string] $TaskName = 'QIDI Studio MCP Tunnel',
    [int] $RestartDelaySeconds = 5
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$resolvedExecutable = (Get-Command $Executable -ErrorAction Stop).Source
$installDirectory = Join-Path $env:LOCALAPPDATA 'QIDIStudio-MCP'
$installedSupervisor = Join-Path $installDirectory 'qidi-mcp-tunnel.ps1'
$configPath = Join-Path $installDirectory 'tunnel.json'
New-Item -ItemType Directory -Path $installDirectory -Force | Out-Null
$sourceSupervisor = Join-Path $PSScriptRoot 'qidi-mcp-tunnel.ps1'
if (-not (Test-Path -LiteralPath $sourceSupervisor -PathType Leaf)) {
    throw "Tunnel supervisor was not found: $sourceSupervisor"
}
if ($SecretPath -and -not (Test-Path -LiteralPath $SecretPath -PathType Leaf)) {
    throw "Encrypted tunnel secret was not found: $SecretPath"
}
if (($SecretPath -and -not $SecretEnvironmentVariable) -or
    ($SecretEnvironmentVariable -and -not $SecretPath)) {
    throw 'SecretPath and SecretEnvironmentVariable must be provided together'
}
if ($SecretEnvironmentVariable -and $SecretEnvironmentVariable -notmatch '^[A-Za-z_][A-Za-z0-9_]*$') {
    throw "Invalid secret environment variable name: $SecretEnvironmentVariable"
}

Copy-Item -LiteralPath $sourceSupervisor -Destination $installedSupervisor -Force

$configuration = [ordered]@{
    executable            = $resolvedExecutable
    argument_line         = $ArgumentLine
    restart_delay_seconds = [Math]::Max(1, [Math]::Min($RestartDelaySeconds, 300))
}
if ($SecretPath) {
    $configuration.secret_path = (Resolve-Path -LiteralPath $SecretPath).Path
    $configuration.secret_environment_variable = $SecretEnvironmentVariable
}
if ($ProfileName) {
    $configuration.profile_name = $ProfileName
}
if ($TunnelId) {
    $configuration.tunnel_id = $TunnelId
}
$configuration | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $configPath -Encoding UTF8

$powerShell = (Get-Command powershell.exe -ErrorAction Stop).Source
$taskArguments = '-NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File "' + $installedSupervisor + '"'
$action = New-ScheduledTaskAction -Execute $powerShell -Argument $taskArguments
$trigger = New-ScheduledTaskTrigger -AtLogOn -User "$env:USERDOMAIN\$env:USERNAME"
$principal = New-ScheduledTaskPrincipal -UserId "$env:USERDOMAIN\$env:USERNAME" -LogonType Interactive -RunLevel Limited
Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger $trigger -Principal $principal -Force | Out-Null
Start-ScheduledTask -TaskName $TaskName

[pscustomobject]@{
    TaskName   = $TaskName
    ConfigPath = $configPath
    StatusPath = (Join-Path $installDirectory 'tunnel-status.json')
    Executable = $resolvedExecutable
}

param(
    [string] $ConfigPath = (Join-Path $env:LOCALAPPDATA 'QIDIStudio-MCP\tunnel.json'),
    [string] $StatusPath = (Join-Path $env:LOCALAPPDATA 'QIDIStudio-MCP\tunnel-status.json')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$heartbeatIntervalSeconds = 15

function Write-TunnelStatus {
    param(
        [string] $State,
        [Nullable[int]] $ProcessId,
        [Nullable[int]] $ExitCode,
        [string] $Message
    )

    $status = [ordered]@{
        schema_version             = 1
        state                      = $State
        updated_utc                = [DateTime]::UtcNow.ToString('o')
        supervisor_process_id      = $PID
        process_id                 = $ProcessId
        exit_code                  = $ExitCode
        heartbeat_interval_seconds = $heartbeatIntervalSeconds
        endpoint                   = 'http://127.0.0.1:8765/mcp'
        message                    = $Message
    }
    $temporaryStatusPath = "$StatusPath.tmp.$PID"
    $status | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $temporaryStatusPath -Encoding UTF8
    Move-Item -LiteralPath $temporaryStatusPath -Destination $StatusPath -Force
}

if (-not (Test-Path -LiteralPath $ConfigPath -PathType Leaf)) {
    throw "Tunnel configuration was not found: $ConfigPath"
}

$config = Get-Content -LiteralPath $ConfigPath -Raw | ConvertFrom-Json
if (-not $config.executable) {
    throw 'tunnel.json must define executable'
}

$command = Get-Command $config.executable -ErrorAction Stop
$argumentLine = if ($config.argument_line) { [string] $config.argument_line } else { '' }
$secretPathProperty = $config.PSObject.Properties['secret_path']
$secretEnvironmentVariableProperty = $config.PSObject.Properties['secret_environment_variable']
$secretPath = if ($null -ne $secretPathProperty) { [string] $secretPathProperty.Value } else { '' }
$secretEnvironmentVariable = if ($null -ne $secretEnvironmentVariableProperty) { [string] $secretEnvironmentVariableProperty.Value } else { '' }
if (($secretPath -and -not $secretEnvironmentVariable) -or
    ($secretEnvironmentVariable -and -not $secretPath)) {
    throw 'tunnel.json must define both secret_path and secret_environment_variable'
}
if ($secretEnvironmentVariable -and $secretEnvironmentVariable -notmatch '^[A-Za-z_][A-Za-z0-9_]*$') {
    throw "Invalid secret environment variable name: $secretEnvironmentVariable"
}
$runtimeSecret = $null
if ($secretPath) {
    if (-not (Test-Path -LiteralPath $secretPath -PathType Leaf)) {
        throw "Encrypted tunnel secret was not found: $secretPath"
    }
    $runtimeSecret = (Get-Content -LiteralPath $secretPath -Raw).Trim() | ConvertTo-SecureString
}
$restartDelay = if ($config.restart_delay_seconds) { [int] $config.restart_delay_seconds } else { 5 }
$restartDelay = [Math]::Max(1, [Math]::Min($restartDelay, 300))
$statusDirectory = Split-Path -Parent $StatusPath
New-Item -ItemType Directory -Path $statusDirectory -Force | Out-Null
$stdoutLogPath = Join-Path $statusDirectory 'tunnel-client.stdout.log'
$stderrLogPath = Join-Path $statusDirectory 'tunnel-client.stderr.log'

$child = $null
try {
    while ($true) {
        Write-TunnelStatus -State 'starting' -ProcessId $null -ExitCode $null -Message 'Starting tunnel companion'
        $start = @{
            FilePath    = $command.Source
            ArgumentList = $argumentLine
            PassThru    = $true
            WindowStyle = 'Hidden'
            RedirectStandardOutput = $stdoutLogPath
            RedirectStandardError = $stderrLogPath
        }
        if ($null -eq $runtimeSecret) {
            $child = Start-Process @start
        }
        else {
            $secretPointer = [IntPtr]::Zero
            $previousSecret = [Environment]::GetEnvironmentVariable($secretEnvironmentVariable, 'Process')
            try {
                $secretPointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($runtimeSecret)
                $secretValue = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($secretPointer)
                [Environment]::SetEnvironmentVariable($secretEnvironmentVariable, $secretValue, 'Process')
                $child = Start-Process @start
            }
            finally {
                [Environment]::SetEnvironmentVariable($secretEnvironmentVariable, $previousSecret, 'Process')
                $secretValue = $null
                if ($secretPointer -ne [IntPtr]::Zero) {
                    [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($secretPointer)
                }
            }
        }
        $childProcessId = $child.Id
        Write-TunnelStatus -State 'running' -ProcessId $childProcessId -ExitCode $null -Message 'Tunnel companion is running'
        while (-not $child.WaitForExit($heartbeatIntervalSeconds * 1000)) {
            Write-TunnelStatus -State 'running' -ProcessId $childProcessId -ExitCode $null -Message 'Tunnel companion heartbeat'
        }
        $exitCode = $child.ExitCode
        $child = $null
        Write-TunnelStatus -State 'restarting' -ProcessId $null -ExitCode $exitCode -Message "Tunnel exited; restarting in $restartDelay seconds"
        Start-Sleep -Seconds $restartDelay
    }
}
finally {
    if ($null -ne $child -and -not $child.HasExited) {
        Stop-Process -Id $child.Id -Force -ErrorAction SilentlyContinue
    }
    Write-TunnelStatus -State 'stopped' -ProcessId $null -ExitCode $null -Message 'Tunnel supervisor stopped'
}

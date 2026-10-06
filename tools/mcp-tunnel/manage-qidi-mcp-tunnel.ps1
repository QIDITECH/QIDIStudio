param(
    [ValidateSet('status', 'start', 'stop', 'restart', 'doctor', 'logs', 'open-ui')]
    [string] $Action = 'status',
    [string] $TaskName = 'QIDI Studio MCP Tunnel'
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
Set-StrictMode -Version Latest

$installDirectory = Join-Path $env:LOCALAPPDATA 'QIDIStudio-MCP'
$configPath = Join-Path $installDirectory 'tunnel.json'
$statusPath = Join-Path $installDirectory 'tunnel-status.json'
$localMcpUrl = 'http://127.0.0.1:8765/mcp'
$dashboardUrl = 'http://127.0.0.1:8080/ui'
$healthListenerAddress = '127.0.0.1'
$healthListenerPort = 8080

function Get-OptionalProperty([object] $Object, [string] $Name) {
    if ($null -eq $Object) { return $null }
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) { return $null }
    return $property.Value
}

function Get-TunnelState {
    $configuration = $null
    if (Test-Path -LiteralPath $configPath -PathType Leaf) {
        try { $configuration = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json } catch { $configuration = $null }
    }

    $task = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
    $taskInfo = if ($null -ne $task) { Get-ScheduledTaskInfo -TaskName $TaskName -ErrorAction SilentlyContinue } else { $null }
    $heartbeat = $null
    if (Test-Path -LiteralPath $statusPath -PathType Leaf) {
        try { $heartbeat = Get-Content -LiteralPath $statusPath -Raw | ConvertFrom-Json } catch { $heartbeat = $null }
    }

    $heartbeatAge = $null
    $heartbeatFresh = $false
    $heartbeatStateValue = Get-OptionalProperty $heartbeat 'state'
    $heartbeatUpdatedValue = Get-OptionalProperty $heartbeat 'updated_utc'
    $heartbeatIntervalValue = Get-OptionalProperty $heartbeat 'heartbeat_interval_seconds'
    $heartbeatState = if ($heartbeatStateValue) { [string] $heartbeatStateValue } else { 'missing' }
    if ($heartbeatUpdatedValue) {
        try {
            $heartbeatAge = [Math]::Max(0, [Math]::Round(([DateTime]::UtcNow - [DateTime]::Parse([string] $heartbeatUpdatedValue).ToUniversalTime()).TotalSeconds))
            $interval = if ($heartbeatIntervalValue) { [int] $heartbeatIntervalValue } else { 15 }
            $heartbeatFresh = $heartbeatAge -le [Math]::Max(45, $interval * 3)
        } catch { $heartbeatAge = $null; $heartbeatFresh = $false }
    }

    $configured = $null -ne $configuration
    $secretPath = Get-OptionalProperty $configuration 'secret_path'
    $tunnelId = Get-OptionalProperty $configuration 'tunnel_id'
    $profileName = Get-OptionalProperty $configuration 'profile_name'
    $credentialsConfigured = $configured -and $secretPath -and (Test-Path -LiteralPath ([string] $secretPath) -PathType Leaf)
    $taskState = if ($null -ne $task) { [string] $task.State } else { 'Not installed' }
    $healthy = $configured -and $taskState -eq 'Running' -and $heartbeatFresh -and $heartbeatState -eq 'running'

    [ordered]@{
        schema_version = 1
        ok = $true
        action = $Action
        configured = $configured
        credentials_configured = [bool] $credentialsConfigured
        task_installed = $null -ne $task
        task_state = $taskState
        last_task_result = if ($null -ne $taskInfo) { [int64] $taskInfo.LastTaskResult } else { $null }
        tunnel_state = $heartbeatState
        heartbeat_fresh = $heartbeatFresh
        heartbeat_age_seconds = $heartbeatAge
        healthy = $healthy
        tunnel_id = if ($tunnelId) { [string] $tunnelId } else { '' }
        profile_name = if ($profileName) { [string] $profileName } else { '' }
        local_mcp_url = $localMcpUrl
        dashboard_url = $dashboardUrl
        log_directory = $installDirectory
        message = if ($healthy) { 'Tunnel is connected' } elseif ($configured) { 'Tunnel is configured but not connected' } else { 'Tunnel is not configured' }
    }
}

function Write-Result([object] $Result, [int] $ExitCode = 0) {
    $Result | ConvertTo-Json -Depth 6 -Compress
    exit $ExitCode
}

function Assert-Installed {
    if (-not (Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue)) {
        throw "Tunnel scheduled task is not installed: $TaskName"
    }
}

function Get-ConfiguredExecutablePath {
    if (-not (Test-Path -LiteralPath $configPath -PathType Leaf)) { return '' }
    try {
        $configuration = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
        $executable = Get-OptionalProperty $configuration 'executable'
        if (-not $executable) { return '' }
        return [IO.Path]::GetFullPath([string] $executable)
    }
    catch { return '' }
}

function Get-TunnelProcessCandidates([int[]] $AdditionalProcessIds = @()) {
    $candidateIds = @($AdditionalProcessIds)
    if (Test-Path -LiteralPath $statusPath -PathType Leaf) {
        try {
            $heartbeat = Get-Content -LiteralPath $statusPath -Raw | ConvertFrom-Json
            $heartbeatProcessId = Get-OptionalProperty $heartbeat 'process_id'
            if ($heartbeatProcessId) { $candidateIds += [int] $heartbeatProcessId }
        }
        catch {}
    }

    $listeners = @(Get-NetTCPConnection -LocalAddress $healthListenerAddress -LocalPort $healthListenerPort -State Listen -ErrorAction SilentlyContinue)
    foreach ($listener in $listeners) {
        if ($listener.OwningProcess) { $candidateIds += [int] $listener.OwningProcess }
    }

    return @($candidateIds | Where-Object { $_ -gt 0 } | Select-Object -Unique)
}

function Test-ConfiguredTunnelProcess([int] $ProcessId, [string] $ConfiguredExecutablePath) {
    if ($ProcessId -le 0 -or -not $ConfiguredExecutablePath) { return $false }
    $process = Get-CimInstance Win32_Process -Filter "ProcessId = $ProcessId" -ErrorAction SilentlyContinue
    if ($null -eq $process -or -not $process.ExecutablePath) { return $false }
    try { $actualPath = [IO.Path]::GetFullPath([string] $process.ExecutablePath) }
    catch { return $false }
    return [String]::Equals($actualPath, $ConfiguredExecutablePath, [StringComparison]::OrdinalIgnoreCase)
}

function Stop-TunnelCompanionProcesses([int[]] $AdditionalProcessIds = @(), [int] $Seconds = 10) {
    $configuredExecutablePath = Get-ConfiguredExecutablePath
    if (-not $configuredExecutablePath) { throw 'Tunnel configuration does not contain a valid executable path' }

    $deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
    do {
        $candidateIds = @(Get-TunnelProcessCandidates -AdditionalProcessIds $AdditionalProcessIds)
        $verifiedIds = @($candidateIds | Where-Object {
            Test-ConfiguredTunnelProcess -ProcessId $_ -ConfiguredExecutablePath $configuredExecutablePath
        })
        foreach ($processId in $verifiedIds) {
            Stop-Process -Id $processId -Force -ErrorAction SilentlyContinue
        }
        if ($verifiedIds.Count -eq 0) { return }
        Start-Sleep -Milliseconds 200
        $AdditionalProcessIds = @()
    } while ([DateTime]::UtcNow -lt $deadline)

    $remainingIds = @(Get-TunnelProcessCandidates | Where-Object {
        Test-ConfiguredTunnelProcess -ProcessId $_ -ConfiguredExecutablePath $configuredExecutablePath
    })
    if ($remainingIds.Count) {
        throw "Tunnel companion did not stop: process $($remainingIds -join ', ')"
    }
}

function Write-StoppedHeartbeat {
    New-Item -ItemType Directory -Path $installDirectory -Force | Out-Null
    $heartbeat = [ordered]@{
        schema_version = 1
        state = 'stopped'
        updated_utc = [DateTime]::UtcNow.ToString('o')
        supervisor_process_id = $null
        process_id = $null
        exit_code = $null
        heartbeat_interval_seconds = 15
        endpoint = $localMcpUrl
        message = 'Tunnel stopped by Tunnel Manager'
    }
    $temporaryStatusPath = "$statusPath.tmp.$PID"
    $heartbeat | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $temporaryStatusPath -Encoding UTF8
    Move-Item -LiteralPath $temporaryStatusPath -Destination $statusPath -Force
}

function Stop-TunnelTaskAndCompanion {
    $knownProcessIds = @(Get-TunnelProcessCandidates)
    Stop-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
    Wait-ForTaskState -DesiredState 'Ready'
    Stop-TunnelCompanionProcesses -AdditionalProcessIds $knownProcessIds
    Write-StoppedHeartbeat
}

function Wait-ForTaskState([string] $DesiredState, [int] $Seconds = 10) {
    $deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
    do {
        Start-Sleep -Milliseconds 200
        $task = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
        $state = if ($null -ne $task) { [string] $task.State } else { 'Not installed' }
    } while ($state -ne $DesiredState -and [DateTime]::UtcNow -lt $deadline)
}

function Wait-ForHealthyHeartbeat([int] $Seconds = 15) {
    $deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
    do {
        if ((Get-TunnelState).healthy) { return }
        Start-Sleep -Milliseconds 300
    } while ([DateTime]::UtcNow -lt $deadline)
}

try {
    switch ($Action) {
        'status' { Write-Result (Get-TunnelState) }
        'start' {
            Assert-Installed
            $currentState = Get-TunnelState
            if (-not $currentState.healthy) {
                Stop-TunnelTaskAndCompanion
            }
            Start-ScheduledTask -TaskName $TaskName
            Wait-ForTaskState -DesiredState 'Running'
            Wait-ForHealthyHeartbeat
            $result = Get-TunnelState
            $result.message = 'Tunnel start requested'
            Write-Result $result
        }
        'stop' {
            Assert-Installed
            Stop-TunnelTaskAndCompanion
            $result = Get-TunnelState
            $result.message = 'Tunnel stopped'
            Write-Result $result
        }
        'restart' {
            Assert-Installed
            Stop-TunnelTaskAndCompanion
            Start-ScheduledTask -TaskName $TaskName
            Wait-ForTaskState -DesiredState 'Running'
            Wait-ForHealthyHeartbeat
            $result = Get-TunnelState
            $result.message = 'Tunnel restarted'
            Write-Result $result
        }
        'logs' {
            New-Item -ItemType Directory -Path $installDirectory -Force | Out-Null
            Start-Process explorer.exe -ArgumentList ('"' + $installDirectory + '"')
            $result = Get-TunnelState
            $result.message = 'Opened tunnel log directory'
            Write-Result $result
        }
        'open-ui' {
            Start-Process $dashboardUrl
            $result = Get-TunnelState
            $result.message = 'Opened tunnel dashboard'
            Write-Result $result
        }
        'doctor' {
            if (-not (Test-Path -LiteralPath $configPath -PathType Leaf)) { throw 'Tunnel is not configured' }
            $configuration = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
            $executable = Get-OptionalProperty $configuration 'executable'
            $profileName = Get-OptionalProperty $configuration 'profile_name'
            $secretPath = Get-OptionalProperty $configuration 'secret_path'
            $secretEnvironmentVariable = Get-OptionalProperty $configuration 'secret_environment_variable'
            if (-not $executable -or -not $profileName) { throw 'Tunnel configuration is incomplete' }
            if (-not $secretPath -or -not $secretEnvironmentVariable) { throw 'Tunnel credentials are not configured' }
            $secureSecret = (Get-Content -LiteralPath ([string] $secretPath) -Raw).Trim() | ConvertTo-SecureString
            $secretPointer = [IntPtr]::Zero
            $secretVariable = [string] $secretEnvironmentVariable
            $previousSecret = [Environment]::GetEnvironmentVariable($secretVariable, 'Process')
            try {
                $secretPointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secureSecret)
                $secretValue = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($secretPointer)
                [Environment]::SetEnvironmentVariable($secretVariable, $secretValue, 'Process')
                $doctorOutput = @(& ([string] $executable) doctor --profile ([string] $profileName) --explain 2>&1)
                $doctorExitCode = $LASTEXITCODE
            }
            finally {
                [Environment]::SetEnvironmentVariable($secretVariable, $previousSecret, 'Process')
                $secretValue = $null
                if ($secretPointer -ne [IntPtr]::Zero) { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($secretPointer) }
            }
            $doctorText = ($doctorOutput | Out-String).TrimEnd()
            $result = Get-TunnelState

            # QIDI Studio uses the Runtime API key at the tunnel boundary, so local
            # OAuth discovery metadata is optional. ChatGPT tunnel connections do
            # not require a local Codex plugin. A running tunnel also owns its health
            # listener by design. Reclassify only those exact findings and only after
            # verifying that the configured tunnel executable owns the listener.
            $failedChecksMatch = [regex]::Match($doctorText, '(?m)^FAILED_CHECKS\s+([^\r\n]+)\s*$')
            $failedChecks = @()
            if ($failedChecksMatch.Success) {
                $failedChecks = @($failedChecksMatch.Groups[1].Value -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ })
            }

            $oauthMetadataExpected = $failedChecks -contains 'oauth_metadata' -and
                $doctorText -match '(?m)^CHECK\s+mcp_server_reachable\s+PASS\b' -and
                $doctorText -match '(?m)^CHECK\s+oauth_metadata\s+FAIL\b'

            $codexPluginExpected = $failedChecks -contains 'codex_plugin' -and
                $doctorText -match '(?m)^CHECK\s+codex_plugin\s+FAIL\b'

            $verifiedHealthListener = $false
            if ($failedChecks -contains 'health_listener' -and $result.healthy) {
                $configuredExecutablePath = Get-ConfiguredExecutablePath
                $listeners = @(Get-NetTCPConnection -LocalAddress $healthListenerAddress -LocalPort $healthListenerPort -State Listen -ErrorAction SilentlyContinue)
                foreach ($listener in $listeners) {
                    if (Test-ConfiguredTunnelProcess -ProcessId ([int] $listener.OwningProcess) -ConfiguredExecutablePath $configuredExecutablePath) {
                        $verifiedHealthListener = $true
                        break
                    }
                }
            }
            $healthListenerExpected = $failedChecks -contains 'health_listener' -and
                $verifiedHealthListener -and
                $doctorText -match '(?m)^CHECK\s+health_listener\s+FAIL\b'

            $unexpectedChecks = @($failedChecks | Where-Object {
                ($_ -ne 'oauth_metadata' -or -not $oauthMetadataExpected) -and
                ($_ -ne 'codex_plugin' -or -not $codexPluginExpected) -and
                ($_ -ne 'health_listener' -or -not $healthListenerExpected)
            })
            $compatibilityOnly = $doctorExitCode -ne 0 -and
                $failedChecksMatch.Success -and
                $failedChecks.Count -gt 0 -and
                $unexpectedChecks.Count -eq 0
            $diagnosticsPassed = $doctorExitCode -eq 0 -or $compatibilityOnly

            if ($compatibilityOnly) {
                $summaryMatch = [regex]::Match($doctorText, '(?ms)\A.*?^EXIT_CODE\s+\d+\s*$')
                if ($summaryMatch.Success) { $doctorText = $summaryMatch.Value.TrimEnd() }
                if ($oauthMetadataExpected) {
                    $doctorText = [regex]::Replace($doctorText, '(?m)^CHECK\s+oauth_metadata\s+FAIL[^\r\n]*$', 'CHECK oauth_metadata       INFO optional for the QIDI API-key tunnel')
                }
                if ($codexPluginExpected) {
                    $doctorText = [regex]::Replace($doctorText, '(?m)^CHECK\s+codex_plugin\s+FAIL[^\r\n]*$', 'CHECK codex_plugin         INFO not required for ChatGPT tunnel connections')
                }
                if ($healthListenerExpected) {
                    $doctorText = [regex]::Replace($doctorText, '(?m)^CHECK\s+health_listener\s+FAIL[^\r\n]*$', 'CHECK health_listener      PASS active configured tunnel owns 127.0.0.1:8080')
                }
                $doctorText = [regex]::Replace($doctorText, '(?m)^RESULT\s+fail\s*$', 'RESULT pass')
                $doctorText = [regex]::Replace($doctorText, '(?m)^FAILED_CHECKS\s+[^\r\n]+$', 'COMPATIBILITY_NOTES ' + ($failedChecks -join ','))
                $doctorText = [regex]::Replace($doctorText, '(?m)^EXIT_CODE\s+\d+\s*$', "ORIGINAL_EXIT_CODE $doctorExitCode (expected compatibility findings only)")
            }

            $effectiveExitCode = if ($diagnosticsPassed) { 0 } else { $doctorExitCode }
            $result.ok = $diagnosticsPassed
            $result.doctor_exit_code = $effectiveExitCode
            $result.doctor_raw_exit_code = $doctorExitCode
            $result.doctor_output = $doctorText
            $result.message = if ($compatibilityOnly) { 'Diagnostics passed with QIDI compatibility notes' } elseif ($diagnosticsPassed) { 'Diagnostics passed' } else { 'Diagnostics reported a problem' }
            Write-Result $result $effectiveExitCode
        }
    }
}
catch {
    Write-Result ([ordered]@{
        schema_version = 1
        ok = $false
        action = $Action
        message = $_.Exception.Message
    }) 1
}

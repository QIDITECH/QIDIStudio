param(
    [string] $TunnelId,
    [Security.SecureString] $RuntimeApiKey,
    [string] $ProfileName = 'qidi-studio',
    [string] $McpServerUrl = 'http://127.0.0.1:8765/mcp',
    [string] $TaskName = 'QIDI Studio MCP Tunnel',
    [switch] $SkipBrowser,
    [switch] $SkipDoctor,
    [switch] $FromEnvironment,
    [switch] $ReuseExistingCredential
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$tunnelsUrl = 'https://platform.openai.com/settings/organization/tunnels'
$runtimeKeysUrl = 'https://platform.openai.com/settings/organization/api-keys'
$readyUrl = 'http://127.0.0.1:8080/readyz'
$uiUrl = 'http://127.0.0.1:8080/ui'
$clientHash = 'd893d8127eee35070d265c1be29bfe008f8d9fcb476e7febf56c8fdc6c0615c8'
$secretEnvironmentVariable = 'CONTROL_PLANE_API_KEY'
$sourceClient = Join-Path $PSScriptRoot 'tunnel-client.exe'
$installer = Join-Path $PSScriptRoot 'install-qidi-mcp-tunnel.ps1'
$installDirectory = Join-Path $env:LOCALAPPDATA 'QIDIStudio-MCP'
$installedClient = Join-Path $installDirectory 'tunnel-client.exe'
$secretPath = Join-Path $installDirectory 'tunnel-key.dpapi'
$configPath = Join-Path $installDirectory 'tunnel.json'

function Get-InstalledTunnelClientProcesses([string] $ExecutablePath) {
    $expectedPath = [IO.Path]::GetFullPath($ExecutablePath)
    return @(Get-CimInstance Win32_Process -Filter "Name = 'tunnel-client.exe'" -ErrorAction SilentlyContinue |
        Where-Object {
            if (-not $_.ExecutablePath) {
                $false
            }
            else {
                try {
                    [String]::Equals([IO.Path]::GetFullPath([string] $_.ExecutablePath),
                                     $expectedPath,
                                     [StringComparison]::OrdinalIgnoreCase)
                }
                catch {
                    $false
                }
            }
        })
}

if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'This guided setup is supported only on Windows'
}
if ($ProfileName -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]*$') {
    throw "Invalid profile name: $ProfileName"
}
if ($McpServerUrl -ne 'http://127.0.0.1:8765/mcp') {
    throw 'QIDI Studio MCP must remain bound to http://127.0.0.1:8765/mcp'
}
if (-not (Test-Path -LiteralPath $sourceClient -PathType Leaf)) {
    throw "Bundled tunnel client was not found: $sourceClient"
}
if (-not (Test-Path -LiteralPath $installer -PathType Leaf)) {
    throw "Tunnel installer was not found: $installer"
}
$actualHash = (Get-FileHash -LiteralPath $sourceClient -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actualHash -ne $clientHash) {
    throw "Bundled tunnel-client.exe checksum mismatch. Expected $clientHash; found $actualHash"
}

Write-Host ''
Write-Host 'QIDI Studio MCP - Connect to ChatGPT' -ForegroundColor Cyan
Write-Host 'This one-time setup creates the qidi-studio tunnel profile and starts it at Windows logon.'
Write-Host 'The QIDI MCP server remains private at 127.0.0.1.'
Write-Host ''

$existingConfiguration = $null
if (Test-Path -LiteralPath $configPath -PathType Leaf) {
    try {
        $existingConfiguration = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
    }
    catch {
        Write-Warning "Existing tunnel configuration could not be read and will be replaced: $($_.Exception.Message)"
    }
}

$reuseExistingProfile = $false
if (-not $TunnelId -and $null -ne $existingConfiguration -and
    $null -ne $existingConfiguration.PSObject.Properties['tunnel_id'] -and
    $null -ne $existingConfiguration.PSObject.Properties['profile_name'] -and
    $existingConfiguration.tunnel_id -and $existingConfiguration.profile_name) {
    $answer = Read-Host "Reuse the existing '$($existingConfiguration.profile_name)' tunnel profile? [Y/n]"
    if (-not $answer -or $answer -match '^[Yy]') {
        $TunnelId = [string] $existingConfiguration.tunnel_id
        $ProfileName = [string] $existingConfiguration.profile_name
        $reuseExistingProfile = $true
    }
}

if (-not $TunnelId) {
    if (-not $SkipBrowser) {
        Start-Process $tunnelsUrl
        Start-Process $runtimeKeysUrl
    }
    Write-Host 'Create or select a tunnel in the browser, and create a Runtime API key with Tunnels Read + Use.'
    $TunnelId = Read-Host 'Paste the tunnel ID'
}
if ($TunnelId -notmatch '^tunnel_[A-Za-z0-9_-]+$') {
    throw 'Tunnel ID must begin with tunnel_'
}
if ($FromEnvironment -and $ReuseExistingCredential) {
    throw 'FromEnvironment and ReuseExistingCredential cannot be used together'
}
if ($ReuseExistingCredential -and $null -eq $RuntimeApiKey) {
    $existingSecretPath = $secretPath
    if ($null -ne $existingConfiguration -and
        $null -ne $existingConfiguration.PSObject.Properties['secret_path'] -and
        $existingConfiguration.secret_path) {
        $existingSecretPath = [string] $existingConfiguration.secret_path
    }
    if (-not (Test-Path -LiteralPath $existingSecretPath -PathType Leaf)) {
        throw "Existing DPAPI-protected Runtime API key was not found: $existingSecretPath"
    }
    try {
        $RuntimeApiKey = (Get-Content -LiteralPath $existingSecretPath -Raw).Trim() | ConvertTo-SecureString
    }
    catch {
        throw "Existing Runtime API key could not be decrypted for the current Windows user: $($_.Exception.Message)"
    }
}
elseif ($FromEnvironment -and $null -eq $RuntimeApiKey) {
    $setupSecretVariable = 'QIDI_MCP_RUNTIME_API_KEY'
    $setupSecret = [Environment]::GetEnvironmentVariable($setupSecretVariable, 'Process')
    if (-not $setupSecret) {
        throw "$setupSecretVariable was not supplied to the setup process"
    }
    try {
        $RuntimeApiKey = ConvertTo-SecureString -String $setupSecret -AsPlainText -Force
    }
    finally {
        [Environment]::SetEnvironmentVariable($setupSecretVariable, $null, 'Process')
        $setupSecret = $null
    }
}
if ($null -eq $RuntimeApiKey) {
    $RuntimeApiKey = Read-Host 'Paste the Runtime API key (input is hidden)' -AsSecureString
}
if ($RuntimeApiKey.Length -lt 8) {
    throw 'Runtime API key was empty or too short'
}

$secretPointer = [IntPtr]::Zero
$previousSecret = [Environment]::GetEnvironmentVariable($secretEnvironmentVariable, 'Process')
try {
    $secretPointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($RuntimeApiKey)
    $secretValue = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($secretPointer)
    [Environment]::SetEnvironmentVariable($secretEnvironmentVariable, $secretValue, 'Process')

    if (-not $reuseExistingProfile) {
        Write-Host 'Creating the local tunnel profile...'
        & $sourceClient init --force --sample sample_mcp_remote_no_auth --profile $ProfileName --tunnel-id $TunnelId --mcp-server-url $McpServerUrl
        if ($LASTEXITCODE -ne 0) {
            throw "tunnel-client init failed with exit code $LASTEXITCODE"
        }
    }

    if (-not $SkipDoctor) {
        Write-Host 'Validating the tunnel profile and local MCP endpoint...'
        $doctorOutput = @(& $sourceClient doctor --profile $ProfileName --explain 2>&1)
        $doctorExitCode = $LASTEXITCODE
        $doctorText = ($doctorOutput | Out-String).TrimEnd()
        if ($doctorText) {
            Write-Host $doctorText
        }

        if ($doctorExitCode -ne 0) {
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
            if ($failedChecks -contains 'health_listener' -and $null -ne $existingConfiguration -and
                $null -ne $existingConfiguration.PSObject.Properties['executable'] -and
                $existingConfiguration.executable) {
                try {
                    $configuredExecutablePath = [IO.Path]::GetFullPath([string] $existingConfiguration.executable)
                    $listeners = @(Get-NetTCPConnection -LocalAddress '127.0.0.1' -LocalPort 8080 -State Listen -ErrorAction SilentlyContinue)
                    foreach ($listener in $listeners) {
                        $process = Get-CimInstance Win32_Process -Filter "ProcessId = $($listener.OwningProcess)" -ErrorAction SilentlyContinue
                        if ($null -ne $process -and $process.ExecutablePath -and
                            [String]::Equals([IO.Path]::GetFullPath([string] $process.ExecutablePath),
                                             $configuredExecutablePath,
                                             [StringComparison]::OrdinalIgnoreCase)) {
                            $verifiedHealthListener = $true
                            break
                        }
                    }
                }
                catch {
                    $verifiedHealthListener = $false
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
            $compatibilityOnly = $failedChecksMatch.Success -and
                $failedChecks.Count -gt 0 -and
                $unexpectedChecks.Count -eq 0
            if ($compatibilityOnly) {
                Write-Warning 'Doctor reported only expected QIDI/ChatGPT compatibility findings; continuing setup.'
            }
            else {
                throw "tunnel-client doctor failed with exit code $doctorExitCode. Make sure QIDI Studio is open and the MCP server is running."
            }
        }
    }
}
finally {
    [Environment]::SetEnvironmentVariable($secretEnvironmentVariable, $previousSecret, 'Process')
    $secretValue = $null
    if ($secretPointer -ne [IntPtr]::Zero) {
        [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($secretPointer)
    }
}

New-Item -ItemType Directory -Path $installDirectory -Force | Out-Null
$encryptedSecret = ConvertFrom-SecureString -SecureString $RuntimeApiKey
$temporarySecretPath = "$secretPath.tmp.$PID"
$encryptedSecret | Set-Content -LiteralPath $temporarySecretPath -Encoding ASCII -NoNewline

$existingTask = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
if ($null -ne $existingTask -and $existingTask.State -eq 'Running') {
    Stop-ScheduledTask -TaskName $TaskName
    $stopDeadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        Start-Sleep -Milliseconds 200
        $existingTask = Get-ScheduledTask -TaskName $TaskName -ErrorAction Stop
    } while ($existingTask.State -eq 'Running' -and [DateTime]::UtcNow -lt $stopDeadline)
    if ($existingTask.State -eq 'Running') {
        Remove-Item -LiteralPath $temporarySecretPath -Force -ErrorAction SilentlyContinue
        throw "Existing tunnel task did not stop within 10 seconds: $TaskName"
    }
}

# Stopping the scheduled PowerShell supervisor can leave its native child alive
# briefly (or orphaned after an interrupted repair). Terminate only processes
# whose executable path exactly matches the managed installed client before
# replacing that file.
$clientStopDeadline = [DateTime]::UtcNow.AddSeconds(10)
do {
    $installedClientProcesses = @(Get-InstalledTunnelClientProcesses -ExecutablePath $installedClient)
    foreach ($installedClientProcess in $installedClientProcesses) {
        Stop-Process -Id $installedClientProcess.ProcessId -Force -ErrorAction SilentlyContinue
    }
    if ($installedClientProcesses.Count -eq 0) {
        break
    }
    Start-Sleep -Milliseconds 200
} while ([DateTime]::UtcNow -lt $clientStopDeadline)

$remainingInstalledClientProcesses = @(Get-InstalledTunnelClientProcesses -ExecutablePath $installedClient)
if ($remainingInstalledClientProcesses.Count -gt 0) {
    Remove-Item -LiteralPath $temporarySecretPath -Force -ErrorAction SilentlyContinue
    throw "Existing tunnel client did not stop within 10 seconds: process $($remainingInstalledClientProcesses.ProcessId -join ', ')"
}

try {
    Copy-Item -LiteralPath $sourceClient -Destination $installedClient -Force
    Move-Item -LiteralPath $temporarySecretPath -Destination $secretPath -Force
    $installResult = & $installer `
        -Executable $installedClient `
        -ArgumentLine "run --profile $ProfileName" `
        -SecretPath $secretPath `
        -SecretEnvironmentVariable $secretEnvironmentVariable `
        -ProfileName $ProfileName `
        -TunnelId $TunnelId `
        -TaskName $TaskName
}
catch {
    Remove-Item -LiteralPath $temporarySecretPath -Force -ErrorAction SilentlyContinue
    throw
}

Write-Host 'Waiting for the background tunnel to become ready...'
$ready = $false
$readyDeadline = [DateTime]::UtcNow.AddSeconds(30)
do {
    try {
        $readyResponse = Invoke-WebRequest -Uri $readyUrl -UseBasicParsing -TimeoutSec 3
        $ready = $readyResponse.StatusCode -ge 200 -and $readyResponse.StatusCode -lt 300
    }
    catch {
        $ready = $false
    }
    if (-not $ready) {
        Start-Sleep -Milliseconds 500
    }
} while (-not $ready -and [DateTime]::UtcNow -lt $readyDeadline)

if (-not $ready) {
    $task = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
    $taskInfo = Get-ScheduledTaskInfo -TaskName $TaskName -ErrorAction SilentlyContinue
    $taskState = if ($null -ne $task) { [string] $task.State } else { 'NotFound' }
    $lastTaskResult = if ($null -ne $taskInfo) { [string] $taskInfo.LastTaskResult } else { 'Unknown' }
    throw "Tunnel background task did not become ready within 30 seconds. Task state: $taskState; last result: $lastTaskResult. Run $installDirectory\qidi-mcp-tunnel.ps1 directly for details."
}

if (-not $SkipBrowser) {
    Start-Process $uiUrl
}

Write-Host ''
Write-Host 'Connected.' -ForegroundColor Green
Write-Host "Profile: $ProfileName"
Write-Host "Local MCP: $McpServerUrl"
Write-Host "Background task: $TaskName"
Write-Host 'The Runtime API key is protected for the current Windows user with DPAPI.'
$installResult

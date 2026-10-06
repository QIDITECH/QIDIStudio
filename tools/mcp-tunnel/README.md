# QIDI Studio MCP tunnel companion

The MCP listener remains loopback-only at `http://127.0.0.1:8765/mcp`. The Windows build stages OpenAI's `tunnel-client.exe` and these scripts in the `mcp-tunnel` directory beside QIDI Studio. The tunnel runs independently through the current user's Task Scheduler, starts at logon, and restarts if it exits.

## Guided setup

1. Start QIDI Studio so `http://127.0.0.1:8765/mcp` is available.
2. Open the `mcp-tunnel` directory beside `qidi-studio.exe`.
3. Double-click `setup-qidi-mcp-tunnel.cmd`.
4. Create/select an OpenAI tunnel and a Runtime API key when the two browser pages open.
5. Paste the tunnel ID and Runtime API key into the setup window.

The wizard creates the `qidi-studio` profile, runs `doctor --explain`, installs the scheduled task, and starts the tunnel. The Runtime API key is encrypted for the current Windows user with DPAPI. It is never put in `tunnel.json`, the scheduled-task command line, QIDI Studio, or source control.

The installed files live in `%LOCALAPPDATA%\QIDIStudio-MCP`. The supervisor writes `tunnel-status.json` atomically and refreshes its heartbeat every 15 seconds while the client is running.

## QIDI Studio Tunnel Manager

In QIDI Studio MCP Edition v1.8.0 or later, open **MCP > Tunnel Manager**. The manager can configure or repair the tunnel, securely replace the Runtime API key, and start, stop, restart, diagnose, or inspect the companion without using PowerShell manually.

The API key is passed only in the setup process environment and is then stored with user-scoped Windows DPAPI. It is never placed in a command line, JSON file, log, or QIDI Studio preference.

When a DPAPI credential is already configured, leave the Runtime API key field empty and click **Save / Repair and Connect** to repair the client, supervisor, configuration, and scheduled task without replacing the key. Enter a Runtime API key only for first-time setup or intentional key replacement.

## Status

```powershell
Get-ScheduledTask -TaskName 'QIDI Studio MCP Tunnel'
Get-Content "$env:LOCALAPPDATA\QIDIStudio-MCP\tunnel-status.json"
```

QIDI Studio MCP v1.4.0 also exposes `get_tunnel_status`. It reports whether the companion is configured, whether the heartbeat is fresh, and a derived `healthy` value. It intentionally does not return the executable, argument line, tunnel ID, or credentials.

## Upgrade or repair

Rerun `setup-qidi-mcp-tunnel.cmd` to replace the bundled client, refresh the DPAPI-protected Runtime API key, and reuse the existing profile. To update only the heartbeat supervisor while preserving all existing configuration:

```powershell
.\update-qidi-mcp-tunnel.ps1
```

## Advanced provider-neutral installation

The lower-level installer remains available for another tunnel provider. Authentication must remain outside its executable argument line:

```powershell
.\install-qidi-mcp-tunnel.ps1 `
    -Executable 'C:\path\to\your-tunnel.exe' `
    -ArgumentLine 'provider arguments without credentials'
```

## Uninstall

```powershell
.\uninstall-qidi-mcp-tunnel.ps1
```

Do not expose port 8765 directly, change the C++ listener to a non-loopback address, put credentials in an argument line, or place a tunnel token in source control.

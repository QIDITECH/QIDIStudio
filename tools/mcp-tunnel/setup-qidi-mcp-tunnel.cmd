@echo off
setlocal
title QIDI Studio MCP Tunnel Setup
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup-qidi-mcp-tunnel.ps1"
set "QIDI_TUNNEL_SETUP_EXIT=%ERRORLEVEL%"
echo.
if not "%QIDI_TUNNEL_SETUP_EXIT%"=="0" (
    echo Setup did not complete. Review the error above.
) else (
    echo Setup completed successfully.
)
pause
exit /b %QIDI_TUNNEL_SETUP_EXIT%

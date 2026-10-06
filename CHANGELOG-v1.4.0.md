# QIDI Studio MCP v1.4.0 Changelog

Release date: 2026-08-08
Target: QIDI Studio 2.7.2.10
MCP tool count: 101

## Overview

v1.4.0 is the monitoring and observability release. It builds on the live-validated v1.3.3 print workflow by adding provider-neutral tunnel health reporting, synchronized printer-camera monitoring snapshots, and explicit printer case-light control.

The release remains intentionally human-in-the-loop. It observes print state and camera imagery but does not diagnose failures or automatically pause or cancel prints.

## Added

- Added `get_tunnel_status` for provider-neutral tunnel configuration state, supervisor state, heartbeat age, and derived healthy/stale reporting.
- Added `capture_print_monitor_snapshot`, returning a camera frame together with the corresponding printer state, active filename, progress, layers, duration, temperatures, fan state, speed, filament sensor state, case-light state, and capture timing.
- Added `set_printer_case_light` for explicit case-light on/off control with asynchronous telemetry confirmation.
- Added atomic tunnel-status writes and a 15-second supervisor heartbeat so a stale `running` record can be distinguished from a healthy live tunnel.
- Added `update-qidi-mcp-tunnel.ps1` to update an existing installed tunnel supervisor without changing or re-entering its provider configuration.

## Changed

- Increased the MCP suite from 98 to 101 tools.
- Updated MCP `serverInfo.version` and `get_suite_capabilities().suite_version` to `1.4.0`.
- Expanded suite capability reporting for tunnel health, observational print monitoring, and printer case-light control.
- Tunnel supervisor status is now refreshed periodically instead of relying on a potentially stale one-time `running` state.

## Safety and privacy

- `get_tunnel_status` does not return the configured tunnel executable, provider argument line, or provider credentials.
- Monitoring snapshots are observational only. Capturing a snapshot does not diagnose the image, pause the printer, cancel the job, or initiate another print-control action.
- Case-light control is explicit rather than an automatic print decision.
- The MCP listener remains bound to `127.0.0.1:8765`; public connectivity continues to be handled by the external tunnel companion.
- Automatic visual failure detection and automatic cancellation remain out of scope for v1.4.0.

## Validation

- Incremental v1.4.0 patch applies cleanly to the verified v1.3.3 source tree.
- Static checks confirm 101 unique MCP tool definitions and 101 matching dispatch branches.
- Patch reconstruction matches the intended v1.4.0 source byte-for-byte.
- Whitespace validation passes.
- Windows x64 `RelWithDebInfo` build completed successfully on 2026-08-08 in 00:44:03.68.
- v1.3.3 remains the live-validated functional baseline: native active-plate by-object auto-arrange, eight-instance sequential handling, sequential clearance validation, slicing, upload/start, printer telemetry confirmation, and cancel/stop were all exercised successfully on the QIDI Max 4.

### Live acceptance remaining

- Confirm v1.4.0 is exposed with 101 tools after launching the new executable.
- Validate `get_tunnel_status` against the running tunnel supervisor and heartbeat.
- Validate `set_printer_case_light` on/off against Max 4 telemetry.
- Validate repeated `capture_print_monitor_snapshot` calls for synchronized camera and printer telemetry.

## Deferred

- Automatic visual print-failure detection and cancellation.
- Cloud-only print start.
- AMS/QIDI Box remapping and control.
- Calibration execution.
- Adaptive layer height control.
- Painted support operations.
- Painted seam operations.

## Artifact

- `qidistudio-mcp-v1.4.0-monitoring-observability.patch`

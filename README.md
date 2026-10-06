![QIDI Studio logo](/resources/images/QIDIStudio.png?raw=true)

# QIDI Studio MCP Edition

QIDI Studio MCP Edition is a Windows community fork of
[QIDI Studio](https://github.com/QIDITECH/QIDIStudio) that adds a native Model
Context Protocol server. It lets ChatGPT inspect and operate the slicer through
explicit, structured tools instead of mouse and keyboard automation.

This project is not an official QIDI Technology product. For printer warranty,
hardware, account, or official QIDI Studio support, use
[QIDI Support](https://qidi3d.com/pages/warranty-policy-after-sales-support).

## Current release

- MCP integration: **v1.11.0**
- Advertised tools: **118**
- QIDI Studio target: **2.7.2.10**
- Validated platform: **Windows x64, RelWithDebInfo**
- Local MCP endpoint: `http://127.0.0.1:8765/mcp`

See [CHANGELOG.md](CHANGELOG.md) for release history and validation details.

## What the MCP edition adds

| Area | Capabilities |
| --- | --- |
| Project and models | Create, load, import ChatGPT-attached models, inspect, transform, arrange, orient, cut, split, merge, repair, and export projects and models. |
| Settings | Read and update print, filament, printer, object, and volume settings; inspect native metadata, inheritance, limits, and enum choices; preview proposed updates without mutation. |
| Slicing | Validate configurations, slice plates, inspect warnings and toolpaths, analyze the first layer, check filament quantity, and export G-code. |
| Advanced preparation | Adaptive layer height, support and seam facet painting, print-by-object ordering and clearance validation, orientation candidates, and printability analysis. |
| Printers | Discover printers, inspect capabilities and readiness, monitor jobs, capture camera frames, control the case light, and pause, resume, or cancel jobs. |
| Print workflow | Select and lock an explicit QIDI Box or external filament source before slicing, run preflight checks, and use a confirmation-gated local/LAN print-start workflow. |
| Recovery and visibility | Resolve recovery prompts, inspect UI state, capture Prepare or Preview in the background with OpenGL composition, display images directly in ChatGPT, and provide expiring local download links. |
| Connectivity | Manage the Secure MCP Tunnel from QIDI Studio, including setup, status, start, stop, restart, diagnostics, logs, and dashboard access. |

## Connect QIDI Studio to ChatGPT

### Requirements

- Windows x64
- QIDI Studio MCP Edition v1.8.0 or later
- A ChatGPT account or workspace that permits developer mode and custom MCP
  connections
- An OpenAI Secure MCP Tunnel ID and Runtime API key

### 1. Configure the tunnel

1. Start QIDI Studio MCP Edition.
2. Open **MCP > Tunnel Manager**.
3. Select **Open Tunnels Page** and create or choose a tunnel.
4. Select **Open Runtime Keys Page** and create a Runtime API key.
5. Paste the `tunnel_id` and Runtime API key into the manager.
6. Select **Save / Repair and Connect**.
7. Select **Run Diagnostics** and confirm the tunnel is healthy.

The tunnel runs as a per-user Windows scheduled task and can remain available
when QIDI Studio restarts. Use the same dialog to start, stop, restart, repair,
or inspect it. Detailed companion documentation is in
[tools/mcp-tunnel/README.md](tools/mcp-tunnel/README.md).

### 2. Add the connection in ChatGPT

1. In ChatGPT, open **Settings > Security and login** and enable **Developer
   mode** if your account or workspace requires it.
2. Open [ChatGPT Plugins](https://chatgpt.com/plugins) and select the plus button.
3. Enter a name such as `Qidi Studio MCP` and a short description.
4. Under **Connection**, choose **Tunnel**, then select the available tunnel or
   enter its `tunnel_id`.
5. Create the connection and confirm that 118 tools are discovered.
6. Start a new conversation and enable **Qidi Studio MCP** from the tools menu.

Example checks:

```text
@Qidi Studio MCP call get_suite_capabilities
@Qidi Studio MCP call get_project_state
@Qidi Studio MCP call list_setting_definitions with scope print and query layer height
```

If a release changes tool names, descriptions, or schemas, open the connection
at [ChatGPT Plugins](https://chatgpt.com/plugins), select **Refresh**, confirm the
new tool count, and start a new conversation. This follows the
[official OpenAI connection workflow](https://developers.openai.com/plugins/deploy/connect-chatgpt).

## Attachment-to-print workflow

Version 1.10.0 supports a guarded conversational workflow from a ChatGPT file
attachment through physical printing. Attached STL, 3MF, OBJ, AMF, STEP, STP,
and PLY files can be imported without first copying them to the QIDI Studio
computer.

The physical filament source must be chosen **before slicing**. The MCP does not
infer a QIDI Box bay or silently default to the external feeder.

### Filament numbering

- `project_filament_index` is zero-based within the QIDI Studio project.
- Object and volume `extruder` values are one-based. Project filament index `3`
  therefore uses extruder `4`.
- Physical `slot_id` is a zero-based global feeder index. QIDI Box slots are
  `0` through `15`; the external feeder is slot `16`.
- With one QIDI Box, its physically labeled bays 1 through 4 correspond to
  global slot IDs `0` through `3`.

For example, a red spool in the fourth bay of the first QIDI Box is physical
slot `3`. If its matching project filament is index `3`, the model must use
extruder `4` and be sliced after that assignment.

### Guarded sequence

1. Attach the model in ChatGPT and call `import_attached_models`, optionally
   with arrangement enabled.
2. Call `get_printer_details`, show the available QIDI Box and external sources,
   and ask the user which physical filament to use.
3. Assign every printable model part to the matching project filament with
   `set_object_extruder` or `set_volume_extruder`. These operations invalidate
   an older slice.
4. Review settings and slice the active plate.
5. Call `prepare_print_job` with the matching project and physical source. A
   QIDI Box example is:

   ```json
   {
     "device_id": "printer-device-id",
     "filament_source": {
       "project_filament_index": 3,
       "source": "qidi_box",
       "slot_id": 3
     },
     "bed_leveling": true,
     "timelapse": false
   }
   ```

   For the external feeder, use `"source": "external"`; physical slot `16` is
   derived automatically.
6. Review the returned summary and physical checks. Preparation rejects stale
   or mismatched slices and returns a short-lived, single-use confirmation
   token. It does not upload or print.
7. Only after explicit user approval, call `start_print_job` with that token and
   `confirm: true`.
8. Poll `get_print_job_status`. The previously loaded toolhead slot may remain
   `pending_physical_load` during layer-zero heating and QIDI Box exchange.
   Printing is confirmed only after telemetry reports both the matching job and
   selected physical slot. Cancellation is recommended if the first print layer
   begins while the slot still mismatches.

Guarded direct start currently supports exactly one sliced project filament.
Use QIDI Studio's native Send-to-Printer dialog for multi-filament jobs until a
complete per-filament physical mapping is available through MCP.

Example conversation requests:

```text
Import this attached model, arrange it, and report its dimensions and printability. Do not slice or print.

Use red PETG from QIDI Box physical slot 3. Assign the matching project filament, slice, run preflight, and prepare the print. Stop before start_print_job.

Use only the newest prepared token, start with confirmation, and monitor until the selected physical slot and printing are both confirmed.
```

## Background captures and inline images

Version 1.11.0 can capture QIDI Studio's Prepare or Preview view while the
application is minimized, hidden, or behind other windows. The native window
and active OpenGL canvas are captured separately and composited so the build
plate, models, toolpaths, and camera view remain visible.

`capture_studio_screenshot` accepts `target: "current"`, `"prepare"`, or
`"preview"`. Its `background` option defaults to `true`. After capture, QIDI
Studio restores the original window placement, visibility, minimized state,
and selected tab.

Compatible ChatGPT clients display Studio screenshots, printer-camera frames,
and monitoring snapshots directly in the conversation through the bundled MCP
App viewer. The PNG bytes remain inside the authenticated MCP result, so the
inline image can be viewed from another device, including a phone. The
short-lived `127.0.0.1` download link is only a fallback for the computer
running QIDI Studio and is not remotely reachable.

Capturing an image does not modify the project, change settings, slice, upload,
or print. During application startup, check `gl_canvas_composited`; if it is
`false`, wait for the Prepare or Preview canvas to finish initializing and
retry the capture.

Example requests:

```text
Capture the QIDI Studio Prepare view in the background and display the image directly in ChatGPT. Do not modify, slice, upload, or print anything.

Capture the Preview view in the background, report whether the OpenGL canvas was composited, and restore the original tab.
```
## Security and operational boundaries

- The MCP server listens only on `127.0.0.1`; remote access is provided by the
  separate Secure MCP Tunnel companion.
- The Runtime API key is protected for the current Windows user with DPAPI. It
  is not stored in QIDI Studio preferences, tunnel JSON, command lines, logs, or
  source control.
- Credential values and tunnel command lines are never returned by MCP tools.
- Preview tools are read-only and explicitly report `mutated: false`.
- Model and settings mutations use QIDI Studio's native data structures and
  undo/reslice paths where applicable.
- Print preparation requires an explicit physical filament source and verifies
  that the valid slice uses its matching project filament. Print start uses a
  prepared confirmation token plus explicit confirmation, applies QIDI's native
  Box/external mapping, and verifies the physically loaded slot through printer
  telemetry. Cancel and destructive operations also require confirmation where
  applicable.
- Software checks cannot verify physical conditions such as plate cleanliness,
  correct filament loading, an empty build plate, or safe machine access. The
  operator remains responsible for the printer and surrounding area.

Intentionally unsupported or deferred capabilities include embedded tunnel
credentials, automatic visual failure detection and cancellation, automated
multi-filament physical-source mapping, cloud-only print start, and operations
that cannot be exposed safely through stable native QIDI Studio state.

## Build from source

This fork follows QIDI Studio's Windows build system. From a Visual Studio 2022
developer environment, run:

```powershell
.\build_win.bat -s app -c RelWithDebInfo
```

The application is produced under `build\src\RelWithDebInfo`. Development-only
incremental rebuilds can use `-s app-dirty` after the initial dependency and
application build.

## Versioning

The MCP integration uses semantic versions independently of the upstream QIDI
Studio application version. `get_suite_capabilities` reports both the MCP suite
version and its QIDI Studio target. Tool counts in the changelog refer to the
registry advertised by the source at that release.

Before creating a release tag or archive, run the documentation preflight:

```powershell
.\tools\verify-release-docs.ps1 -Version 1.11.0 -RequireAccepted
```

Use the version being released. The check requires the README current-release
line and newest changelog heading to match, and can reject a release whose
acceptance status is still pending.

## Upstream QIDI Studio

QIDI Studio provides the slicer, printer profiles, device integration, and user
interface on which this fork is built. It descends from
[Bambu Studio](https://github.com/bambulab/BambuStudio),
[PrusaSlicer](https://github.com/prusa3d/PrusaSlicer), and
[Slic3r](https://github.com/Slic3r/Slic3r), with contributions from the broader
3D-printing community, including OrcaSlicer.

- [Official QIDI Studio repository](https://github.com/QIDITECH/QIDIStudio)
- [QIDI Studio wiki](https://wiki.qidi3d.com/en/software/qidi-studio)
- [QIDI homepage](https://qidi3d.com)

## License

QIDI Studio MCP Edition is licensed under the
[GNU Affero General Public License, version 3](LICENSE), consistent with
QIDI Studio and its upstream projects. Modified source distributed or operated
as a network service remains subject to the AGPL-3.0 requirements.

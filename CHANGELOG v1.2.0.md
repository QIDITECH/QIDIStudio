# QIDI Studio MCP Changelog

All notable changes to the QIDI Studio MCP integration are recorded here.

- Target application: QIDI Studio 2.7.2.10
- MCP endpoint: `http://127.0.0.1:8765/mcp`
- Versioning: semantic versioning for the MCP integration, independent of QIDI Studio's version
- Tool counts refer to the tools advertised by the source at that release

## [Unreleased]

### Planned

- Return printer-camera images as MCP image content, automatically enabling the selected printer's case light before capture.
- Capture QIDI Studio Prepare and Preview images for visual inspection.
- Add native print-by-object validation and object-sequence inspection/control.
- Generalize modal-dialog and UI-busy-state reporting.
- Serialize GUI mutations and add bounded GUI-thread waits/timeouts.
- Integrate management of a persistent, authenticated tunnel companion while keeping the MCP listener loopback-only.
- Correct `get_suite_capabilities()` so its reported suite version follows the MCP server version.

### Deferred

- Direct upload/start-print flow with explicit confirmation.
- Expanded printer controls, calibration execution, adaptive layer height, and painted support/seam operations.
- Closed-loop visual print-failure monitoring.

## [1.1.0] - 2026-08-07

Status: build and live recovery validation complete. Tool count: 89.

### Added

- Added `get_recovery_state` to report whether QIDI Studio is presenting its startup project-recovery dialog.
- Added `resolve_project_recovery` with `restore` and `cancel` actions.
- Exposed recovery states: `none`, `prompted`, `restoring`, and `cancelling`.
- Added structured blocked-UI responses with `error_code: "UI_BLOCKED"` and `dialog: "project_recovery"`.

### Changed

- Project/model tools now refuse to access the plater while recovery is pending or being processed.
- Recovery decisions use QIDI Studio's existing native modal path and native restore/cleanup logic.
- MCP `serverInfo.version` updated to `1.1.0`.

### Safety

- Cancelling recovery deletes pending recovery data and requires `confirm=true`.
- Recovery state and dialog pointers are accessed only on the GUI thread.
- Recovery state is reset if dialog display, project loading, or new-project creation throws an exception.

### Validation

- Patch applied successfully with `git apply --check`.
- Result passed `git diff --check`.
- Patch was applied to a clean copy of the supplied source and byte-compared with the intended modified files.
- All 89 tool definitions have matching dispatch branches with no duplicate names.
- Windows x64 `RelWithDebInfo` build completed successfully using the established `app-dirty` build path.
- Live `tools/list` returned all 89 tools through the ChatGPT tunnel, including both recovery tools.
- Verified the no-dialog state and rejection of recovery resolution when no decision was pending.
- Verified active-dialog detection, `ui_blocked: true`, and `UI_BLOCKED` rejection from a normal project tool.
- Verified restore resolution, subsequent project access, and recovery of the expected model object.
- Verified cancel rejection without `confirm=true`, confirmed cancellation, recovery-data cleanup, and normal restart without the prompt returning.

### Known issue

- Confirmation-gated recovery cancellation currently reports `INVALID_ARGUMENT` rather than a dedicated confirmation-required error code; the confirmation safety behavior is correct.

### Artifact

- `qidistudio-mcp-v1.1.0-project-recovery.patch`

## [1.0.2] - 2026-08-07

### Fixed

- Removed the unavailable `QDSDevice::m_print_msg` field from `get_printer_details`, resolving the printer-details compile failure.

### Testing

- Built as Windows x64 `RelWithDebInfo` using the established `app-dirty` build path.
- Established live MCP connectivity through the tunnel.
- Sent a print through QIDI Studio and exercised printer pause/resume; resume succeeded after the printer completed its purge delay.
- Cancel-print was intentionally not exercised.

### Artifacts

- `qidistudio-mcp-v1.0.2-printer-details-compile-hotfix.patch`
- `build-v1.0.2.log`

## [1.0.1] - 2026-08-07

### Changed

- Rebased the complete-suite patch onto the current QIDI Studio MCP source state without an intended capability change from 1.0.0.

### Known issue

- The rebase referenced `QDSDevice::m_print_msg`, which was not available in the target source and required the 1.0.2 compile hotfix.

### Artifacts

- `qidistudio-mcp-v1.0.1-complete-suite-rebased.patch`
- `build-v1.0.1.log`

## [1.0.0] - 2026-08-07

Tool count: 87.

### Added

- Added volume inspection and mutation tools: `list_object_volumes`, `rename_volume`, `delete_volume`, `set_volume_extruder`, and `set_volume_type`.
- Added native mesh-repair support with preview and confirmation-gated execution.
- Added transformed STL export and per-instance printable control.
- Added machine, nozzle, project-filament, and physical-printer capability reporting.
- Added filament-profile comparison, active-configuration validation, profile-change preview, and profile-variant creation.
- Added model measurement, build-volume comparison, overhang analysis, bed-contact analysis, and consolidated printability analysis.
- Added ranked axis-aligned orientation candidates and confirmation-free application with undo support.
- Added object relationship, bounding-box fit scaling, alignment, and scale-to-fit tools.
- Added native split-to-parts and merge-volumes operations with explicit confirmation.
- Added toolpath inspection, paginated layer summaries, and first-layer analysis.
- Added detailed printer status, printer-readiness checks, filament-quantity checks, calibration recommendations, and consolidated print preflight.
- Added `get_suite_capabilities` to disclose native, computed, agent-orchestrated, unavailable, and safety-gated capabilities.

### Safety

- Geometry-replacing repair, split, and merge operations require explicit confirmation.
- Print preflight reports blockers across model geometry, build volume, configuration, slice state, G-code, and optional printer readiness.
- Sending a print remains a user-confirmed QIDI Studio UI operation; unattended print start is not exposed.
- Painted support/seam brush strokes, anatomical deformation, camera download, and closed-loop visual failure detection are declared unavailable.

### Artifact

- `qidistudio-mcp-v1.0.0-complete-suite.patch`

## [0.7.0] - 2026-08-07

Tool count: 48.

### Added

- Added `get_model_diagnostics` for topology, open-edge, non-manifold, disconnected-part, and import-repair information.
- Added `get_slicing_warnings` for native object, print-step, and G-code warnings, including floating regions, cantilevers, overhangs, and empty layers.

### Artifact

- `qidistudio-mcp-v0.7.0-diagnostics.patch`

## [0.6.1] - 2026-08-07

### Fixed

- Corrected the horizontal-cut `keep=both` bitmask to explicitly combine `KeepUpper | KeepLower`.

### Artifact

- `qidistudio-mcp-v0.6.1-cut-bitmask-hotfix.patch`

## [0.6.0] - 2026-08-07

Tool count: 46.

### Added

- Added native `cut_object_horizontal` with upper/lower/both selection and explicit confirmation.
- Added `reset_slice_settings` and `save_preset_as`.
- Added `rename_object`, confirmation-gated `delete_instance`, and `set_object_extruder`.

### Safety

- Horizontal cuts validate that the cut height lies strictly within the transformed object bounds.
- Horizontal cuts create an undo snapshot and require `confirm=true` because they replace source geometry.
- System/default presets cannot be overwritten.

### Artifacts

- `qidistudio-mcp-v0.6-consolidated.patch`
- `build-v0.6.log`

## [0.5.0] - 2026-08-06

Tool count: 40.

### Added

- Added `mirror_object` for local X/Y/Z mirroring.
- Added `undo` and `redo` project/model actions.

### Changed

- Horizontal cutting was withheld from the applied diagnostic variant until its native path and bitmask behavior were corrected in 0.6.0/0.6.1.

### Artifacts

- `qidistudio-mcp-v0.5-consolidated.patch`
- `qidistudio-mcp-v0.5-no-cut-diagnostic.patch`

## [0.4.0] - 2026-08-06

Tool count: 37.

### Added

- Added `list_objects` and `get_object_state` with geometry counts, bounds, printable state, and instance transforms.
- Added `center_object` and `drop_object_to_bed`.
- Added object-scope setting inspection, atomic mutation, and reset through `get_object_settings`, `set_object_settings`, and `reset_object_settings`.

### Artifact

- `qidistudio-mcp-v0.4-consolidated.patch`

## [0.3.0] - 2026-08-06

Tool count: 30.

### Added

- Added project state, guarded new-project and load-project operations, and 3MF export.
- Added plate listing, creation, selection, rename, and confirmation-gated deletion.
- Added paginated slice-setting discovery.
- Added object-level printable control.
- Added the native Send-to-Printer UI opener.
- Added online-printer pause, resume, and confirmation-gated cancel control.

### Safety

- New/load operations refuse to replace dirty projects or dirty presets.
- Plate deletion requires `confirm=true` and preserves QIDI Studio's one-plate minimum.
- Printer cancel requires `confirm=true`.
- Print start remains in QIDI Studio's native confirmation UI.

### Artifact

- `qidistudio-mcp-v0.3-consolidated-tools.patch`

## [0.2.0] - 2026-08-05

Tool count: 17.

### Added

- Expanded the initial read-only server into project and slicing control.
- Added active plate/instance state, model import, preset listing/selection, setting read/write, object transform/duplicate/delete, arrange, auto-orient, slice, slice status/result, and G-code export.
- Added indexed filament-setting reads for projects with multiple filament slots.
- Added paginated filament-preset enumeration.

### Safety

- All GUI/model access is marshalled to the wxWidgets GUI thread.
- Mutations refuse to run while slicing or another QIDI UI job is active.
- Setting changes are parsed into a temporary configuration and applied atomically.
- Transforms and duplication create undo snapshots.
- Preset switching refuses dirty presets instead of opening an unsaved-changes dialog.
- Arrange, auto-orient, slice, and export are asynchronous and require status polling.
- Direct printer upload/start remains intentionally excluded.

### Artifacts

- `qidi-studio-mcp-v0.2-source-bundle.zip`
- `qidi-studio-mcp-get-plate-state.patch`
- `qidi-studio-mcp-filament-index-read.patch`
- `qidistudio-mcp-filament-count-diagnostic.patch`
- `qidistudio-mcp-filament-paging-diagnostic.patch`

## [0.1.0] - 2026-08-05

Tool count: 1.

### Added

- Added the MCP HTTP/JSON-RPC server and integrated its lifecycle with `PrinterWebView`.
- Bound the endpoint to loopback at `127.0.0.1:8765/mcp`.
- Added read-only `list_printers` status reporting.
- Added request-size and request-time limits plus graceful server shutdown before device-manager teardown.

### Safety

- No printer-control tools were exposed.
- Printer device objects were snapshotted under the device-manager lock and not retained by the MCP response path.

### Artifact

- `qidi-studio-mcp-list-printers.patch`

## Known Issues

- QIDI Studio has produced native Windows crashes in both official and MCP builds. Two MCP-build dumps from 2026-08-07 are archived with matching `qidi-studio.exe`, `QIDIStudio.dll`, and PDB files; root cause remains undetermined.
- Rapid chained GUI mutations have not been ruled out as a contributor to MCP-build instability and need serialization testing.
- `control_printer` currently invokes both `sendCommand()` and `sendActionCommand()`; the device-manager implementation must be audited before adding more physical controls.
- The connected ChatGPT MCP previously exposed only 46 actions while the local server advertised 87; tunnel/control-plane discovery and metadata refresh still need verification.
- `get_suite_capabilities()` reports suite version `1.0.0` even after the MCP `serverInfo` version moves to `1.1.0`.
- Native print-by-object collision/clearance errors are visible in QIDI Studio but are not yet returned or resolved by the MCP.
- Printer camera metadata is available, but image download and MCP image-content return are not yet implemented.

## Release Procedure

For every new patch:

1. Add the release entry and move completed work out of `Unreleased`.
2. Update both MCP version-reporting locations.
3. Record added, changed, fixed, safety, testing, and known-issue information.
4. Confirm every advertised tool has exactly one dispatch branch.
5. Run `git apply --check` for distributed patches and `git diff --check` for the working tree.
6. Build Windows x64 `RelWithDebInfo` using the established `build_win.bat` `app-dirty` configuration and retain the versioned log.
7. Perform live smoke tests before starting the next patch.

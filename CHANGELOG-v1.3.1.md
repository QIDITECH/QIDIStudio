# QIDI Studio MCP Changelog

All notable changes to the QIDI Studio MCP integration are recorded here.

- Target application: QIDI Studio 2.7.2.10
- MCP endpoint: `http://127.0.0.1:8765/mcp`
- Versioning: semantic versioning for the MCP integration, independent of QIDI Studio's version
- Tool counts refer to the tools advertised by the source at that release

## [Unreleased]

### Planned

- Add tunnel-status reporting to the MCP API after the companion has been exercised with the user's existing tunnel provider.
- Add closed-loop camera monitoring only after capture and light-control behavior have been validated on live prints.

### Deferred

- Cloud-only print start, AMS/box remapping, calibration execution, adaptive layer height, and painted support/seam operations.
- Closed-loop visual print-failure monitoring.

## [1.3.1] - 2026-08-07

Status: source hotfix prepared; Windows build and live validation pending. Tool count: 98.

### Fixed

- Native FFF auto-arrange now chooses sequential spacing from the active plate's effective print sequence instead of consulting only the global process preset.
- Print-by-object now treats multiple printable instances of one model as a sequential print, matching multiple distinct model objects.
- Sequential G-code generation no longer skips later instances merely because they share the same `PrintObject`.
- Sequential layer counting and timelapse context now use the same instance-aware sequential predicate.
- Initial instance ordering now assigns a unique native model-instance identity instead of collapsing every copy of a single model to order `1`.
- `get_print_object_sequence` now enumerates printable on-plate instances directly from each model object and reports `stable_instance_id` plus `arrange_order`.

### Validation

- Live v1.3.0 testing reproduced the split state: the active plate reported `by_object`, while auto-arrange selected by-layer settings and moved seven of eight bearing instances off the Max 4 plate.
- Undo restored the eight-bearing 135 mm test layout exactly; no print was uploaded or started.
- The earlier unexpected QIDI Studio exit remains attributed to the known Windows runtime-DLL environment and is not treated as an MCP request-pressure defect in this hotfix.

### Artifact

- `qidistudio-mcp-v1.3.1-sequential-instances-hotfix.patch`

## [1.3.0] - 2026-08-07

Status: source patch prepared; Windows build and live validation pending. Tool count: 98.

### Added

- Added `prepare_print_job` to lock the active plate, target printer, bed-leveling/timelapse choices, native sequential-clearance preflight, slice fingerprint, and final job summary into a ten-minute single-use confirmation token.
- Added `start_print_job`, requiring both the token and `confirm=true`, to package the active plate through `Plater::send_gcode` and run QIDI's native local/LAN `.gcode.3mf` upload with `StartPrint`.
- Added `get_print_job_status` for packaging, upload progress, printer acceptance, printing, completion/stop, and failure reporting.
- Added short-lived in-memory print tokens. They are never persisted, are consumed once, and become invalid if the active plate, model transforms, profiles, slice settings, slice result, or native object order changes.
- Added local/LAN bed-leveling and timelapse option application before upload.
- Added native print-by-object clearance results to `run_print_preflight`, with explicit blockers for rejected plates, horizontal collisions, height collisions, and other sequential-clearance errors.

### Fixed

- `capture_studio_screenshot` now captures the QIDI Studio window through its native Windows handle instead of copying pixels from the desktop.
- Windows captures no longer include Chrome or another window merely because it overlaps QIDI Studio.
- The capture result now identifies its method and whether it used visible screen pixels.
- Print-object sequence responses now include `stable_object_id` values and an identity note because QIDI's native vector reorder renumbers index-based `object_id` values.
- `set_print_object_sequence` now returns the requested pre-operation indices, their stable identities, and whether native reordering reindexed object IDs.
- Sequence mutation results explicitly confirm that native model order was applied and require `validate_print_by_object` as the next safety gate.
- `validate_print_by_object` now returns `valid: false` when QIDI has rejected the sequential layout, even if the native validator supplies no message.

### Changed

- Removed the foreground-raising side effect from Studio capture.
- Updated MCP `serverInfo.version` and `get_suite_capabilities().suite_version` to `1.3.0`.
- `start_print_job` returns only native-job acceptance. Printing is confirmed later only when printer telemetry reports the matching filename.
- Direct print start is intentionally limited to printers with a reachable local/LAN address and a preset supporting QIDI's native `.gcode.3mf` package.

### Safety

- Preparation refuses every consolidated preflight blocker, including unsafe CoreXY sequential object order or placement.
- Start rechecks printer readiness, active plate identity, the locked fingerprint, current preflight, and packaged-file existence before upload.
- Confirmation tokens expire after 60–1800 seconds, default to 600 seconds, and are consumed before packaging to prevent concurrent reuse.
- The upload runs asynchronously and does not expose the temporary package path, printer address, tunnel credentials, or provider credentials.
- Automatic camera-based cancellation remains excluded; the existing cancel command still requires separate explicit confirmation.

### Validation

- Live v1.2.0 testing reproduced the defect: the PNG had the correct QIDI Studio window dimensions but contained the overlapping ChatGPT window.
- Live v1.2.0 testing confirmed native reverse ordering, by-object mode selection, undo restoration, and a valid clearance result with zero horizontal or height collisions.
- Live testing exposed the object-index renumbering ambiguity using two independently positioned objects; both sequence and temporary geometry changes were restored through native undo snapshots.
- Live testing confirmed an unsafe 1 mm layout becomes unsliceable; v1.3 reports that settled rejection as invalid and also blocks it in consolidated preflight.
- Static source audit confirms 98 unique tool definitions with 98 matching dispatch branches.
- Native Windows capture and the local/LAN upload/start path still require a Windows x64 build and live tests.

### Artifact

- `qidistudio-mcp-v1.3.0-end-to-end-print-workflow.patch`

## [1.2.0] - 2026-08-07

Status: source patch prepared; Windows build and live validation pending. Tool count: 95.

### Added

- Added `capture_printer_camera`, returning JPEG, PNG, or WebP camera frames as MCP image content.
- Camera capture now sends QIDI's native `SET_PIN PIN=caselight VALUE=1` command before every frame, waits for exposure, and intentionally leaves the light on.
- Added `capture_studio_screenshot` for the visible current, Prepare, or Preview window as PNG MCP image content.
- Added `get_ui_state` to report the selected view, visible dialogs, modal blocking state, and recovery state.
- Added `get_print_object_sequence` and `set_print_object_sequence` using the native model-object ordering that QIDI's sequential slicer consumes.
- Added `validate_print_by_object`, backed by QIDI's native horizontal and vertical toolhead-clearance validator.
- Added a provider-neutral Windows tunnel supervisor and Task Scheduler installer under `tools/mcp-tunnel`.

### Changed

- Generalized `UI_BLOCKED` responses to include either `project_recovery` or a visible modal-dialog title.
- Serialized GUI calls and replaced unbounded waits with a 30-second `GUI_TIMEOUT` result; queued calls are cancelled before execution when possible.
- Refactored `get_plate_state` onto the common bounded GUI-call path.
- Updated MCP `serverInfo.version` and `get_suite_capabilities().suite_version` to `1.2.0`.
- Removed camera image download from the unavailable-capability list.
- Kept the MCP socket bound to `127.0.0.1:8765`; the tunnel remains an external user-session process so it can survive QIDI Studio crashes.

### Safety

- Camera URLs are derived only from the selected QIDI device and image responses are capped at 8 MiB.
- Studio screenshots disclose that they capture visible screen pixels and may include overlapping windows.
- Object-order updates require an exact permutation, create an undo snapshot, and retain QIDI's own ordering and collision-validation path.
- The tunnel companion stores its executable and arguments under `%LOCALAPPDATA%`; no provider credential is compiled into QIDI Studio or committed to the patch.
- Direct print start remains excluded and cancel-print still requires explicit confirmation.

### Validation

- Static source audit confirms 95 unique tool definitions and a matching dispatch branch for each of the six new tools.
- The legacy custom `get_plate_state` future was removed; only the common bounded GUI future remains.
- Source and tunnel-script diffs pass whitespace checks.
- Windows x64 `RelWithDebInfo` build, live camera image delivery, screen capture, print-by-object collision checks, and tunnel restart behavior remain to be tested.

### Artifact

- `qidistudio-mcp-v1.2.0-visual-sequential-tunnel-r1.patch`

## [1.1.0] - 2026-08-07

Status: build and live validation in progress. Tool count: 89.

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

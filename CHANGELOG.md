# QIDI Studio MCP Changelog

## v1.11.0

- Status: Windows x64 build, packaged launch, background/offscreen capture, OpenGL canvas composition, local download, large-response transport, MCP App inline viewer, viewer security metadata, and live ChatGPT acceptance passed. Tool count: 118.
- Added **MCP Edition** branding to the QIDI Studio startup splash while preserving the upstream artwork.
- Added an MCP Apps capture viewer resource and linked all three image-producing tools to it so compatible ChatGPT clients can display Studio screenshots and printer-camera frames directly in the conversation, including a responsive image view and download control.
- Kept image bytes inside the authenticated MCP tool result and retained loopback download URLs only as a local fallback; no capture endpoint is exposed through the external tunnel.
- Hydrated the viewer from ChatGPT's full tool-response metadata when the initial result notification arrives before component subscription, versioned the resource URI to invalidate cached viewer HTML, and directed non-MCP-Apps clients to materialize the returned image content as an inline temporary attachment instead of merely reporting capture success.
- Declared an explicit no-egress widget CSP and the project owner's unique GitHub Pages widget domain, removing ChatGPT's missing-CSP and missing-domain template warnings while keeping the self-contained capture viewer network-isolated.
- Extended `capture_studio_screenshot` with an optional `background` flag that defaults to true while preserving the existing tool name and target values.
- On Windows, a minimized or hidden QIDI Studio window is restored outside the virtual desktop with no activation, captured with `PrintWindow`, and returned to its original placement, visibility, minimized state, and selected tab.
- Added sampled dynamic-range validation so an entirely blank background render fails explicitly instead of being returned as a successful image.
- Added short-lived loopback download URLs for Studio screenshots and printer-camera images so the assistant can provide a normal clickable file link without publishing captures through the external tunnel.
- Captured the active Prepare or Preview OpenGL back buffer and composited it into the native-window PNG so the bed, models, toolpaths, and current camera are preserved in background captures.
- Added canvas-specific geometry and dynamic-range metadata; blank captured canvases fail explicitly, and callers can use `gl_canvas_composited` to detect and retry an early-startup capture before the Prepare or Preview canvas is ready.
- Split the loopback download address into scheme, origin, and path metadata so ChatGPT can construct the clickable local link without rejecting the tool result's private URL.
- Switched MCP responses and capture downloads back to blocking socket writes after request parsing so large PNG/base64 responses cannot be truncated by would_block.
- Kept the existing visible-window capture path available with `background: false`.
- Updated MCP `serverInfo.version` and `get_suite_capabilities().suite_version` to `1.11.0`.

## v1.10.0

- Status: Windows x64 build and live end-to-end acceptance passed for attachment import, GUI contention, enum serialization, explicit physical-filament selection, guarded print start, and printer-slot verification. Tool count: 118.
- Added `import_attached_models` so ChatGPT can pass user-attached STL, 3MF, OBJ, AMF, STEP, STP, or PLY files directly to QIDI Studio using the official `_meta["openai/fileParams"]` contract.
- Streamed attachments into a randomized per-request directory under the current user's QIDI Studio MCP application-data directory, imported them with QIDI's native model loader, and removed the temporary files after the import attempt.
- Preserved `import_model` for models that already exist on the QIDI Studio computer.
- Restricted attachment ingestion to trusted ChatGPT/OpenAI HTTPS file hosts, disabled redirects, limited each file to 256 MiB and each request to 512 MiB, and retained no download URL or local staging path in tool results.
- Kept QIDI global HTTP headers and tunnel credentials out of attachment requests by using an isolated libcurl download path.
- Added stale-request cleanup for staging directories left by an interrupted process.
- Extended native and agent-orchestrated capability reporting for attachment import and the end-to-end conversational print pipeline.
- Passed the first live ChatGPT attachment import with a 608-bearing STL: the 22 x 22 x 7 mm model downloaded, imported, arranged, and remained available for mesh and printability analysis without slicing.
- Repaired missing QIDI enum serialization maps on temporary MCP option copies before reading, previewing, changing, fingerprinting, or reporting settings, preventing the observed `ConfigOptionEnumsGenericTempl::serialize()` access violation without mutating active settings during reads or previews.
- Made overlapping GUI-backed MCP calls fail fast with retryable `GUI_BUSY` metadata instead of waiting behind the active request until the tunnel returns HTTP 504; a full 23-setting non-mutating preview passed afterward with all four QIDI Box slots and the external filament slot active.
- Required guarded direct prints to name an explicit QIDI Box slot or external spool, invalidated old slices when object filament assignments change, verified that the valid slice was generated with the matching project filament, locked slot telemetry into the confirmation token, applied QIDI's native `enable_box`/`value_tN` mapping commands before upload, and required matching physical-slot telemetry before reporting acceptance or printing as confirmed. Multi-filament direct starts remain blocked pending complete per-filament mapping support.
- Treated the previously loaded toolhead slot as pending during printer preparation and QIDI Box filament exchange; physical-source mismatch is now reported only if the first print layer begins without the requested slot, preventing premature cancellation at layer zero.
- Completed live attachment-to-print acceptance on a Max 4 with the 608-bearing STL: the model was assigned to project filament index 3/extruder 4 before slicing, mapped to red PETG in QIDI Box global slot 3, confirmed by `reported_physical_slot: slot3`, and printed successfully without using external slot 16.
- Updated MCP `serverInfo.version` and `get_suite_capabilities().suite_version` to `1.10.0`.

## v1.9.1

- Status: source patch ready for Windows x64 build and live acceptance. Tool count: 117.
- Fixed Tunnel Manager **Save / Repair and Connect** so it reuses an existing current-user DPAPI credential when the Runtime API key field is empty, while still requiring a key for first-time setup or intentional replacement.
- Classified a missing local Codex plugin as informational for ChatGPT tunnel connections, allowing the existing OAuth and verified health-listener compatibility findings to be presented correctly.
- Applied the same compatibility rules to setup preflight while preserving failures for unexpected findings.
- Made repair stop and verify the managed tunnel client before replacing its executable, including an orphan left by an interrupted repair.
- Updated MCP `serverInfo.version` and `get_suite_capabilities().suite_version` to `1.9.1`.

## v1.9.0

- Status: Windows x64 build and live acceptance passed. Tool count: 117.
- Added `list_setting_definitions` for searchable, paginated native setting metadata across print, filament, printer, object, and volume scopes.
- Setting definitions now expose QIDI's canonical type, unit, default, numeric bounds, enum choices, category, complexity mode, read-only state, supported scopes, and contextual effective/inherited values.
- Added `preview_settings_update` for strict, non-mutating parsing and normalization of proposed changes across all five settings scopes.
- Preview results report effective-value inheritance, override creation, whether the slice would be invalidated, and copied full-configuration validation.
- Native validation distinguishes pre-existing active-configuration errors from errors introduced by the proposed update.
- Updated the MCP suite version to `1.9.0`; registry count is now 117 tool definitions and 117 dispatch handlers.
- Windows x64 `RelWithDebInfo` build completed successfully in 00:45:15.07.
- Connector refresh exposed all 117 tools and reported suite `1.9.0` against QIDI Studio `2.7.2.10`.
- Live `list_setting_definitions` returned 280 print-scope definitions with native metadata and `mutated: false`.
- Live `preview_settings_update` normalized and validated a proposed layer-height change, correctly predicted slice invalidation and dirty state, and left the setting and both dirty flags unchanged.
- The malformed-value preview returned a strict parse error without mutation.

## v1.8.0

- Added the integrated Windows MCP Tunnel Manager for configuration, status, Start, Stop, Restart, Diagnostics, dashboard access, logs, documentation, and copying connection information.
- Preserved DPAPI protection for the Runtime API key without displaying stored credentials.
- Added UTF-8-safe communication between the native dialog and PowerShell management scripts.
- Hardened Stop and Restart by verifying the configured tunnel executable, terminating orphaned companion processes, releasing port 8080, and writing an accurate stopped heartbeat.
- Made Start recover safely from an unhealthy stale tunnel instance.
- Corrected Diagnostics handling for optional OAuth metadata and the verified active health listener while preserving genuine failures.
- Runtime-validated configuration status, Stop, Start, Restart, Diagnostics, dashboard health, and Open Logs.
- MCP registry remains complete at 115 tool definitions and 115 dispatch handlers.

## v1.7.0

- Added `set_project_filament` for indexed project-filament replacement with compatibility validation and rollback.
- Added `get_volume_settings`, `set_volume_settings`, and `reset_volume_settings`.
- Added correct print-preset, object, and volume inheritance reporting.
- Refreshed filament controls immediately after MCP-driven filament changes.
- Increased the complete MCP registry to 115 tools.

## v1.6.1

- Corrected transformed-instance surface selection using the combined instance and volume transforms.
- Added busy/retry protection around adaptive-layer-height and surface-paint operations while QIDI Studio jobs are active.
- Limited returned layer-profile points while retaining accurate total, range, and truncation metadata.
- Completed build and runtime validation of the v1.6 surface-painting and adaptive-layer-height work.

All notable changes to the QIDI Studio MCP integration are recorded here.

- Target application: QIDI Studio 2.7.2.10
- MCP endpoint: `http://127.0.0.1:8765/mcp`
- Versioning: semantic versioning for the MCP integration, independent of QIDI Studio's version
- Tool counts refer to the tools advertised by the source at that release

## [Unreleased]

### Deferred

- Cloud-only print start, AMS/box remapping, calibration execution, and connected-surface selection.
- Closed-loop visual print-failure monitoring.

## [1.6.0] - 2026-08-08

Status: source milestone completed; Windows build and live validation completed with the v1.6.1 corrective hotfix. Tool count: 111.

### Added

- Added `preview_surface_selection` for read-only, deterministic whole-facet selection using explicit source facet IDs, plate-space height ranges, plate-space bounding boxes, facing direction, overhang angle, or the entire volume.
- Added `get_support_paint_state` and `get_seam_paint_state` to inspect QIDI's native per-volume painted-facet state.
- Added `set_support_paint` for native support enforcer, support blocker, and erase operations.
- Added `set_seam_paint` for native preferred-seam, blocked-seam, and erase operations.
- Added clear-all behavior through selector type `all` with state `erase`.

### Changed

- Updated MCP `serverInfo.version` and `get_suite_capabilities().suite_version` to `1.6.0`.
- Expanded native capability reporting for geometric surface selection and painted support/seam facets.
- Reclassified freehand brush-stroke replay, rather than painted support/seam state itself, as not safely exposed.

### Design

- Surface selectors operate on original source-mesh facet IDs without opening a painter gizmo or simulating mouse input.
- `instance_id` supplies the reference transform for plate-space selectors; painted state remains shared by every instance of the model object, matching QIDI Studio's native data model.
- Whole-facet mutations intentionally collapse any finer brush subdivision inside each selected source facet.
- Preview limits returned facet IDs to a caller-controlled maximum while still reporting the complete selected count, area, and bounds.

### Safety

- Surface previews report `mutated: false` and never touch model state.
- Paint mutations refuse to run during slicing or another active QIDI Studio job.
- Every changed paint operation uses QIDI Studio's undo stack and schedules native background reslicing.
- Empty selections and no-op paint requests do not create undo snapshots.

### Validation

- Static source audit confirms 111 unique tool definitions with 111 matching dispatch branches.
- Both public version-reporting locations report `1.6.0`.
- Windows x64 `RelWithDebInfo` build and live support/seam persistence tests remain required.

### Artifact

- `qidistudio-mcp-v1.6.0-surface-painting.patch`

## [1.5.0] - 2026-08-08

Status: Windows build and live acceptance passed. Tool count: 106.

### Added

- Added `get_layer_height_profile` to inspect both the stored per-object variable profile and the effective profile produced by the active presets.
- Added `preview_adaptive_layer_height` to run QIDI's native adaptive layer-height engine without mutating the project.
- Added `apply_adaptive_layer_height` to apply a native speed-to-quality profile to an explicit object with an undo snapshot.
- Added `smooth_layer_height_profile` with QIDI's native radius and keep-min controls.
- Added `reset_layer_height_profile` to return an object to the active uniform layer settings.

### Changed

- Updated MCP `serverInfo.version` and `get_suite_capabilities().suite_version` to `1.5.0`.
- Expanded native capability reporting to include adaptive layer-height profiles.

### Safety

- Preview is read-only and reports `mutated: false`.
- Apply, smooth, and reset refuse to run while slicing or another QIDI Studio job is active.
- Every profile mutation uses QIDI Studio's undo stack and schedules native background reslicing.
- Adaptive profiles are generated from the active full configuration plus the target object's own geometry and overrides.
- Automatic visual failure diagnosis/cancellation remains excluded.

### Validation

- Static source audit confirms 106 unique tool definitions with 106 matching dispatch branches.
- Both public version-reporting locations report `1.5.0`.
- Windows x64 `RelWithDebInfo` build completed successfully in 00:51:42.66.
- Connector refresh exposed all 106 tools and reported suite `1.5.0`.
- Live inspection, read-only preview, apply, native undo, smoothing, and reset all passed.
- The tested project was left at its original uniform 0.20 mm profile; nothing was sliced or printed.

### Artifact

- `qidistudio-mcp-v1.5.0-adaptive-layer-height.patch`

## [1.4.0] - 2026-08-08

Status: Windows build and live acceptance passed. Tool count: 101.

### Added

- Added `get_tunnel_status` for provider-neutral configuration, supervisor state, heartbeat age, and derived healthy/stale reporting.
- Added `capture_print_monitor_snapshot`, returning one camera frame together with matching print state, filename, progress, layers, duration, temperatures, fans, speed, filament sensor, and case-light telemetry.
- Added `set_printer_case_light` for explicit on/off requests with asynchronous telemetry confirmation.
- Added atomic tunnel-status writes and a 15-second supervisor heartbeat so a stale `running` record is distinguishable from a healthy tunnel.
- Added `update-qidi-mcp-tunnel.ps1` to refresh an existing installed supervisor while preserving its provider configuration.
- Bundled OpenAI `tunnel-client` v0.0.10 for Windows x64 as an independently staged companion, with its Apache-2.0 license and notice.
- Added a click-to-run guided tunnel setup that opens the required Platform pages, creates/reuses the `qidi-studio` profile, runs `doctor --explain`, installs the scheduled task, and starts the tunnel.
- Added per-user DPAPI protection for the Runtime API key; the supervisor injects it only into the child process environment at launch.

### Changed

- Updated MCP `serverInfo.version` and `get_suite_capabilities().suite_version` to `1.4.0`.
- Expanded capability reporting for tunnel health, observational print monitoring, and case-light control.

### Safety

- Tunnel reporting never returns the configured executable, provider argument line, or credentials.
- Monitoring snapshots are observational: no image diagnosis, pause, cancel, or other print-control action is performed.
- The MCP listener remains bound to `127.0.0.1:8765`; public connectivity remains owned by the external tunnel companion.
- The Runtime API key is not stored in `tunnel.json`, a process argument line, QIDI Studio, the MCP response, or source control.
- Automatic visual failure detection/cancellation, cloud-only print start, and other deferred slicer operations remain excluded.

### Validation

- Windows x64 `RelWithDebInfo` build completed successfully in 00:44:03.68.
- Connector refresh exposed all 101 tools and reported suite `1.4.0` against QIDI Studio `2.7.2.10`.
- Guided setup created the no-auth loopback profile, protected the Runtime API key with DPAPI, and installed the per-user scheduled task.
- Live `/readyz` returned `ready`; the scheduled task remained running and the supervisor status reported a fresh heartbeat.
- Live Max 4 monitoring snapshot, camera capture, and case-light telemetry confirmation passed; the original off state was restored after testing.
- Project recovery was restored successfully after the smoke test and returned eight printable instances.

### Artifact

- `qidistudio-mcp-v1.4.0-monitoring-observability.patch`
- `qidistudio-mcp-v1.4.0-integrated-secure-tunnel.patch`
- `qidistudio-mcp-v1.4.0-tunnel-profile-hotfix.patch`
- `qidistudio-mcp-v1.4.0-no-auth-doctor-hotfix.patch`
- `qidistudio-mcp-v1.4.0-dpapi-readiness-hotfix.patch`

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

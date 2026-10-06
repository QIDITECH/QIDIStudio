# QIDI Studio MCP v1.6.0 Changelog

Release date: 2026-08-08
Target: QIDI Studio 2.7.2.10
MCP tool count: 111

## Overview

v1.6.0 begins deterministic surface authoring. This milestone adds reusable geometric facet selection plus native support and seam painting without opening QIDI Studio's painter gizmos or replaying mouse strokes.

## Added

- `preview_surface_selection`
- `get_support_paint_state`
- `set_support_paint`
- `get_seam_paint_state`
- `set_seam_paint`

## Selection modes

- `all`
- `facet_ids`
- `height_range`
- `bounding_box`
- `normal`
- `overhang`

Height, bounds, normal, and overhang selectors use build-plate coordinates based on the requested `instance_id`. Explicit facet IDs refer to the selected volume's original source mesh.

## Paint states

- `enforcer`: enforce support or prefer a seam.
- `blocker`: block support or block a seam.
- `erase`: remove support or seam painting from the selected facets.

Selector type `all` with state `erase` clears the corresponding paint type from the volume.

## Design

- Painting is stored on the model volume and therefore affects every instance of that object.
- Mutations operate on complete original facets. Selecting a source facet intentionally replaces any finer painter subdivision inside that facet.
- Preview returns total selected count, transformed surface area, transformed bounds, and a bounded list of source facet IDs.
- Existing native painted state is deserialized before each mutation, so unselected painted regions are preserved.

## Safety

- Preview never mutates the project.
- Mutations refuse active slicing and other QIDI Studio jobs.
- Changed operations create a native undo snapshot and schedule background reslicing.
- Empty and no-op selections do not create snapshots.
- Automatic camera diagnosis and automatic print cancellation remain out of scope.

## Validation

- Static source audit: 111 unique tool definitions and 111 unique dispatch branches, with exact parity.
- Version audit: both `serverInfo.version` and suite capability reporting are `1.6.0`.
- Windows x64 build and live QIDI Studio persistence tests remain pending.

## Planned follow-on milestones

- Connected-surface selection.
- Native calibration-plate generation for flow rate, pressure advance, temperature, maximum volumetric speed, and retraction.
- Camera control, clean offscreen rendering, primitive creation, and richer setting metadata.

## Deferred

- Automatic visual failure diagnosis/cancellation.
- Cloud-only print start.
- AMS/QIDI Box remapping and control.
- Freehand painter brush-stroke replay.

## Artifact

- `qidistudio-mcp-v1.6.0-surface-painting.patch`

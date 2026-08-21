# Changelog

## v0.6.9 — Selectable DOF backends

### DOF integration

- Added support for both **IGCSDOF** and **MARTY'S MODS Parallax DOF**.
- Both compatible DOF addons may stay loaded and connected at the same time.
- Added an overlay selector to choose which DOF backend receives the active camera.
- The inactive backend remains connected but receives a disabled camera state.
- Switching between IGCSDOF and Parallax DOF is immediate when no screenshot session is active.
- Starting a screenshot session from either addon automatically makes that addon the active backend.
- Overlay now reports `Active`, `Ready`, `Detected / not connected`, or `Not found` per backend.
- Screenshot-session switching is locked until the current session finishes.

### Compatibility

- Existing IGCS command exports are unchanged.
- Provider protocol remains v1.
- No camera-basis, engine-profile, multishot, panorama, or bokeh math was changed in this release.

## v0.6.8 — idTech6 / DOOM 2016

- Added the dedicated idTech6 engine profile used by DOOM 2016.
- Added native Forward-vector handling and Relay-side basis reconstruction.
- Added DOOM 2016 reference configuration/documentation.
- No provider protocol change.

## v0.6.7 — Release cleanup

### Universal RAW provider v6

- Multi-base support.
- Base resolver supports:
  1. Lua numeric globals
  2. Lua functions returning addresses
  3. CE pointer symbols
- Camera/Detach pointer chains do not need to be duplicated.
- CONFIG contains offsets only.
- Added a clear `STOP EDITING HERE` boundary after CONFIG.
- Rebuilt the STOP function as one canonical block.
- Removed malformed/duplicated STOP residue.

### idTech7 engine guard

- Pointer lock and writer NOPs execute only for idTech7.
- UE2.5 / UE3 / UE4 / Northlight ignore idTech7 writer definitions.

### Updated CT examples

- DMC-DevilMayCry.CT aligned to v6.
- DOOMEternalx64vk.CT aligned to v6.
- Control_DX12.CT retained as validated Northlight reference.

### Bridge

- No engine-math changes in this cleanup.

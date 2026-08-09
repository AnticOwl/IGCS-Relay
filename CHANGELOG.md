# Changelog

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

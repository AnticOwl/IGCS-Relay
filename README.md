# IGCSDOF Universal Bridge v0.6.9

Universal ReShade bridge for external camera providers, with selectable support for **IGCSDOF** and **MARTY'S MODS Parallax DOF**.

## DOF backend selection

IGCS Relay can keep both supported DOF addons loaded at the same time:

```text
IGCSRelay.addon32 / IGCSRelay.addon64
IgcsConnector.addon32 / IgcsConnector.addon64
MartysMods_ParallaxDOF.addon32 / MartysMods_ParallaxDOF.addon64
```

Open the Relay overlay and choose the backend under **DOF Integration**:

```text
Backend: IGCSDOF
```

or:

```text
Backend: Parallax DOF
```

Both detected addons stay connected. Only the selected backend receives an enabled camera; the other remains ready and can be selected immediately after the current screenshot session ends.

If a screenshot session is started directly from IGCSDOF or Parallax DOF, the Relay automatically detects which addon started it and marks that backend as active.

Typical status display:

```text
IGCSDOF addon      Active
Parallax DOF addon Ready
```

or:

```text
IGCSDOF addon      Ready
Parallax DOF addon Active
```

Backend switching is disabled while a screenshot session is rendering.

See [docs/DOF_BACKENDS.md](docs/DOF_BACKENDS.md) for the complete backend workflow and troubleshooting notes.

## Camera provider methods

IGCS Relay includes two generic Cheat Engine provider templates. Choose whichever method fits the table you are adapting.

### Automatic Memory Record detection

```text
examples/Generic_RAW_Provider_Auto.lua
```

This provider reads the camera values directly from existing Cheat Engine Memory Records and lets Cheat Engine resolve their final addresses.

The table should expose camera records using these simple descriptions:

```text
Camera X   or X
Camera Y   or Y
Camera Z   or Z
Pitch
Yaw
Roll
FOV
```

The records may use direct addresses, registered symbols, pointer records, multi-level pointer chains, or different chains for position, rotation and FOV. The provider uses the final address already resolved by Cheat Engine, so pointer chains do not need to be copied into the IGCS script.

Only the Relay engine profile still needs to be selected in the provider configuration.

### Manual configuration

```text
examples/Generic_RAW_Provider.lua
```

This provider uses an explicit CONFIG block containing camera bases and offsets. It is useful when you prefer to define the layout manually or when a table does not expose suitable Memory Records for automatic detection.

Edit **only** the CONFIG block. Everything below:

```text
END USER CONFIGURATION
```

is universal provider core.

Engine-specific camera-basis math belongs in the Relay, not in the CE provider.

## Multi-base resolver

The manual provider allows each data group to select its own base:

```lua
bases = {
    position = "getCamBase",
    rotation = "addr1",
    fov      = "addr1"
}
```

A base name can be:

```text
1. Lua global numeric address
2. Lua global function returning an address
3. CE registered symbol containing a pointer
```

This allows existing Camera/Detach code to remain unchanged.

### Simple one-base camera

```lua
bases = {
    position = "pCamera",
    rotation = "pCamera",
    fov      = "pCamera"
}
```

### Split camera

If Camera/Detach already has:

```lua
function getCamBase()
    ...
    addr1 = ...
    return cam
end
```

use:

```lua
bases = {
    position = "getCamBase",
    rotation = "addr1",
    fov      = "addr1"
}
```

No pointer chain is duplicated in IGCS.

## Engine profiles

```text
UE2.5      integer Unreal Rotator
UE3        integer Unreal Rotator
UE4        float-degree rotation
UE5        UE4-compatible degree rotation math
idTech6    native Forward vector packed in RAW rotation slots
idTech7    float-degree rotation
Northlight float-radian rotation
```

`UE5` is a distinct provider tag but currently resolves to the same camera-basis math as `UE4`. This keeps UE5 providers cleanly identified while allowing UE5-specific behavior to be introduced later without changing existing tables.

## idTech6 / DOOM 2016

DOOM 2016 uses a dedicated `idTech6` profile in the Relay.

The CE provider remains math-free. For this profile the three RAW rotation slots are used to carry the camera's native Forward vector:

```text
pitch slot -> Forward X
yaw slot   -> Forward Y
roll slot  -> Forward Z
```

Validated DOOM 2016 camera layout relative to `pCamera`:

```text
+000  Position X
+004  Position Y
+008  Position Z

+5A4  Forward X
+5A8  Forward Y
+5AC  Forward Z
```

Provider configuration:

```lua
engine = "idTech6"

bases = {
    position = "pCamera",
    rotation = "pCamera",
    fov      = ""
}

position = {
    x = 0x000,
    y = 0x004,
    z = 0x008
}

rotation = {
    pitch = 0x5A4,
    yaw   = 0x5A8,
    roll  = 0x5AC
}

fov = {
    fixed = 45.0
}

cameraLockSymbol = ""
writers = {}
```

The Relay normalizes the native Forward vector, reconstructs an orthonormal Right/Up/Forward basis, and performs multishot movement strictly in the camera Right/Up plane. This removes Forward leakage during U/D movement.

The current idTech6 multishot path uses the validated DOOM bokeh scale of `2.0`.

### idTech6 limitations

- No camera writer NOPs are required for the validated DOOM 2016 implementation.
- No pointer lock is required.
- Native Photo Mode roll is not available.
- The real DOOM 2016 Photo Mode FOV address is not yet validated; use a fixed FOV value for now.
- Panorama yaw is intentionally not enabled for idTech6 yet because the RAW rotation triplet carries Forward XYZ rather than Euler angles.

## idTech7 engine guard

Only `Engine=idTech7` activates:

```text
pCameraLock
configured camera writer NOPs
250 ms restore hold
```

UE2.5 / UE3 / UE4 / UE5 / idTech6 / Northlight ignore the idTech7 writer list.

## Reference CTs

```text
examples/reference_cameras/DMC-DevilMayCry.CT
examples/reference_cameras/DOOMEternalx64vk.CT
examples/reference_cameras/Control_DX12.CT
```

DmC and DOOM Eternal contain the current provider configuration for their engine profiles.

CONTROL remains the validated Northlight reference.

DOOM 2016 is documented through the idTech6 configuration above; a dedicated reference CT can be added once its remaining FOV/roll work is finalized.

## Build

```bat
scripts\Setup-All.cmd
scripts\Build-x86.cmd
scripts\Build-x64.cmd
```

GitHub Actions builds can also be launched manually with **Run workflow**.

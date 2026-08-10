# IGCSDOF Universal Bridge v0.6.8

Universal Bridge + Cheat Engine RAW provider reference.

## Rule for new camera integrations

Edit **only** the CONFIG block in:

```text
examples/Generic_RAW_Provider.lua
```

Everything below:

```text
END USER CONFIGURATION
```

is universal provider core.

Engine-specific camera-basis math belongs in the Relay, not in the CE provider.

## Multi-base resolver

Each data group selects its own base:

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
idTech6    native Forward vector packed in RAW rotation slots
idTech7    float-degree rotation
Northlight float-radian rotation
```

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

The Relay normalizes the native Forward vector, reconstructs an orthonormal Right/Up/Forward basis, and performs multishot movement strictly in the camera Right/Up plane. This removes the Forward leakage that caused focus-plane blur during U/D movement.

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

UE2.5 / UE3 / UE4 / idTech6 / Northlight ignore the idTech7 writer list.

## Reference CTs

```text
examples/reference_cameras/DMC-DevilMayCry.CT
examples/reference_cameras/DOOMEternalx64vk.CT
examples/reference_cameras/Control_DX12.CT
```

DmC and DOOM Eternal contain the current provider configuration for their engine profiles.

CONTROL remains the validated Northlight reference.

DOOM 2016 is currently documented through the idTech6 configuration above; a dedicated reference CT can be added once its remaining FOV/roll work is finalized.

## Build

```bat
scripts\Setup-All.cmd
scripts\Build-x86.cmd
scripts\Build-x64.cmd
```

GitHub Actions builds can also be launched manually with **Run workflow** on the branch to test, then repeated on `main` after merge.

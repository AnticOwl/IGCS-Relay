# IGCSDOF Universal Bridge v0.6.7

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
idTech7    float-degree rotation
Northlight float-radian rotation
```

## idTech7 engine guard

Only `Engine=idTech7` activates:

```text
pCameraLock
configured camera writer NOPs
250 ms restore hold
```

UE2.5 / UE3 / UE4 / Northlight ignore the idTech7 writer list.

## Reference CTs

```text
examples/reference_cameras/DMC-DevilMayCry.CT
examples/reference_cameras/DOOMEternalx64vk.CT
examples/reference_cameras/Control_DX12.CT
```

DmC and DOOM contain the current v6 provider configuration.

CONTROL remains the validated Northlight reference.

## Build

```bat
scripts\Setup-All.cmd
scripts\Build-x86.cmd
scripts\Build-x64.cmd
```

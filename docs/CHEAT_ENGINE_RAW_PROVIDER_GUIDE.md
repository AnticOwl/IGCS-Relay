# Cheat Engine RAW Provider Guide

## Golden rule

Only edit `CONFIG`.

Do not edit provider core below:

```text
END USER CONFIGURATION
```

## CONFIG example — single base

```lua
local CONFIG = {

  engine = "UE3",

  bases = {
    position = "pCamera",
    rotation = "pCamera",
    fov      = "pCamera"
  },

  position = {
    x = 0x56C,
    y = 0x570,
    z = 0x574
  },

  rotation = {
    pitch = 0x578,
    yaw   = 0x57C,
    roll  = 0x580
  },

  fov = {
    offset = 0x584
  }
}
```

CONFIG contains offsets only. Do not write runtime expressions like:

```lua
x = cam + 0x1D8
```

## Base resolution order

```text
1. Lua global numeric variable
2. Lua global function returning an address
3. CE symbol containing a pointer
```

### Split camera example

Existing Camera/Detach:

```lua
function getCamBase()
    ...
    addr1 = ...
    return cam
end
```

IGCS CONFIG:

```lua
bases = {
    position = "getCamBase",
    rotation = "addr1",
    fov      = "addr1"
},

position = {
    x = 0x1D8,
    y = 0x1DC,
    z = 0x1E0
},

rotation = {
    pitch = 0x1E4,
    yaw   = 0x1E8,
    roll  = 0x1EC
},

fov = {
    offset = 0x45C
}
```

No Camera/Detach rewrite and no pointer-chain duplication are required.

## Engine storage

```text
UE2.5 / UE3
  rotation = integer Unreal Rotator

UE4
  rotation = float degrees

idTech7
  rotation = float degrees

Northlight
  rotation = float radians
```

## idTech7 lifecycle

Only idTech7 uses:

```text
pCameraLock
writer NOPs
250 ms restore hold
```

## Reference CTs

### DmC / UE3

```text
pCamera
X +56C
Y +570
Z +574
Pitch +578
Yaw +57C
Roll +580
FOV +584
```

### DOOM Eternal / idTech7

```text
pCamera
X +D68
Y +D64
Z +D6C
Pitch +BC4
Yaw +BC8
Roll +BCC
FOV +D78
```

Uses `pCameraLock`, seven writer locks and 250 ms restore hold.

### CONTROL / Northlight

See `NORTHLIGHT_CONTROL.md`.

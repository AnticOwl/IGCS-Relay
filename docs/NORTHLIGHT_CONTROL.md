# Northlight / CONTROL Profile — v0.6.7

Northlight is a validated RAW engine profile in IGCSDOF Universal Bridge.

The first validated implementation is CONTROL DX12.

## Raw CONTROL layout

`pControlPM` points directly to the Photo Mode data block:

```text
+00  Y       float
+04  Z       float
+08  X       float

+10  Yaw     float radians
+14  Pitch   float radians
+18  Roll    float radians
```

Protocol mapping:

```text
RAW X = +08
RAW Y = +00
RAW Z = +04

RAW Pitch = +14
RAW Yaw   = +10
RAW Roll  = +18
```

There is no additional XYZ permutation after this mapping.

## Northlight zero basis

At zero rotation:

```text
Forward = +X
Right   = +Y
Up      = +Z
```

The validated basis is:

```text
Forward:
  fx = cp * cy
  fy = cp * sy
  fz = sp

Base Right:
  rx = -sy
  ry =  cy
  rz =  0

Base Up:
  ux = -sp * cy
  uy = -sp * sy
  uz =  cp
```

Positive raw roll follows the validated CONTROL convention:

```text
Right' = Right*cos(roll) + Up*sin(roll)
Up'    = Up*cos(roll) - Right*sin(roll)
```

The shared bridge basis reproduces this by negating Northlight raw roll before
evaluation.

## Multishot / DoF

Validated CONTROL scaling:

```text
scaledLR = incomingLR * 0.007
scaledUD = incomingUD * 0.007
```

Sample position:

```text
sample = base + Right*scaledLR + Up*scaledUD
```

A complete IGCSDOF DoF render was successfully completed with this profile.

## Panorama

Northlight yaw is stored in radians.

`ROTATE_YAW_RAW` therefore uses radians directly.

## Session base

Northlight uses a fresh-session handshake:

```text
SESSION_BEGIN
provider saves fresh camera
provider publishes CAMERA_RAW locked=1
bridge waits for that packet
bridge stores it as session base
```

CONTROL does not require the idTech 7-style camera writer NOPs or pointer lock.

## FOV

The actual Photo Mode FOV address has not yet been identified in this camera
structure.

The validated CONTROL provider therefore reports:

```text
FOV = 37.0
```

This fixed value does not prevent IGCSDOF DoF rendering and remains the only
temporary part of the CONTROL provider.

## Included files

```text
examples/Northlight_CONTROL_RAW_Provider.lua
examples/Northlight_CONTROL_CameraFinder.txt
examples/reference_cameras/Control_DX12.CT
```

The CT contains the aligned current DualPipe RAW provider.

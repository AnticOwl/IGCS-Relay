# IGCSDOF Universal Bridge — RAW Engine Profiles

This document defines how `CameraMode=RAW` values are interpreted by the
bridge.

The provider sends memory values as stored by the engine. It does not perform
camera-basis math.

## UE2.5

HELLO tag:

```text
Engine=UE2.5
```

Raw storage:

```text
XYZ             float
Pitch/Yaw/Roll  signed integer Unreal Rotator
FOV             float
```

Angular scale:

```text
65536 raw units = 360 degrees
```

## UE3

HELLO tag:

```text
Engine=UE3
```

Raw storage is the same legacy Unreal Rotator convention:

```text
XYZ             float
Pitch/Yaw/Roll  signed integer Unreal Rotator
FOV             float
```

Angular scale:

```text
65536 raw units = 360 degrees
```

## UE4

HELLO tag:

```text
Engine=UE4
```

Raw storage:

```text
XYZ             float
Pitch/Yaw/Roll  float degrees
FOV             float
```

Do not call `math.deg()` on a UE4 FOV value that is already stored in degrees.
A raw FOV around 62.5 must be sent as approximately 62.5, not converted as
radians.

## idTech7

HELLO tag:

```text
Engine=idTech7
```

Raw storage:

```text
XYZ             float
Pitch/Yaw/Roll  float degrees
FOV             float
```

Validated bridge orientation conversion:

```text
pitch = radians(-rawPitch)
yaw   = radians(90 - rawYaw)
roll  = radians(rawRoll)
```

Validated basis:

```text
rightX = cy*sr*sp - cr*sy
rightY = sy*sr*sp + cr*cy
rightZ = -sr*cp

upX = -cr*cy*sp - sr*sy
upY = -cr*sy*sp + sr*cy
upZ = cr*cp
```

Validated multishot displacement:

```text
lr = incomingLR * 2.0
ud = incomingUD * 2.0

position = sessionBase + Right*lr + Up*ud
```

Panorama yaw deltas are written back as raw degrees.

### Fresh session base

For idTech 7 the bridge waits after `SESSION_BEGIN` for the provider's first
valid `CAMERA_RAW` packet with:

```text
locked=1
```

That fresh packet becomes the session base.

This prevents reuse of a stale camera from the previous session.

### Provider lifecycle requirement

The validated idTech 7 CE provider:

```text
locks pCamera
NOPs active camera writers
rewrites saved camera
publishes locked CAMERA_RAW
restores saved camera on SESSION_END
holds restore for 250 ms
restores writer bytes
unlocks pCamera
```

This is a provider-side memory-management requirement, not engine basis math.

## Northlight

HELLO tag:

```text
Engine=Northlight
```

Validated CONTROL storage:

```text
XYZ             float
Pitch/Yaw/Roll  float radians
FOV             fixed 37.0 in test provider
```

CONTROL memory mapping:

```text
X      +08
Y      +00
Z      +04
Pitch  +14
Yaw    +10
Roll   +18
```

Zero basis:

```text
Forward = +X
Right   = +Y
Up      = +Z
```

Initial multishot scaling:

```text
LR * 0.007
UD * 0.007
```

Panorama yaw delta is raw radians.

Northlight uses a fresh locked `CAMERA_RAW` after `SESSION_BEGIN` as the session
base. The validated CONTROL integration does not use writer NOPs or a pointer lock.

The Northlight RAW profile is validated on CONTROL DX12 with a successful full
IGCSDOF DoF render. CONTROL currently reports fixed FOV 37.0 because the real
Photo Mode FOV address has not yet been identified.

# IGCSDOF Universal Bridge — DualPipe Protocol v1

Protocol v1 defines transport and message framing between a camera provider
and the IGCSDOF Universal Bridge.

Engine-specific camera interpretation is defined separately in
`PROTOCOL_V2_ENGINE_PROFILES.md`.

## Pipes

Provider to bridge:

```text
\\.\pipe\IGCSDOF_ProviderToBridge_v1
```

Bridge to provider:

```text
\\.\pipe\IGCSDOF_BridgeToProvider_v1
```

Messages are UTF-8/ASCII text lines terminated by `\n`.

## HELLO

The provider sends:

```text
HELLO|Protocol=1|Transport=DualPipe|Provider=<provider>|Engine=<engine>|CameraMode=RAW
```

Example:

```text
HELLO|Protocol=1|Transport=DualPipe|Provider=Cheat Engine|Engine=idTech7|CameraMode=RAW
```

Current identity fields are:

```text
Provider
Engine
CameraMode
```

There is no game-name field.

## CAMERA_RAW

Provider to bridge:

```text
CAMERA_RAW|valid|enabled|locked|x|y|z|pitch|yaw|roll|fov
```

Meaning:

```text
valid    1 when all camera fields are readable
enabled  1 when the camera is active/usable
locked   1 while an IGCSDOF session owns the camera
x/y/z    raw camera world position
pitch    raw engine pitch
yaw      raw engine yaw
roll     raw engine roll
fov      raw float FOV
```

If the provider cannot resolve a camera it may send:

```text
CAMERA_INVALID
```

## SESSION_BEGIN

Bridge to provider:

```text
SESSION_BEGIN|type
```

The provider must capture a fresh camera state before accepting movement
commands.

The provider may also lock its camera pointer and active camera writers when
required by the engine/camera implementation.

For the validated idTech 7 lifecycle, the provider then immediately publishes
a fresh `CAMERA_RAW` with `locked=1`. The bridge uses that packet as the
authoritative session base.

## SET_POSITION_RAW

Bridge to provider:

```text
SET_POSITION_RAW|x|y|z|fov|fromStart
```

The XYZ values are already calculated by the bridge.

The provider must write the raw camera position and must **not** reconstruct
Right/Up/Forward.

`fov` and `fromStart` are protocol fields available for profiles that require
them.

## ROTATE_YAW_RAW

Bridge to provider:

```text
ROTATE_YAW_RAW|rawAngleDelta
```

The delta is already converted by the bridge to the raw angle unit expected by
the selected engine profile.

Examples:

```text
UE2.5 / UE3  -> Unreal Rotator units
UE4          -> degrees
idTech7      -> degrees
```

The provider normally applies:

```text
rawYaw = rawYaw + rawAngleDelta
```

## SESSION_END

Bridge to provider:

```text
SESSION_END
```

The provider restores the camera captured at `SESSION_BEGIN`.

Profiles may require a delayed release of camera writers. The validated
idTech 7 implementation forces the restored camera for 250 ms before restoring
writer bytes and unlocking the camera pointer.

## Provider-ready rule

The bridge does not notify IGCS Connector that camera tools are ready until:

```text
provider-to-bridge pipe connected
bridge-to-provider pipe connected
HELLO received
valid camera received
```

This preserves the stable startup ordering used by the working bridge.

## Responsibilities

Provider owns:

```text
memory discovery
raw camera reads/writes
camera save/restore
optional pointer lock
optional writer lock
```

Bridge owns:

```text
engine angle interpretation
Right / Up / Forward basis
multishot displacement
Bokeh scaling
focus-plane rules
panorama angle conversion
```

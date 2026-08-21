# DOF Backends — IGCSDOF and Parallax DOF

IGCS Relay v0.6.9 can work with either **IGCSDOF** or **MARTY'S MODS Parallax DOF** without restarting the game or ReShade.

## Required files

Use the architecture matching the game:

```text
IGCSRelay.addon32 / IGCSRelay.addon64
IgcsConnector.addon32 / IgcsConnector.addon64
MartysMods_ParallaxDOF.addon32 / MartysMods_ParallaxDOF.addon64
```

You may load only one DOF addon, or both at the same time.

## Selecting a backend

Open the ReShade Add-ons tab and expand **IGCSRelay**.

Under **DOF Integration**, choose:

```text
IGCSDOF
```

or:

```text
Parallax DOF
```

The selected backend receives the live camera state. A second detected backend remains connected but receives `cameraEnabled = 0`, so it stays ready without controlling the camera.

## Switching

When no screenshot session is active, switching is immediate.

During a screenshot session the selector is locked. Finish or cancel the current render first, then switch.

If a render is started directly from either DOF addon, Relay identifies which addon called the IGCS screenshot-session export and automatically marks that backend as active.

## Overlay states

```text
Active                    Selected backend receiving the live camera
Ready                     Connected, but currently inactive
Detected / not connected  Addon found but CameraTools connection is not ready
Not found                 Addon is not loaded
```

Example while rendering with Parallax DOF:

```text
DOF backend        Parallax DOF
IGCSDOF addon      Ready
Parallax DOF addon Active
Session            Rendering
```

## Compatibility contract

Both backends use the same CameraTools bridge interface:

```text
connectFromCameraTools
getDataFromCameraToolsBuffer
```

and the same IGCS command exports from Relay:

```text
IGCS_StartScreenshotSession
IGCS_EndScreenshotSession
IGCS_MoveCameraMultishot
IGCS_MoveCameraPanorama
```

Relay keeps the provider side unchanged. Cheat Engine, native providers, engine profiles and camera math do not need separate Parallax-specific implementations.

## Troubleshooting

If a backend shows **Not found**, confirm that its `.addon32`/`.addon64` file matches the game architecture and is loaded by ReShade.

If it shows **Detected / not connected**, make sure the camera provider is running and Relay shows the provider, camera channel, command channel and camera data as ready.

If the wrong backend appears active during a render, update to v0.6.9 or newer. v0.6.9 adds caller detection so a session started from Parallax DOF selects Parallax, and a session started from IGCSDOF selects IGCSDOF.

## Scope of v0.6.9

The DOF selector changes only backend discovery, routing and UI state. It does **not** change engine math, camera basis reconstruction, multishot movement, panorama math, bokeh scales or the provider protocol.

# Generic RAW Provider — Quick Guide

## Only edit CONFIG

Set:

```text
engine
bases
position offsets
rotation offsets
FOV offset
idTech7 lock/writers when applicable
```

## Base examples

Simple:

```lua
bases = {
    position = "pCamera",
    rotation = "pCamera",
    fov      = "pCamera"
}
```

Split:

```lua
bases = {
    position = "getCamBase",
    rotation = "addr1",
    fov      = "addr1"
}
```

Supported sources:

```text
Lua numeric global
Lua function
CE pointer symbol
```

Everything below `END USER CONFIGURATION` is provider core.

# Reference Camera Tables

## DMC-DevilMayCry.CT

UE3, current v6 provider.

```text
Base   pCamera
X      +56C
Y      +570
Z      +574
Pitch  +578
Yaw    +57C
Roll   +580
FOV    +584
```

## DOOMEternalx64vk.CT

idTech7, current v6 provider.

```text
Base   pCamera
X      +D68
Y      +D64
Z      +D6C
Pitch  +BC4
Yaw    +BC8
Roll   +BCC
FOV    +D78
```

Special idTech7 lifecycle:

```text
pCameraLock
7 writer locks
250 ms restore hold
```

## Control_DX12.CT

Validated Northlight reference.

```text
X      +08
Y      +00
Z      +04
Pitch  +14 radians
Yaw    +10 radians
Roll   +18 radians
FOV    37.0 fixed
```

------------------------------------------------------------
-- IGCSDOF UNIVERSAL RAW PROVIDER v4
--
-- Generic Cheat Engine -> IGCSDOF Universal Bridge
--
-- NO GAME NAME.
-- NO CAMERA-BASIS MATH.
--
-- Normally you only change:
--
--   1. engine
--   2. cameraSymbol
--   3. position offsets
--   4. rotation offsets
--   5. FOV offset
--   6. optional cameraLockSymbol
--   7. optional writer instructions
--
-- Supported engine tags:
--
--   UE2.5
--   UE3
--   UE4
--   idTech7
--   Northlight
--
-- Engine-specific camera math stays in the bridge.
------------------------------------------------------------

------------------------------------------------------------
-- USER CONFIGURATION
------------------------------------------------------------

local CONFIG = {

  ----------------------------------------------------------
  -- ENGINE
  ----------------------------------------------------------

  engine = "UE3",

  ----------------------------------------------------------
  -- BASES
  --
  -- Each camera data group can use its own base.
  --
  -- Supported base values:
  --
  --   "pCamera"
  --     -> CE symbol containing a pointer
  --
  --   "cam", "addr1", etc.
  --     -> existing Lua global variable from Camera/Detach
  --
  --   "getCamBase", etc.
  --     -> existing Lua global function returning the base
  --
  -- The provider does NOT rebuild pointer chains when those
  -- globals already exist.
  ----------------------------------------------------------

  bases = {
    position = "getCamBase",
    rotation = "addr1",
    fov      = "addr1"
  },

  ----------------------------------------------------------
  -- POSITION
  ----------------------------------------------------------

  position = {
    x = 0x1D8,
    y = 0x1DC,
    z = 0x1E0
  },

  ----------------------------------------------------------
  -- ROTATION
  ----------------------------------------------------------

  rotation = {
    pitch = 0x1E4,
    yaw   = 0x1E8,
    roll  = 0x1EC
  },

  ----------------------------------------------------------
  -- FOV
  ----------------------------------------------------------

  fov = {
    offset = 0x45C
  },

  ----------------------------------------------------------
  -- OPTIONAL IDTECH7 SPECIALS
  --
  -- Used ONLY when engine == "idTech7".
  ----------------------------------------------------------

  cameraLockSymbol = "pCameraLock",

  writers = {
    {
      address = "DOOMEternalx64vk.exe+DC3F74",
      bytes = {0xF2,0x0F,0x11,0x8B,0x64,0x0D,0x00,0x00}
    },
    {
      address = "DOOMEternalx64vk.exe+DC3F7C",
      bytes = {0x89,0x83,0x6C,0x0D,0x00,0x00}
    },
    {
      address = "DOOMEternalx64vk.exe+DCC63B",
      bytes = {0xF3,0x41,0x0F,0x11,0x07}
    },
    {
      address = "DOOMEternalx64vk.exe+DCC64E",
      bytes = {0x41,0x89,0x4F,0x04}
    },
    {
      address = "DOOMEternalx64vk.exe+DCC658",
      bytes = {0x41,0x89,0x47,0x08}
    },
    {
      address = "DOOMEternalx64vk.exe+DCC664",
      bytes = {0xF3,0x41,0x0F,0x11,0x17}
    },
    {
      address = "DOOMEternalx64vk.exe+DCC716",
      bytes = {0xF3,0x0F,0x11,0x9B,0x78,0x0D,0x00,0x00}
    }
  },

  restoreHoldMs = 250,
  cameraIntervalMs = 16
}

------------------------------------------------------------
-- END USER CONFIGURATION
--
-- STOP EDITING HERE.
-- Everything below this line is universal provider core.
------------------------------------------------------------

local CAMERA_PIPE_NAME =
  "IGCSDOF_ProviderToBridge_v1"

local COMMAND_PIPE_NAME =
  "IGCSDOF_BridgeToProvider_v1"

local RECONNECT_DELAY_MS = 1000

------------------------------------------------------------
-- ENGINE PROFILE
--
-- IMPORTANT:
-- This section defines ONLY provider-side memory behavior.
-- It does not define camera basis math.
------------------------------------------------------------

local ENGINE_PROFILE = {

  ["UE2.5"] = {
    rotationType = "integer",
    forcePointerLock = false,
    forceWriterLock = false,
    useRestoreHold = false
  },

  ["UE3"] = {
    rotationType = "integer",
    forcePointerLock = false,
    forceWriterLock = false,
    useRestoreHold = false
  },

  ["UE4"] = {
    rotationType = "float",
    forcePointerLock = false,
    forceWriterLock = false,
    useRestoreHold = false
  },

  ["idTech7"] = {
    rotationType = "float",

    -- Validated behavior:
    forcePointerLock = true,
    forceWriterLock = true,
    useRestoreHold = true
  },

  ["Northlight"] = {
    rotationType = "float",
    forcePointerLock = false,
    forceWriterLock = false,
    useRestoreHold = false
  }
}

local function getEngineProfile()

  return
    ENGINE_PROFILE[CONFIG.engine] or
    ENGINE_PROFILE["UE4"]

end

local function usesIntegerRotator()

  return
    getEngineProfile().rotationType == "integer"

end

------------------------------------------------------------
-- GENERATION / STATE
------------------------------------------------------------

IGCSDOF_PROVIDER_GENERATION =
  (IGCSDOF_PROVIDER_GENERATION or 0) + 1

local myGeneration =
  IGCSDOF_PROVIDER_GENERATION

IGCSDOF_PROVIDER_CONTEXTS =
  IGCSDOF_PROVIDER_CONTEXTS or {}

local ctx = {

  active = true,

  cameraPipe = nil,
  commandPipe = nil,

  latestCameraLine = nil,
  cameraSequence = 0,

  sessionActive = false,
  savedCamera = nil,

  restorePending = false,
  restoreCamera = nil,
  restoreStart = 0,

  writersLocked = false
}

IGCSDOF_PROVIDER_CONTEXTS[myGeneration] = ctx

local unpackFn =
  table.unpack or unpack

local function isCurrent()

  return
    ctx.active and
    IGCSDOF_PROVIDER_GENERATION == myGeneration

end

------------------------------------------------------------
-- HELPERS
------------------------------------------------------------

local function safeDestroy(pipe)

  if pipe then

    pcall(function()
      pipe.destroy()
    end)

  end

end

local function safeWrite(pipe, line)

  return pcall(function()
    pipe.writeString(line .. "\n")
  end)

end

local function splitLine(line)

  local result = {}

  for value in string.gmatch(line, "([^|]+)") do
    result[#result + 1] = value
  end

  return result

end

local function roundInteger(value)

  if value >= 0 then
    return math.floor(value + 0.5)
  end

  return math.ceil(value - 0.5)

end

local function makeNops(count)

  local result = {}

  for i = 1, count do
    result[i] = 0x90
  end

  return result

end

------------------------------------------------------------
-- CAMERA POINTER
------------------------------------------------------------

local function resolveBase(kind)

  if not CONFIG.bases then
    return nil
  end

  local name = CONFIG.bases[kind]

  if not name or name == "" then
    return nil
  end

  ----------------------------------------------------------
  -- 1. Existing Lua global variable containing an address
  ----------------------------------------------------------

  local globalValue = rawget(_G, name)

  if type(globalValue) == "number" and globalValue ~= 0 then
    return globalValue
  end

  ----------------------------------------------------------
  -- 2. Existing Lua global function returning an address
  --
  -- Example:
  --   position = "getCamBase"
  --
  -- Calling getCamBase() can also populate helper globals
  -- such as addr1, which may then be used by rotation/FOV.
  ----------------------------------------------------------

  if type(globalValue) == "function" then

    local ok, address =
      pcall(globalValue)

    if ok and
       type(address) == "number" and
       address ~= 0 then

      return address

    end

    return nil
  end

  ----------------------------------------------------------
  -- 3. CE registered symbol containing a pointer
  ----------------------------------------------------------

  local symbolAddress =
    getAddressSafe(name)

  if not symbolAddress then
    return nil
  end

  local pointerValue =
    readPointer(symbolAddress)

  if not pointerValue or
     pointerValue == 0 then
    return nil
  end

  return pointerValue

end

------------------------------------------------------------
-- CAMERA POINTER LOCK
------------------------------------------------------------

local function setCameraPointerLock(locked)

  if not CONFIG.cameraLockSymbol or
     CONFIG.cameraLockSymbol == "" then

    return

  end

  local address =
    getAddressSafe(CONFIG.cameraLockSymbol)

  if not address then
    return
  end

  writeBytes(
    address,
    locked and 1 or 0
  )

end

------------------------------------------------------------
-- RAW ROTATION READ / WRITE
------------------------------------------------------------

local function readRotation(address)

  if usesIntegerRotator() then
    return readInteger(address)
  end

  return readFloat(address)

end

local function writeRotation(address, value)

  if usesIntegerRotator() then

    writeInteger(
      address,
      roundInteger(value)
    )

  else

    writeFloat(
      address,
      value
    )

  end

end

------------------------------------------------------------
-- CAMERA READ
------------------------------------------------------------

local function readCamera()

  local posBase = resolveBase("position")
  local rotBase = resolveBase("rotation")
  local fovBase = resolveBase("fov")

  if not posBase or not rotBase or not fovBase then
    return nil
  end

  local camera = {

    x = readFloat(
      posBase + CONFIG.position.x
    ),

    y = readFloat(
      posBase + CONFIG.position.y
    ),

    z = readFloat(
      posBase + CONFIG.position.z
    ),

    pitch = readRotation(
      rotBase + CONFIG.rotation.pitch
    ),

    yaw = readRotation(
      rotBase + CONFIG.rotation.yaw
    ),

    roll = readRotation(
      rotBase + CONFIG.rotation.roll
    ),

    fov = readFloat(
      fovBase + CONFIG.fov.offset
    )
  }

  if camera.x == nil or
     camera.y == nil or
     camera.z == nil or
     camera.pitch == nil or
     camera.yaw == nil or
     camera.roll == nil or
     camera.fov == nil then
    return nil
  end

  return camera

end

------------------------------------------------------------
-- CAMERA WRITE
------------------------------------------------------------

local function cloneCamera(camera)

  if not camera then
    return nil
  end

  return {

    x = camera.x,
    y = camera.y,
    z = camera.z,

    pitch = camera.pitch,
    yaw = camera.yaw,
    roll = camera.roll,

    fov = camera.fov
  }

end

local function writeCamera(camera)

  if not camera then
    return false
  end

  local posBase = resolveBase("position")
  local rotBase = resolveBase("rotation")
  local fovBase = resolveBase("fov")

  if not posBase or not rotBase or not fovBase then
    return false
  end

  writeFloat(
    posBase + CONFIG.position.x,
    camera.x
  )

  writeFloat(
    posBase + CONFIG.position.y,
    camera.y
  )

  writeFloat(
    posBase + CONFIG.position.z,
    camera.z
  )

  writeRotation(
    rotBase + CONFIG.rotation.pitch,
    camera.pitch
  )

  writeRotation(
    rotBase + CONFIG.rotation.yaw,
    camera.yaw
  )

  writeRotation(
    rotBase + CONFIG.rotation.roll,
    camera.roll
  )

  writeFloat(
    fovBase + CONFIG.fov.offset,
    camera.fov
  )

  return true

end

------------------------------------------------------------
-- CAMERA WRITER LOCK
------------------------------------------------------------

local function enableWriterLock()

  if ctx.writersLocked then
    return
  end

  for _, writer in ipairs(CONFIG.writers or {}) do

    local address =
      getAddressSafe(writer.address)

    if address and writer.bytes then

      writeBytes(
        address,
        unpackFn(
          makeNops(#writer.bytes)
        )
      )

    end

  end

  ctx.writersLocked = true

end

local function disableWriterLock()

  if not ctx.writersLocked then
    return
  end

  for _, writer in ipairs(CONFIG.writers or {}) do

    local address =
      getAddressSafe(writer.address)

    if address and writer.bytes then

      writeBytes(
        address,
        unpackFn(writer.bytes)
      )

    end

  end

  ctx.writersLocked = false

end

------------------------------------------------------------
-- CAMERA_RAW MESSAGE
------------------------------------------------------------

local function makeCameraLine()

  local camera =
    readCamera()

  if not camera then
    return "CAMERA_INVALID"
  end

  local locked =
    ctx.sessionActive or
    ctx.restorePending

  local rotationFormat =
    usesIntegerRotator()
      and "%d|%d|%d"
      or "%.9f|%.9f|%.9f"

  local format =
    "CAMERA_RAW|1|1|%d|" ..
    "%.9f|%.9f|%.9f|" ..
    rotationFormat ..
    "|%.9f"

  return string.format(

    format,

    locked and 1 or 0,

    camera.x,
    camera.y,
    camera.z,

    camera.pitch,
    camera.yaw,
    camera.roll,

    camera.fov

  )

end

------------------------------------------------------------
-- ENGINE-SPECIFIC PROVIDER LIFECYCLE
------------------------------------------------------------

local function beginProviderLock()

  ----------------------------------------------------------
  -- ENGINE-SPECIFIC PROVIDER LIFECYCLE
  --
  -- idTech7 is the only current profile that uses the
  -- validated camera pointer lock + camera writer lock.
  --
  -- Leaving the idTech7 writer list populated in CONFIG is
  -- harmless for UE / Northlight: those addresses are never
  -- resolved or modified.
  ----------------------------------------------------------

  if CONFIG.engine == "idTech7" then

    setCameraPointerLock(true)
    enableWriterLock()

  end

end

local function releaseProviderLock()

  if CONFIG.engine == "idTech7" then

    disableWriterLock()
    setCameraPointerLock(false)

  end

end

------------------------------------------------------------
-- SESSION BEGIN
------------------------------------------------------------

local function startSession()

  if ctx.sessionActive then
    return
  end

  local camera =
    readCamera()

  if not camera then
    return
  end

  -- Fresh camera read for every render.
  ctx.savedCamera =
    cloneCamera(camera)

  beginProviderLock()

  -- Important for idTech7 and harmless for other profiles:
  -- re-apply saved values after camera writers are locked.
  writeCamera(ctx.savedCamera)

  ctx.sessionActive = true

  -- Publish locked=1 immediately.
  ctx.latestCameraLine =
    makeCameraLine()

  ctx.cameraSequence =
    ctx.cameraSequence + 1

end

------------------------------------------------------------
-- SESSION END
------------------------------------------------------------

local function finishRestore()

  if ctx.restoreCamera then
    writeCamera(ctx.restoreCamera)
  end

  releaseProviderLock()

  ctx.savedCamera = nil
  ctx.restoreCamera = nil

  ctx.restorePending = false
  ctx.restoreStart = 0

end

local function endSession()

  local profile =
    getEngineProfile()

  if ctx.savedCamera then

    ctx.restoreCamera =
      cloneCamera(ctx.savedCamera)

    writeCamera(ctx.restoreCamera)

  end

  ctx.sessionActive = false

  ----------------------------------------------------------
  -- idTech7:
  -- validated 250 ms forced restore before releasing writers.
  --
  -- Other engines:
  -- restore and release immediately unless their profile is
  -- later configured to use a hold.
  ----------------------------------------------------------

  if profile.useRestoreHold then

    ctx.restorePending = true

    ctx.restoreStart =
      getTickCount()

  else

    ctx.restorePending = false

    finishRestore()

  end

end

local function processPendingRestore()

  if not ctx.restorePending then
    return
  end

  -- Keep forcing saved camera while writers remain locked.
  if ctx.restoreCamera then
    writeCamera(ctx.restoreCamera)
  end

  if
    getTickCount() - ctx.restoreStart
      <
    (CONFIG.restoreHoldMs or 250)
  then

    return

  end

  -- Final restore while still locked.
  finishRestore()

end

------------------------------------------------------------
-- COMMANDS FROM BRIDGE
------------------------------------------------------------

local function setPositionRaw(x, y, z, fov)

  local posBase = resolveBase("position")

  if not posBase then
    return
  end

  writeFloat(
    posBase + CONFIG.position.x,
    x
  )

  writeFloat(
    posBase + CONFIG.position.y,
    y
  )

  writeFloat(
    posBase + CONFIG.position.z,
    z
  )

end

local function rotateYawRaw(delta)

  local rotBase = resolveBase("rotation")

  if not rotBase then
    return
  end

  local address =
    rotBase + CONFIG.rotation.yaw

  local yaw =
    readRotation(address) or 0

  writeRotation(
    address,
    yaw + delta
  )

end

local function handleCommand(line)

  if not isCurrent() then
    return
  end

  local parts =
    splitLine(line)

  local command =
    parts[1]

  if command == "SESSION_BEGIN" then

    startSession()

  elseif command == "SESSION_END" then

    endSession()

  elseif command == "SET_POSITION_RAW" then

    setPositionRaw(
      tonumber(parts[2]) or 0,
      tonumber(parts[3]) or 0,
      tonumber(parts[4]) or 0,
      tonumber(parts[5]) or 0
    )

  elseif command == "ROTATE_YAW_RAW" then

    rotateYawRaw(
      tonumber(parts[2]) or 0
    )

  end

end

------------------------------------------------------------
-- PROVIDER -> BRIDGE PIPE
------------------------------------------------------------

local function cameraSenderWorker()

  local pipe = nil

  local sentSequence = -1

  while isCurrent() do

    if not pipe then

      local ok, candidate =
        pcall(function()

          return connectToPipe(
            CAMERA_PIPE_NAME,
            0
          )

        end)

      if not isCurrent() then

        if ok and candidate then
          safeDestroy(candidate)
        end

        break

      end

      if ok and candidate then

        pipe = candidate

        ctx.cameraPipe = pipe

        sentSequence = -1

        local hello =
          "HELLO|Protocol=1" ..
          "|Transport=DualPipe" ..
          "|Provider=Cheat Engine" ..
          "|Engine=" .. CONFIG.engine ..
          "|CameraMode=RAW"

        if not safeWrite(pipe, hello) then

          safeDestroy(pipe)

          pipe = nil
          ctx.cameraPipe = nil

        end

      else

        sleep(RECONNECT_DELAY_MS)

      end

    end

    if pipe and isCurrent() then

      local sequence =
        ctx.cameraSequence

      local line =
        ctx.latestCameraLine

      if line and
         sequence ~= sentSequence then

        if safeWrite(pipe, line) then

          sentSequence = sequence

        else

          safeDestroy(pipe)

          pipe = nil
          ctx.cameraPipe = nil

          sleep(RECONNECT_DELAY_MS)

        end

      else

        sleep(2)

      end

    end

  end

  safeDestroy(pipe)

  ctx.cameraPipe = nil

end

------------------------------------------------------------
-- BRIDGE -> PROVIDER PIPE
------------------------------------------------------------

local function commandReceiverWorker()

  local pipe = nil

  local pending = ""

  while isCurrent() do

    if not pipe then

      local ok, candidate =
        pcall(function()

          return connectToPipe(
            COMMAND_PIPE_NAME,
            0
          )

        end)

      if not isCurrent() then

        if ok and candidate then
          safeDestroy(candidate)
        end

        break

      end

      if ok and candidate then

        pipe = candidate

        ctx.commandPipe = pipe

        pending = ""

      else

        sleep(RECONNECT_DELAY_MS)

      end

    end

    if pipe and isCurrent() then

      local ok, byte =
        pcall(function()

          return pipe.readByte()

        end)

      if not ok then

        safeDestroy(pipe)

        pipe = nil

        ctx.commandPipe = nil

        sleep(RECONNECT_DELAY_MS)

      elseif byte ~= nil then

        if byte == 10 then

          local line =
            pending

          pending = ""

          if line:sub(-1) == "\r" then

            line =
              line:sub(1, -2)

          end

          if
            line ~= "" and
            line:sub(1, 7) ~= "WELCOME"
          then

            synchronize(function()

              if isCurrent() then
                handleCommand(line)
              end

            end)

          end

        else

          pending =
            pending ..
            string.char(byte)

        end

      end

    end

  end

  safeDestroy(pipe)

  ctx.commandPipe = nil

end

------------------------------------------------------------
-- START
------------------------------------------------------------

if IGCSDOF_PROVIDER_CAMERA_TIMER then

  IGCSDOF_PROVIDER_CAMERA_TIMER.destroy()

end

IGCSDOF_PROVIDER_CAMERA_TIMER =
  createTimer(nil, false)

IGCSDOF_PROVIDER_CAMERA_TIMER.Interval =
  CONFIG.cameraIntervalMs or 16

IGCSDOF_PROVIDER_CAMERA_TIMER.OnTimer =
  function()

    if not isCurrent() then
      return
    end

    processPendingRestore()

    ctx.latestCameraLine =
      makeCameraLine()

    ctx.cameraSequence =
      ctx.cameraSequence + 1

  end

IGCSDOF_PROVIDER_CAMERA_TIMER.Enabled =
  true

ctx.latestCameraLine =
  makeCameraLine()

ctx.cameraSequence = 1

ctx.senderThread =
  createThread(cameraSenderWorker)

ctx.receiverThread =
  createThread(commandReceiverWorker)

ctx.cleanup =
  function()

    if ctx.savedCamera then
      writeCamera(ctx.savedCamera)
    end

    releaseProviderLock()

    safeDestroy(ctx.cameraPipe)

    safeDestroy(ctx.commandPipe)

  end

print(
  "[IGCSDOF] Universal RAW provider started | Engine=" ..
  CONFIG.engine
)

------------------------------------------------------------
-- STOP
--
-- In CT [DISABLE]:
--
-- if IGCSDOF_PROVIDER_STOP then
--   IGCSDOF_PROVIDER_STOP()
-- end
------------------------------------------------------------

IGCSDOF_PROVIDER_STOP =
function()

  local old =
    IGCSDOF_PROVIDER_CONTEXTS and
    IGCSDOF_PROVIDER_CONTEXTS[myGeneration]
    or nil

  IGCSDOF_PROVIDER_GENERATION =
    (IGCSDOF_PROVIDER_GENERATION or 0) + 1

  if IGCSDOF_PROVIDER_CAMERA_TIMER then

    IGCSDOF_PROVIDER_CAMERA_TIMER.destroy()

    IGCSDOF_PROVIDER_CAMERA_TIMER = nil

  end

  if old then

    old.active = false

    if old.cleanup then
      pcall(old.cleanup)
    end

    IGCSDOF_PROVIDER_CONTEXTS[myGeneration] =
      nil

  end

  print(
    "[IGCSDOF] Universal RAW provider stopped."
  )

end

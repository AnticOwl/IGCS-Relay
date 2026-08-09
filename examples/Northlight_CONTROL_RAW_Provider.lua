------------------------------------------------------------
-- IGCSDOF NORTHLIGHT RAW PROVIDER - CONTROL
--
-- Requires the CT camera finder to expose:
--   pControlPM -> points directly to the Photo Mode data block
--
-- Native layout:
--   +00 = Y position (float)
--   +04 = Z position (float)
--   +08 = X position (float)
--   +10 = Yaw   (float radians)
--   +14 = Pitch (float radians)
--   +18 = Roll  (float radians)
--
-- FOV address is not yet known in this profile.
-- A fixed FOV of 37.0 is reported, matching the validated CONTROL integration.
------------------------------------------------------------

local CONFIG = {
  engine = "Northlight",
  cameraSymbol = "pControlPM",
  fixedFov = 37.0,
  cameraIntervalMs = 16
}

local CAMERA_PIPE_NAME = "IGCSDOF_ProviderToBridge_v1"
local COMMAND_PIPE_NAME = "IGCSDOF_BridgeToProvider_v1"
local RECONNECT_DELAY_MS = 1000

IGCSDOF_PROVIDER_GENERATION = (IGCSDOF_PROVIDER_GENERATION or 0) + 1
local myGeneration = IGCSDOF_PROVIDER_GENERATION
IGCSDOF_PROVIDER_CONTEXTS = IGCSDOF_PROVIDER_CONTEXTS or {}

local ctx = {
  active = true,
  cameraPipe = nil,
  commandPipe = nil,
  latestCameraLine = nil,
  cameraSequence = 0,
  sessionActive = false,
  savedCamera = nil
}
IGCSDOF_PROVIDER_CONTEXTS[myGeneration] = ctx

local function isCurrent()
  return ctx.active and IGCSDOF_PROVIDER_GENERATION == myGeneration
end

local function safeDestroy(pipe)
  if pipe then pcall(function() pipe.destroy() end) end
end

local function safeWrite(pipe, line)
  return pcall(function() pipe.writeString(line .. "\n") end)
end

local function splitLine(line)
  local result = {}
  for value in string.gmatch(line, "([^|]+)") do
    result[#result + 1] = value
  end
  return result
end

local function getCameraBase()
  local sym = getAddressSafe(CONFIG.cameraSymbol)
  if not sym then return nil end
  local p = readPointer(sym)
  if not p or p == 0 then return nil end
  return p
end

local function validFloat(v)
  return v ~= nil and v == v and math.abs(v) < 100000000.0
end

local function readCamera()
  local base = getCameraBase()
  if not base then return nil end

  local y = readFloat(base + 0x00)
  local z = readFloat(base + 0x04)
  local x = readFloat(base + 0x08)
  local yaw = readFloat(base + 0x10)
  local pitch = readFloat(base + 0x14)
  local roll = readFloat(base + 0x18)

  if not (validFloat(x) and validFloat(y) and validFloat(z)
      and validFloat(pitch) and validFloat(yaw) and validFloat(roll)) then
    return nil
  end

  return {
    x=x, y=y, z=z,
    pitch=pitch, yaw=yaw, roll=roll,
    fov=CONFIG.fixedFov
  }
end

local function cloneCamera(c)
  if not c then return nil end
  return {
    x=c.x, y=c.y, z=c.z,
    pitch=c.pitch, yaw=c.yaw, roll=c.roll,
    fov=c.fov
  }
end

local function writePosition(c)
  local base = getCameraBase()
  if not base or not c then return false end

  writeFloat(base + 0x00, c.y)
  writeFloat(base + 0x04, c.z)
  writeFloat(base + 0x08, c.x)
  return true
end

local function writeYaw(value)
  local base = getCameraBase()
  if not base then return false end
  writeFloat(base + 0x10, value)
  return true
end

local function makeCameraLine()
  local c = readCamera()
  if not c then return "CAMERA_INVALID" end

  return string.format(
    "CAMERA_RAW|1|1|%d|%.9f|%.9f|%.9f|%.9f|%.9f|%.9f|%.9f",
    ctx.sessionActive and 1 or 0,
    c.x, c.y, c.z,
    c.pitch, c.yaw, c.roll,
    c.fov
  )
end

local function publishNow()
  ctx.latestCameraLine = makeCameraLine()
  ctx.cameraSequence = ctx.cameraSequence + 1
end

local function startSession()
  if ctx.sessionActive then return end

  local c = readCamera()
  if not c then
    print("[IGCSDOF] Northlight SESSION_BEGIN: camera unavailable")
    return
  end

  ctx.savedCamera = cloneCamera(c)
  ctx.sessionActive = true

  -- Important: immediately publish the exact fresh camera used as
  -- savedCamera with locked=1 so the bridge can use it as session base.
  publishNow()

  print(string.format(
    "[IGCSDOF] Northlight START XYZ=(%.6f %.6f %.6f) PYRrad=(%.6f %.6f %.6f)",
    c.x, c.y, c.z, c.pitch, c.yaw, c.roll
  ))
end

local function endSession()
  if ctx.savedCamera then
    writePosition(ctx.savedCamera)
  end

  ctx.sessionActive = false
  ctx.savedCamera = nil
  publishNow()

  print("[IGCSDOF] Northlight END restored")
end

local function setPositionRaw(x, y, z)
  writePosition({x=x, y=y, z=z})
end

local function rotateYawRaw(delta)
  local base = getCameraBase()
  if not base then return end
  local yaw = readFloat(base + 0x10) or 0.0
  writeYaw(yaw + delta)
end

local function handleCommand(line)
  local parts = splitLine(line)
  local cmd = parts[1]

  if cmd == "SESSION_BEGIN" then
    startSession()
  elseif cmd == "SESSION_END" then
    endSession()
  elseif cmd == "SET_POSITION_RAW" then
    setPositionRaw(
      tonumber(parts[2]) or 0,
      tonumber(parts[3]) or 0,
      tonumber(parts[4]) or 0
    )
  elseif cmd == "ROTATE_YAW_RAW" then
    rotateYawRaw(tonumber(parts[2]) or 0)
  end
end

local function cameraSenderWorker()
  local pipe = nil
  local sentSequence = -1

  while isCurrent() do
    if not pipe then
      local ok, candidate = pcall(function()
        return connectToPipe(CAMERA_PIPE_NAME, 0)
      end)

      if ok and candidate then
        pipe = candidate
        ctx.cameraPipe = pipe
        sentSequence = -1

        local hello =
          "HELLO|Protocol=1" ..
          "|Transport=DualPipe" ..
          "|Provider=Cheat Engine" ..
          "|Engine=Northlight" ..
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
      local seq = ctx.cameraSequence
      if ctx.latestCameraLine and seq ~= sentSequence then
        if safeWrite(pipe, ctx.latestCameraLine) then
          sentSequence = seq
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

local function commandReceiverWorker()
  local pipe = nil
  local pending = ""

  while isCurrent() do
    if not pipe then
      local ok, candidate = pcall(function()
        return connectToPipe(COMMAND_PIPE_NAME, 0)
      end)

      if ok and candidate then
        pipe = candidate
        ctx.commandPipe = pipe
        pending = ""
      else
        sleep(RECONNECT_DELAY_MS)
      end
    end

    if pipe and isCurrent() then
      local ok, byte = pcall(function() return pipe.readByte() end)

      if not ok then
        safeDestroy(pipe)
        pipe = nil
        ctx.commandPipe = nil
        sleep(RECONNECT_DELAY_MS)
      elseif byte ~= nil then
        if byte == 10 then
          local line = pending
          pending = ""
          if line:sub(-1) == "\r" then line = line:sub(1, -2) end

          if line ~= "" and line:sub(1, 7) ~= "WELCOME" then
            synchronize(function()
              if isCurrent() then handleCommand(line) end
            end)
          end
        else
          pending = pending .. string.char(byte)
        end
      end
    end
  end

  safeDestroy(pipe)
  ctx.commandPipe = nil
end

if IGCSDOF_PROVIDER_CAMERA_TIMER then
  IGCSDOF_PROVIDER_CAMERA_TIMER.destroy()
end

IGCSDOF_PROVIDER_CAMERA_TIMER = createTimer(nil, false)
IGCSDOF_PROVIDER_CAMERA_TIMER.Interval = CONFIG.cameraIntervalMs
IGCSDOF_PROVIDER_CAMERA_TIMER.OnTimer = function()
  if not isCurrent() then return end
  publishNow()
end
IGCSDOF_PROVIDER_CAMERA_TIMER.Enabled = true

publishNow()
ctx.senderThread = createThread(cameraSenderWorker)
ctx.receiverThread = createThread(commandReceiverWorker)

IGCSDOF_PROVIDER_STOP = function()
  IGCSDOF_PROVIDER_GENERATION = (IGCSDOF_PROVIDER_GENERATION or 0) + 1
  ctx.active = false

  if IGCSDOF_PROVIDER_CAMERA_TIMER then
    IGCSDOF_PROVIDER_CAMERA_TIMER.destroy()
    IGCSDOF_PROVIDER_CAMERA_TIMER = nil
  end

  if ctx.savedCamera then
    writePosition(ctx.savedCamera)
  end

  safeDestroy(ctx.cameraPipe)
  safeDestroy(ctx.commandPipe)

  print("[IGCSDOF] Northlight RAW provider stopped")
end

print("[IGCSDOF] Northlight RAW provider started")
print("[IGCSDOF] CONTROL mapping: X=+08 Y=+00 Z=+04")
print("[IGCSDOF] Rotation RAW radians: Yaw=+10 Pitch=+14 Roll=+18")
print("[IGCSDOF] FOV value: 37.0 (fixed)")

[ENABLE]
{$lua}
if syntaxcheck then return end

IGCSDOF_SESSION_ACTIVE = false

------------------------------------------------------------
-- IGCSDOF UNIVERSAL RAW PROVIDER v4 - AUTO CAMERA RECORDS
--
-- AUTO:
--   X / Y / Z / Pitch / Yaw / Roll / FOV
--   direct addresses
--   CE symbols
--   pointer records
--   pointer chains
--   different chains/bases for position/rotation/FOV
--
-- ONLY MANUAL VALUE LEFT:
--   engine (Relay camera math profile)
------------------------------------------------------------

local CONFIG = {
  engine = "UE3", -- UE2.5 / UE3 / UE4 / idTech6 / idTech7 / Northlight
  restoreHoldMs = 250,
  cameraIntervalMs = 16,

  -- Optional aliases. Exact description match, case-insensitive.
  aliases = {
    x     = {"X", "Camera X", "Position X", "Pos X"},
    y     = {"Y", "Camera Y", "Position Y", "Pos Y"},
    z     = {"Z", "Camera Z", "Position Z", "Pos Z"},
    pitch = {"Pitch", "Camera Pitch"},
    yaw   = {"Yaw", "Camera Yaw"},
    roll  = {"Roll", "Camera Roll"},
    fov   = {"FOV", "Fov", "Field of View", "Camera FOV"}
  }
}

local CAMERA_PIPE_NAME  = "IGCSDOF_ProviderToBridge_v1"
local COMMAND_PIPE_NAME = "IGCSDOF_BridgeToProvider_v1"
local RECONNECT_DELAY_MS = 1000

------------------------------------------------------------
-- GENERATION / STATE
------------------------------------------------------------
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
  savedCamera = nil,
  records = nil,
  lastResolveLog = 0
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

local function trim(s)
  if s == nil then return "" end
  s = tostring(s)
  s = s:gsub('^%s+', ''):gsub('%s+$', '')
  s = s:gsub('^"', ''):gsub('"$', '')
  return s
end

local function lower(s)
  return string.lower(trim(s))
end

------------------------------------------------------------
-- MEMORY RECORD DISCOVERY
------------------------------------------------------------
local function descriptionMatches(desc, aliases)
  local d = lower(desc)
  for _, alias in ipairs(aliases or {}) do
    if d == lower(alias) then return true end
  end
  return false
end

local function getParentDescription(mr)
  local ok, p = pcall(function() return mr.Parent end)
  if ok and p then
    local ok2, d = pcall(function() return p.Description end)
    if ok2 and d then return trim(d) end
  end
  return ""
end

local function getAllRecords()
  local list = getAddressList()
  local out = {}
  if not list then return out end

  local count = 0
  pcall(function() count = list.Count end)

  for i = 0, count - 1 do
    local mr = nil

    local ok, value = pcall(function()
      if list.getMemoryRecord then return list.getMemoryRecord(i) end
      return nil
    end)
    if ok then mr = value end

    if not mr then
      pcall(function() mr = list[i] end)
    end

    if mr then out[#out + 1] = mr end
  end

  return out
end

local function findCandidates(key, aliases)
  local result = {}
  for _, mr in ipairs(getAllRecords()) do
    local ok, desc = pcall(function() return mr.Description end)
    if ok and descriptionMatches(desc, aliases) then
      result[#result + 1] = mr
    end
  end
  return result
end

local function recordScore(mr)
  local score = 0
  local parent = lower(getParentDescription(mr))
  if parent:find("camera", 1, true) then score = score + 100 end
  if parent:find("freecam", 1, true) then score = score + 100 end

  local ok, active = pcall(function() return mr.Active end)
  if ok and active then score = score + 10 end

  local ok2, addr = pcall(function() return mr.Address end)
  if ok2 and addr and trim(addr) ~= "" then score = score + 1 end

  return score
end

local function chooseCandidate(key, aliases)
  local candidates = findCandidates(key, aliases)
  if #candidates == 0 then return nil end

  table.sort(candidates, function(a, b)
    return recordScore(a) > recordScore(b)
  end)

  return candidates[1]
end

local function detectCameraRecords()
  local r = {}
  local missing = {}

  for _, key in ipairs({"x","y","z","pitch","yaw","roll","fov"}) do
    r[key] = chooseCandidate(key, CONFIG.aliases[key])
    if not r[key] then missing[#missing + 1] = key end
  end

  if #missing > 0 then
    print("[IGCSDOF AUTO] Missing records: " .. table.concat(missing, ", "))
    return nil
  end

  return r
end

------------------------------------------------------------
-- RESOLVED ADDRESS
--
-- CurrentAddress is CE's own final address after resolving
-- Address + pointer chain + offsets. This is the key: we do
-- NOT rebuild the user's pointer chain ourselves.
------------------------------------------------------------
local function resolvedAddress(mr)
  if not mr then return nil end

  local addr = nil

  local ok, value = pcall(function() return mr.CurrentAddress end)
  if ok and type(value) == "number" and value ~= 0 then
    addr = value
  end

  if not addr then
    local ok2, value2 = pcall(function()
      if mr.getCurrentAddress then return mr.getCurrentAddress() end
      return nil
    end)
    if ok2 and type(value2) == "number" and value2 ~= 0 then
      addr = value2
    end
  end

  if not addr then
    local offsetCount = 0
    pcall(function() offsetCount = mr.OffsetCount or 0 end)

    if offsetCount == 0 then
      local expression = nil
      pcall(function() expression = mr.Address end)
      if expression and expression ~= "" then
        local ok3, value3 = pcall(getAddressSafe, expression)
        if ok3 and type(value3) == "number" and value3 ~= 0 then
          addr = value3
        end
      end
    end
  end

  return addr
end

local function getOffsetList(mr)
  local offsets = {}
  local count = 0
  pcall(function() count = mr.OffsetCount or 0 end)

  for i = 0, count - 1 do
    local v = nil
    pcall(function() v = mr.Offset[i] end)
    if type(v) == "number" then
      offsets[#offsets + 1] = string.format("%X", v)
    elseif v ~= nil then
      offsets[#offsets + 1] = tostring(v)
    end
  end

  return offsets
end

local function describeRecord(name, mr)
  local desc, expression, vtype = "?", "?", "?"
  pcall(function() desc = trim(mr.Description) end)
  pcall(function() expression = tostring(mr.Address) end)
  pcall(function() vtype = tostring(mr.Type) end)

  local offsets = getOffsetList(mr)
  local resolved = resolvedAddress(mr)

  local accessType
  if #offsets > 0 then
    accessType = "pointer-chain"
  elseif expression:find("%[") then
    accessType = "dereferenced-expression"
  else
    accessType = "direct/symbol"
  end

  print(string.format(
    "[IGCSDOF AUTO] %-5s | %-21s | %-18s | resolved=%s | Address=%s | Offsets=%s | Parent=%s",
    string.upper(name),
    desc,
    accessType,
    resolved and string.format("%X", resolved) or "INVALID",
    expression,
    (#offsets > 0) and table.concat(offsets, " -> ") or "none",
    getParentDescription(mr)
  ))
end

local function validateRecords(records, verbose)
  if not records then return false end

  local valid = true
  for _, key in ipairs({"x","y","z","pitch","yaw","roll","fov"}) do
    local mr = records[key]
    local addr = resolvedAddress(mr)
    if not addr then valid = false end
    if verbose then describeRecord(key, mr) end
  end

  return valid
end

------------------------------------------------------------
-- VARIABLE TYPE HELPERS
------------------------------------------------------------
local function recordType(mr)
  local t = nil
  pcall(function() t = mr.Type end)
  return t
end

local function readRecordValue(mr, rotation)
  local addr = resolvedAddress(mr)
  if not addr then return nil end

  local t = recordType(mr)

  if vtSingle ~= nil and t == vtSingle then
    return readFloat(addr)
  end

  if vtDouble ~= nil and t == vtDouble then
    return readDouble(addr)
  end

  if not rotation then
    return readFloat(addr)
  end

  if vtDword ~= nil and t == vtDword then
    return readInteger(addr)
  end

  if CONFIG.engine == "UE2.5" or CONFIG.engine == "UE3" then
    return readInteger(addr)
  end

  return readFloat(addr)
end

local function writeRecordValue(mr, value, rotation)
  local addr = resolvedAddress(mr)
  if not addr then return false end

  local t = recordType(mr)

  if vtSingle ~= nil and t == vtSingle then
    writeFloat(addr, value)
    return true
  end

  if vtDouble ~= nil and t == vtDouble then
    writeDouble(addr, value)
    return true
  end

  if not rotation then
    writeFloat(addr, value)
    return true
  end

  if vtDword ~= nil and t == vtDword then
    writeInteger(addr, math.floor(value + (value >= 0 and 0.5 or -0.5)))
    return true
  end

  if CONFIG.engine == "UE2.5" or CONFIG.engine == "UE3" then
    writeInteger(addr, math.floor(value + (value >= 0 and 0.5 or -0.5)))
  else
    writeFloat(addr, value)
  end

  return true
end

local function rotationIsInteger()
  local mr = ctx.records and ctx.records.pitch
  if mr then
    local t = recordType(mr)
    if vtDword ~= nil and t == vtDword then return true end
    if vtSingle ~= nil and t == vtSingle then return false end
  end
  return CONFIG.engine == "UE2.5" or CONFIG.engine == "UE3"
end

------------------------------------------------------------
-- CAMERA READ / WRITE
------------------------------------------------------------
local function readCamera()
  if not ctx.records then
    ctx.records = detectCameraRecords()
    if not ctx.records then return nil end
  end

  if not validateRecords(ctx.records, false) then
    return nil
  end

  local c = {
    x     = readRecordValue(ctx.records.x, false),
    y     = readRecordValue(ctx.records.y, false),
    z     = readRecordValue(ctx.records.z, false),
    pitch = readRecordValue(ctx.records.pitch, true),
    yaw   = readRecordValue(ctx.records.yaw, true),
    roll  = readRecordValue(ctx.records.roll, true),
    fov   = readRecordValue(ctx.records.fov, false)
  }

  if c.x == nil or c.y == nil or c.z == nil or
     c.pitch == nil or c.yaw == nil or c.roll == nil or c.fov == nil then
    return nil
  end

  return c
end

local function cloneCamera(c)
  if not c then return nil end
  return {
    x=c.x, y=c.y, z=c.z,
    pitch=c.pitch, yaw=c.yaw, roll=c.roll,
    fov=c.fov
  }
end

local function writeCamera(c)
  if not c or not ctx.records then return false end

  local ok = true
  ok = writeRecordValue(ctx.records.x, c.x, false) and ok
  ok = writeRecordValue(ctx.records.y, c.y, false) and ok
  ok = writeRecordValue(ctx.records.z, c.z, false) and ok
  ok = writeRecordValue(ctx.records.pitch, c.pitch, true) and ok
  ok = writeRecordValue(ctx.records.yaw, c.yaw, true) and ok
  ok = writeRecordValue(ctx.records.roll, c.roll, true) and ok
  ok = writeRecordValue(ctx.records.fov, c.fov, false) and ok
  return ok
end

------------------------------------------------------------
-- CAMERA_RAW MESSAGE
------------------------------------------------------------
local function makeCameraLine()
  local camera = readCamera()
  if not camera then return "CAMERA_INVALID" end

  local locked = ctx.sessionActive
  local rotationFormat = rotationIsInteger() and "%d|%d|%d" or "%.9f|%.9f|%.9f"
  local format = "CAMERA_RAW|1|1|%d|%.9f|%.9f|%.9f|" .. rotationFormat .. "|%.9f"

  return string.format(
    format,
    locked and 1 or 0,
    camera.x, camera.y, camera.z,
    camera.pitch, camera.yaw, camera.roll,
    camera.fov
  )
end

------------------------------------------------------------
-- SESSION
------------------------------------------------------------
local function startSession()
  if ctx.sessionActive then return end
  local camera = readCamera()
  if not camera then return end

  ctx.savedCamera = cloneCamera(camera)
  ctx.sessionActive = true
  ctx.latestCameraLine = makeCameraLine()
  ctx.cameraSequence = ctx.cameraSequence + 1
end

local function endSession()
  if ctx.savedCamera then writeCamera(ctx.savedCamera) end
  ctx.savedCamera = nil
  ctx.sessionActive = false
end

------------------------------------------------------------
-- COMMANDS FROM BRIDGE
------------------------------------------------------------
local function setPositionRaw(x, y, z, fov)
  if not ctx.records then return end
  writeRecordValue(ctx.records.x, x, false)
  writeRecordValue(ctx.records.y, y, false)
  writeRecordValue(ctx.records.z, z, false)
end

local function rotateYawRaw(delta)
  if not ctx.records then return end
  local yaw = readRecordValue(ctx.records.yaw, true)
  if yaw == nil then return end
  writeRecordValue(ctx.records.yaw, yaw + delta, true)
end

local function handleCommand(line)
  if not isCurrent() then return end
  local parts = splitLine(line)
  local command = parts[1]

  if command == "SESSION_BEGIN" then
    IGCSDOF_SESSION_ACTIVE = true
    startSession()

  elseif command == "SESSION_END" then
    endSession()
    IGCSDOF_SESSION_ACTIVE = false

  elseif command == "SET_POSITION_RAW" then
    setPositionRaw(
      tonumber(parts[2]) or 0,
      tonumber(parts[3]) or 0,
      tonumber(parts[4]) or 0,
      tonumber(parts[5]) or 0
    )

  elseif command == "ROTATE_YAW_RAW" then
    rotateYawRaw(tonumber(parts[2]) or 0)
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
      local ok, candidate = pcall(function()
        return connectToPipe(CAMERA_PIPE_NAME, 0)
      end)

      if not isCurrent() then
        if ok and candidate then safeDestroy(candidate) end
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
      local sequence = ctx.cameraSequence
      local line = ctx.latestCameraLine

      if line and sequence ~= sentSequence then
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
      local ok, candidate = pcall(function()
        return connectToPipe(COMMAND_PIPE_NAME, 0)
      end)

      if not isCurrent() then
        if ok and candidate then safeDestroy(candidate) end
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

------------------------------------------------------------
-- START / DIAGNOSTIC
------------------------------------------------------------
ctx.records = detectCameraRecords()

if not ctx.records then
  error("[IGCSDOF AUTO] Camera records not found. Need X/Y/Z/Pitch/Yaw/Roll/FOV records.")
end

print("------------------------------------------------------------")
print("[IGCSDOF AUTO] Camera Memory Records detected")
validateRecords(ctx.records, true)
print("[IGCSDOF AUTO] Rotation storage: " .. (rotationIsInteger() and "INTEGER" or "FLOAT"))
print("[IGCSDOF AUTO] Relay engine: " .. CONFIG.engine)
print("------------------------------------------------------------")

if IGCSDOF_PROVIDER_CAMERA_TIMER then
  IGCSDOF_PROVIDER_CAMERA_TIMER.destroy()
end

IGCSDOF_PROVIDER_CAMERA_TIMER = createTimer(nil, false)
IGCSDOF_PROVIDER_CAMERA_TIMER.Interval = CONFIG.cameraIntervalMs or 16
IGCSDOF_PROVIDER_CAMERA_TIMER.OnTimer = function()
  if not isCurrent() then return end
  ctx.latestCameraLine = makeCameraLine()
  ctx.cameraSequence = ctx.cameraSequence + 1
end
IGCSDOF_PROVIDER_CAMERA_TIMER.Enabled = true

ctx.latestCameraLine = makeCameraLine()
ctx.cameraSequence = 1
ctx.senderThread = createThread(cameraSenderWorker)
ctx.receiverThread = createThread(commandReceiverWorker)

ctx.cleanup = function()
  if ctx.savedCamera then writeCamera(ctx.savedCamera) end
  safeDestroy(ctx.cameraPipe)
  safeDestroy(ctx.commandPipe)
end

print("[IGCSDOF] AUTO RAW provider started | Engine=" .. CONFIG.engine)

{$asm}
[DISABLE]
{$lua}
if syntaxcheck then return end

IGCSDOF_PROVIDER_STOP = function()
  IGCSDOF_PROVIDER_GENERATION = (IGCSDOF_PROVIDER_GENERATION or 0) + 1

  if IGCSDOF_PROVIDER_CAMERA_TIMER then
    IGCSDOF_PROVIDER_CAMERA_TIMER.destroy()
    IGCSDOF_PROVIDER_CAMERA_TIMER = nil
  end

  local contexts = IGCSDOF_PROVIDER_CONTEXTS
  if contexts then
    for _, old in pairs(contexts) do
      if old then
        old.active = false
        if old.cleanup then pcall(old.cleanup) end
      end
    end
  end

  IGCSDOF_SESSION_ACTIVE = false
  print("[IGCSDOF] AUTO RAW provider stopped.")
end

IGCSDOF_PROVIDER_STOP()
{$asm}

-- Replays a script against the retail camera.lua, unmodified, with the real profile.lua, LuaJIT's real
-- ffi (so the cdefs and struct sizes are the real ones) and a scripted helper, backend, controller and clock.
-- Usage: luajit run_camera_adapter.lua <retail kit folder> <script file>
-- Interpreted only: LuaJIT's compiled traces convert a uint64 of 2^63 or more to a number with two
-- roundings (signed conversion, then + 2^64) and so can be one unit in the last place off the
-- interpreter's single rounding. The interpreter's answer is the reference.
if jit then jit.off() end
local ffi = require('ffi')
local kit, script = arg[1], dofile(arg[2])
local R = script.reasons
local world, trace = nil, {}
local calls = {}
local function note(text) trace[#trace + 1] = text end
local function reason(index) if index and index > 0 then return R[index] end end
-- The nth answer of a list during one operation; the last one repeats.
local function nth(name, list)
  calls[name] = (calls[name] or 0) + 1
  return list[math.min(calls[name], #list)]
end
local function hex(pointer, size)
  return (ffi.string(pointer, size):gsub('.', function(c) return string.format('%02x', c:byte()) end))
end
local function float_bits(value) return ffi.cast('uint32_t*', ffi.new('float[1]', value))[0] end
local function generation_text(value) return string.format('%.0f', tonumber(value)) end

-- The scripted helper. `live` is the generation it last handed out and has not ended. With `follow`
-- it answers a generation as the real helper does: only the live one is served, generation 0 reads
-- as "nothing installed" while none is live, a second Begin is refused.
local live, serial = 0, 0
local library = {}
function library.NADSC_Validate(c) note('V(' .. hex(c, 88) .. ')'); return world.validate end
function library.NADSC_Begin(c, out)
  note('B(' .. hex(c, 88) .. ')')
  local result, generation = world.begin, world.gen
  if world.follow then
    if live ~= 0 then result, generation = 0, 0
    elseif result == 1 or result == 2 then serial = serial + 1; generation = serial
    else generation = 0 end
  end
  if generation ~= 0 then out[0] = generation; live = generation end
  return result
end
function library.NADSC_End(generation)
  generation = tonumber(generation)
  note('E(' .. generation_text(generation) .. ')')
  local result = nth('end', world.ends)
  if world.follow and generation ~= live then result = 0 end
  if result == 1 then live = 0 end
  return result
end
function library.NADSC_ReadStats(generation, s)
  generation = tonumber(generation)
  note(string.format('S(%s,%d,%d)', generation_text(generation), tonumber(s.size), tonumber(s.abi)))
  local answer = script.stats[nth('stats', world.st)]
  local result, idle = answer.ret, false
  if world.follow then
    if generation == 0 and live == 0 then result, idle = 1, true
    elseif generation == 0 or generation ~= live then return 0 end
  end
  if idle then s.size, s.installed, s.armed, s.error = 80, 0, 0, 0
  elseif answer.fill then s.size, s.abi, s.installed, s.armed, s.error = answer.size, answer.abi, answer.installed, answer.armed, answer.error end
  if answer.fill or idle then
    s.seen, s.forwarded, s.replaced, s.native6_forwarded, s.resets = answer.seen, answer.forwarded, answer.replaced, answer.native6, answer.resets
    s.blend, s.head_distance, s.last_dt = answer.blend, answer.head, answer.dt
  end
  return result
end
function library.NADSC_Error() note('Err'); return nth('error', world.errs) end
local function setter(name, answer, format)
  return function(generation, ...)
    generation = tonumber(generation)
    note(name .. '(' .. generation_text(generation) .. format(...) .. ')')
    if world.follow and generation ~= live then return 0 end
    return world[answer]
  end
end
library.NADSC_SetPresentation47 = setter('P', 'pres', function() return '' end)
library.NADSC_SetHandoff49 = setter('H', 'hand', function(entry, rotation)
  return string.format(',%08x,%08x', float_bits(entry), float_bits(rotation)) end)
library.NADSC_SetCutHold50 = setter('C', 'cut', function(mode) return string.format(',%.0f', tonumber(ffi.new('uint32_t', mode))) end)
library.NADSC_SetWeaponKey50 = setter('K', 'key', function(key) return string.format(',%.0f', tonumber(ffi.new('uint32_t', key))) end)

-- compat51.lua's part used here: its ffi (the real one, reporting the game's platform, loading the
-- scripted helper), kind (= type for these values) and number (= tonumber without a base).
local compat = {kind = type, number = function(value) return tonumber(value) end,
  ffi = setmetatable({os = 'Windows', load = function()
    note('load')
    if world.load > 0 then error(reason(world.load), 0) end
    return library
  end}, {__index = ffi})}
local profile
mod = {dir = 'MOD', require = function(path)
  if path == 'src/first_person/compat51.lua' then return compat end
  assert(path == 'src/first_person/profile.lua', path)
  if not profile then
    profile = dofile(kit .. '/' .. path)
    for key, value in pairs(script.profile or {}) do
      if value == 'none' then profile.camera[key] = nil else profile.camera[key] = value end
    end
  end
  return profile
end}
local Camera = dofile(kit .. '/src/first_person/camera.lua')

local backend = {}
function backend:camera_config()
  note('config')
  local c = script.configs[nth('config', world.cfg)]
  if c.ok == 2 then error(reason(c.why), 0) end
  if c.ok == 0 then return nil, reason(c.why) end
  local out = {camera_kind = c.kind > 0 and c.kind or nil, presentation_enabled = c.pres == 1, pin_view = false, pin_look = false,
               kick_enabled = false}
  for index, name in ipairs({'game', 'exe', 'context', 'camera', 'avatar_owner', 'avatar_index', 'avatar', 'body', 'camera_unit',
                             'forward', 'up', 'smoothing_ms', 'blend_ms', 'max_gap_ms'}) do
    if math.floor(c.present / 2 ^ (index - 1)) % 2 == 1 then out[name] = c[name] end
  end
  return out
end
if script.snapshot then
  function backend:snapshot()
    note('snapshot')
    if world.snap == 3 then error('snapshot raised', 0) end
    if world.snap == 0 then return nil, 'no snapshot' end
    if world.snap == 4 then return {weapon_unit = 'unit'} end
    return {weapon_unit = world.snap == 2 and world.unit or nil}
  end
end
local controller = {}
function controller:result() note('result'); return {mode = ({'pose_offset', 'native'})[world.mode]} end
function controller:is_enabled() note('enabled'); return world.enabled end
local function clock()
  note('clock')
  local value = nth('clock', world.clock)
  if value == 'raise' then error('clock raised', 0) end
  return value
end
-- A raised assert carries "<chunk name>:<line>: " in front of its message. The chunk name is whatever
-- the loader called the file (here: the path given to dofile), so it is taken off; the only assert a
-- script can reach is the missing configuration field at line 95, and anything else stops the run.
local function plain(message)
  local line, rest = message:match('^[^\n]-camera%.lua:(%d+): (.*)$')
  if not line then return message end
  assert(line == '95' and rest:match('^[%a_]+ unavailable$'), 'an unexpected error was raised: ' .. message)
  return rest
end
local log = script.log and function(text)
  assert(text:sub(1, 20) == 'Native head camera: ', text)
  note('log(' .. text:sub(1, 20) .. plain(text:sub(21)) .. ')')
end or nil
local camera = Camera.new(backend, log, nil, script.clock and clock or nil)
local helper
for index = 1, 60 do
  local name, value = debug.getupvalue(camera.stop, index)
  if name == 'helper' then helper = value end
  if not name then break end
end
assert(helper and getmetatable(helper) == Camera.Adapter, 'the adapter was not found')

local PHASE = {idle = 0, waiting = 1, active = 2, cleanup_pending = 3}
local KIND = {temporary_read = 1, unsafe = 2}
local function flag(value) return value and 1 or 0 end
local function number(value) return string.format('%.17g', value) end
local function text(value)
  assert(value == nil or type(value) == 'string', 'a text was expected')
  return flag(value ~= nil), value and plain(value) or ''
end
for _, op in ipairs(script.ops) do
  world, trace, calls = op, {}, {}
  local first, second
  if op.call == 'update' then first, second = camera:update(controller)
  elseif op.call == 'prepare' then first, second = camera:prepare()
  elseif op.call == 'prearm' then first, second = camera:prearm()
  elseif op.call == 'stop' then first, second = camera:stop()
  elseif op.call == 'shutdown' then first, second = camera:shutdown()
  elseif op.call == 'preflight' then first, second = camera:preflight()
  elseif op.call == 'set_handoff' then first, second = camera:set_handoff(op.a, op.b)
  elseif op.call == 'set_cut_hold' then
    first, second = camera:set_cut_hold(op.hold % 2 >= 1, op.hold % 4 >= 2, op.hold % 8 >= 4, op.hold % 16 >= 8)
  else error('unknown call ' .. tostring(op.call)) end
  assert(first == nil or type(first) == 'boolean', 'a boolean or nil was expected')
  -- What the C unit relies on: the manager's handle is the adapter's owned handle, never a retired
  -- one, and a statistics cache belongs to it.
  assert(camera.handle == (helper.owned or nil), 'the manager and the adapter disagree about the handle')
  assert(not camera.handle or (camera.handle.disarmed == false and camera.handle.owner == helper), 'a retired handle is held')
  assert(not helper.stats_cache or (helper.stats_handle == helper.owned and helper.stats_handle), 'statistics cache of another handle')
  assert((helper.stats_cache ~= nil) == (helper.stats_due ~= nil), 'statistics cache without a due time')
  assert(helper.library == nil or helper.library == library)
  assert(camera.bound == nil or camera.handle ~= nil, 'an identity is bound without a handle')
  assert(not camera.handle or camera.phase == 'cleanup_pending' or (camera.phase == 'active' and camera.bound), 'a handle outside active or cleanup')
  local r, out = camera:result(), {}
  local function add(...) for _, value in ipairs({...}) do out[#out + 1] = tostring(value) end end
  add(first == nil and -1 or flag(first)); add(text(second))
  add(flag(camera:is_clean()), PHASE[r.phase], flag(camera.closing))
  assert(r.cleanup_pending == (r.phase == 'cleanup_pending'))
  local has_stats = r.error ~= nil
  add(flag(has_stats))
  for _, name in ipairs({'seen', 'forwarded', 'replaced', 'native6_forwarded', 'resets', 'blend', 'head_distance', 'last_dt'}) do
    assert((r[name] ~= nil) == has_stats); add(number(r[name] or 0))
  end
  add(flag(r.installed), flag(r.armed), number(r.error or 0))
  assert(r.error_message == camera.fault); add(text(r.error_message))
  add(flag(r.failure_native_error ~= nil), number(r.failure_native_error or 0), KIND[r.failure_kind] or 0)
  add(camera:status())
  add(flag(camera.handle ~= nil), camera.handle and generation_text(camera.handle.generation[0]) or 0)
  add(flag(camera.weapon_key ~= nil), number(camera.weapon_key or 0))
  add(flag(camera.handoff ~= nil), number(camera.handoff and camera.handoff.entry_ms or 0), number(camera.handoff and camera.handoff.rotation_ms or 0))
  add(flag(camera.cut_hold ~= nil), camera.cut_hold or 0)
  add(text(camera.handoff_issue)); add(text(camera.cut_hold_issue))
  if camera.bound then
    local identity = {}
    for _, name in ipairs({'game', 'exe', 'context', 'camera', 'avatar_owner', 'avatar_index', 'avatar', 'body', 'camera_unit'}) do
      identity[#identity + 1] = string.format('%.0f', camera.bound[name])
    end
    add(table.concat(identity, ','))
  else add('-') end
  add(flag(helper.library ~= nil), flag(helper.stats_cache ~= nil), flag(helper.stats_at ~= nil), number(helper.stats_at or 0),
      number(helper.stats_due or 0))
  add(flag(helper.last_failure_code ~= nil), number(helper.last_failure_code or 0))
  add(table.concat(trace, ' '))
  print(table.concat(out, '\t'))
end

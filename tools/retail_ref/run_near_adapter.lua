-- Replays scripts against the retail near_clip.lua, unmodified, under LuaJIT.
-- Usage: luajit run_near_adapter.lua <Goose Gadget Kit folder> <script file>
-- The module gets LuaJIT's real ffi (its cdefs and struct sizes are real); ffi.load returns a
-- table of real C function pointers (ffi callbacks) whose bodies answer what the script says, so
-- argument and result conversions are LuaJIT's own. The script is one Lua table per line: the
-- reasons, then for each run its options followed by its operations. One line per operation:
-- what the call returned, the lease's and adapter's state, and the ordered trace of every call.
local ffi = require('ffi')
local bit = require('bit')
jit.off() -- callbacks must not be reached from compiled code
local kit = arg[1]
local REASONS
local NAMES = {'game', 'exe', 'context', 'camera', 'avatar_owner', 'avatar_index'}
local w, trace, counts, sim, lib
local function t(text) trace[#trace + 1] = text end
local function reason(index) if index and index > 0 then return REASONS[index] end end
local function u64(x) return (tostring(ffi.cast('uint64_t', x)):gsub('U?LL$', '')) end
local function num(x)
  if x == nil then return '~' end
  if x == true then return 'T' elseif x == false then return 'F' end
  if type(x) ~= 'number' then return tostring(x) end
  if x ~= x then return 'nan' elseif x == math.huge then return 'inf' elseif x == -math.huge then return '-inf' end
  return string.format('%.17g', x)
end
local function pick(list, key) counts[key] = (counts[key] or 0) + 1; return list[math.min(counts[key], #list)] end
local function cfgtext(c)
  return table.concat({c.size, c.abi, u64(c.game), u64(c.exe), u64(c.context), u64(c.camera), u64(c.avatar_owner), c.avatar_index,
    num(c.hip), num(c.aim), c.reserved, u64(c.reserved2)}, ' ')
end
-- The helper. A scripted answer below zero asks the small model of the real helper instead.
local function build_library()
  local keep = {}
  local function cb(signature, body) local f = ffi.cast(signature, body); keep[#keep + 1] = f; return f end
  return {
    _keep = keep,
    NADSN_Begin = cb('uint32_t(*)(const NADSN_Config *,uint64_t *)', function(c, out)
      t('B(' .. cfgtext(c) .. ')')
      local r, g = w.begin, w.begin_gen
      if r < 0 then
        if sim.gen ~= 0 then r, g, sim.lasterr = 0, 0, 2
        else
          sim.next = sim.next + 1
          sim.gen, sim.armed, sim.error, sim.hip, sim.aim = sim.next, true, 0, tonumber(c.hip), tonumber(c.aim)
          r, g = 1, sim.gen
        end
      elseif g ~= 0 then sim.gen, sim.armed, sim.error = g, r == 1, 0 end
      out[0] = g
      return r
    end),
    NADSN_Set = cb('uint32_t(*)(uint64_t,float,float)', function(gen, hip, aim)
      t('s(' .. u64(gen) .. ' ' .. num(hip) .. ' ' .. num(aim) .. ')')
      local r = w.setr
      if r < 0 then
        if tonumber(gen) == sim.gen and sim.armed then sim.hip, sim.aim, r = hip, aim, 1 else r, sim.lasterr = 0, 17 end
      end
      return r
    end),
    NADSN_End = cb('uint32_t(*)(uint64_t)', function(gen)
      t('E(' .. u64(gen) .. ')')
      local r = pick(w.endr, 'end')
      if r < 0 then
        if tonumber(gen) == sim.gen and sim.gen ~= 0 then sim.gen, sim.armed, r = 0, false, 1 else r, sim.lasterr = 0, 17 end
      elseif r == 1 then sim.gen, sim.armed = 0, false end
      return r
    end),
    NADSN_ReadStats = cb('uint32_t(*)(uint64_t,NADSN_Stats *)', function(gen, s)
      t('S(' .. u64(gen) .. ' ' .. s.size .. ' ' .. s.abi .. ')')
      local a = pick(w.st, 'stats')
      if a.ret < 0 then
        gen = tonumber(gen)
        if (gen == sim.gen and gen ~= 0) or (gen == 0 and sim.gen == 0) then
          sim.seen = sim.seen + 3
          s.size, s.abi, s.installed, s.armed, s.error = 88, 1, sim.gen ~= 0 and 1 or 0, sim.armed and 1 or 0, sim.error
          s.seen, s.forwarded, s.replaced, s.ads_forwarded, s.aim_applied, s.other_kind =
            sim.seen, sim.seen + 1, sim.seen * 2, sim.seen + 3, sim.seen + 4, sim.seen * 3
          s.last_native, s.last_applied, s.hip, s.aim = 0.05, sim.seen * 0.001, sim.hip, sim.aim
          return 1
        end
        sim.lasterr = 17
        return 0
      end
      if a.fill then
        local b = ffi.cast('uint64_t', w.sbase)
        s.size, s.abi, s.installed, s.armed, s.error = w.ssize, w.sabi, a.inst, a.armed, a.err
        s.seen, s.forwarded, s.replaced, s.ads_forwarded, s.aim_applied, s.other_kind = b, b + 1, b * 2, b + 3, b + 4, b * 3
        s.last_native, s.last_applied, s.hip, s.aim = w.f1, w.f2, w.f3, w.f4
      end
      return a.ret
    end),
    NADSN_Error = cb('uint32_t(*)(void)', function()
      t('e')
      local r = pick(w.err, 'error')
      if r < 0 then r = sim.lasterr end
      return r
    end),
  }
end
local compat = {kind = type, number = tonumber, -- compat51 maps cdata to 'cdata' and converts it; LuaJIT's own do the same
  ffi = setmetatable({os = 'Windows', load = function() lib = build_library(); return lib end}, {__index = ffi})}
local profile
mod = {dir = 'KIT', require = function(path)
  if path == 'src/first_person/compat51.lua' then return compat end
  if path == 'src/first_person/profile.lua' then return profile end
  error('unexpected require ' .. path)
end}
-- The file's bytes as they are, under the name the mod requires it by: assert() puts
-- "<chunk>:<line>: " before its message, and those texts reach the player.
local function retail(path)
  local file = assert(io.open(kit .. '/' .. path, 'rb'))
  local source = file:read('*a')
  file:close()
  return assert(loadstring(source, '@' .. path))()
end
profile = retail('src/first_person/profile.lua')
local Near = retail('src/first_person/near_clip.lua')

local function statstext(s)
  if type(s) ~= 'table' then return '-' end
  return table.concat({num(s.seen), num(s.forwarded), num(s.replaced), num(s.ads_forwarded), num(s.aim_applied), num(s.other_kind),
    num(s.last_native), num(s.last_applied), num(s.hip), num(s.aim), num(s.installed), num(s.armed), num(s.error)}, ',')
end
local function upvalue(f, wanted)
  for i = 1, 50 do
    local name, value = debug.getupvalue(f, i)
    if name == wanted then return value end
    if not name then return nil end
  end
end
local function protected(f, ...) -- as the module's own invoke(): a raised error is a refusal with its text
  local called, a, b = pcall(f, ...)
  if not called then return nil, tostring(a) end
  return a, b
end

local function start(run, index)
  mod.scope = run.scope and {} or nil
  sim = {gen = 0, next = 100 * index, armed = false, error = 0, seen = 0, lasterr = 0, hip = 0, aim = 0}
  local backend, controller = {}, {}
  function backend:fov_config(lease)
    t('cfg')
    assert(lease == controller.lease)
    if w.cfg_throw then error(reason(w.cfg_why), 0) end
    if not w.cfg_ok then return nil, reason(w.cfg_why) end
    local c = {request = 5, avatar = 11, body = 12, weapon = 7} -- the backend's table also carries what this module does not read
    for i, name in ipairs(NAMES) do if bit.band(w.cfg_missing, bit.lshift(1, i - 1)) == 0 then c[name] = w.cfg[i] end end
    return c
  end
  function controller:result() t('res'); return {mode = w.mode and 'pose_offset' or 'other', phase = w.running and 'running' or 'waiting'} end
  function controller:is_enabled() t('en'); return w.en end
  local function clock()
    t('clk')
    if w.clk == 1 then error('Engine FPV clock sample invalid', 0) end
    return w.now
  end
  local function options()
    t('opt')
    if not w.opt_table then return nil end
    return {hip = w.hip_num and w.hip or 'x', aim = w.aim_num and w.aim or nil}
  end
  local function log(text) t('L[' .. text .. ']') end
  -- pull=false: the same adapter the module builds for itself, on a copy of the retail profile with the switch off.
  local given
  if not run.pull then
    local copy = {}
    for k, v in pairs(profile.near) do copy[k] = v end
    copy.pull_sight_distance = false
    given = Near.Adapter.new(copy)
  end
  local near = Near.new(backend, not run.nolog and log or nil, given, clock, options)
  local helper = given or assert(upvalue(near.update, 'helper'))
  local other = Near.Adapter.new(profile.near)
  local foreign = {owner = other, disarmed = false, generation = ffi.new('uint64_t[1]', 5)}
  local stale = {owner = helper, disarmed = false, generation = ffi.new('uint64_t[1]', 6)} -- ours by name, never begun
  local slots = {}
  return function(op)
    w, trace, counts = op, {}, {}
    if op.sim_disarm >= 0 then sim.armed, sim.error = false, op.sim_disarm end
    controller.lease = op.lease > 0 and {origin = op.lo and {player_identity = op.lp > 0 and ('p' .. op.lp) or nil} or nil} or nil
    local handle
    if op.slot == -1 then handle = near.handle elseif op.slot == -3 then handle = foreign elseif op.slot == -4 then handle = stale
    elseif op.slot >= 0 then handle = slots[op.slot] end
    local a, b
    if op.call == 'update' then
      local called
      called, a, b = pcall(near.update, near, controller)
      if not called then a, b = 'R', a end
    elseif op.call == 'stop' then a, b = near:stop()
    elseif op.call == 'shutdown' then a, b = near:shutdown()
    elseif op.call == 'a_begin' then
      local config
      if op.cfg_ok then
        config = {hip = op.hip, aim = op.aim}
        for i, name in ipairs(NAMES) do if bit.band(op.cfg_missing, bit.lshift(1, i - 1)) == 0 then config[name] = op.cfg[i] end end
      end
      a, b = protected(helper.begin, helper, config)
      if a == true then slots[op.slot], b = b, nil else slots[op.slot] = helper:owned_handle() or nil end
      a = a == true
    elseif op.call == 'a_set' then a, b = protected(helper.set, helper, handle, op.hip, op.aim); a = a == true
    elseif op.call == 'a_stats' then
      a, b = protected(helper.stats, helper, handle)
      if type(a) == 'table' then a, b = true, a else a = false end
    elseif op.call == 'a_finish' then a, b = protected(helper.finish, helper, handle); a = a == true
    else error('unknown call ' .. tostring(op.call)) end
    local r = near:result()
    local second = type(b) == 'table' and statstext(b) or (b == nil and '~' or tostring(b))
    print(table.concat({num(a), second,
      near.phase, near.handle and (near.handle.disarmed and 2 or 1) or 0, num(near.bound_player), num(near.hip), num(near.aim),
      num(near.closing), num(near.fault), num(near.retry_at), num(near.next_collect), num(near.last_collect), statstext(near.last),
      helper.owned and 1 or 0, num(near:is_clean()),
      table.concat({r.phase, num(r.cleanup_pending), num(r.error_message), statstext(r)}, ';'),
      table.concat(trace, ' '), near:status()}, '|'))
  end
end
local step, index = nil, 0
for line in io.lines(arg[2]) do
  local row = assert(loadstring('return ' .. line))()
  if row.reasons then REASONS = row.reasons
  elseif row.run then index = index + 1; step = start(row, index)
  else step(row) end
end

-- Replays scripts against the retail fov.lua, unmodified, under LuaJIT.
-- Usage: luajit run_fov_adapter.lua <Goose Gadget Kit folder> <script file>
-- The module gets LuaJIT's real ffi (its cdefs and struct sizes are real); ffi.load returns a
-- table of real C function pointers (ffi callbacks) whose bodies answer what the script says, so
-- argument and result conversions are LuaJIT's own. The script is one Lua table per line: the
-- reasons, then for each run its options followed by its operations. One line per operation: what the call
-- returned, the lease's and adapter's state, and the ordered trace of every call made.
local ffi = require('ffi')
local bit = require('bit')
jit.off() -- callbacks must not be reached from compiled code
local kit = arg[1]
local REASONS
local NAMES = {'game', 'exe', 'context', 'camera', 'avatar_owner', 'request', 'avatar_index', 'avatar', 'body', 'weapon'}
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
  return table.concat({c.size, c.abi, u64(c.game), u64(c.exe), u64(c.context), u64(c.camera), u64(c.avatar_owner), u64(c.request),
    c.avatar_index, c.avatar, c.body, c.weapon, u64(c.reserved)}, ' ')
end
-- The helper. A scripted answer below zero asks the small model of the real helper instead.
local function build_library()
  local keep = {}
  local function cb(signature, body) local f = ffi.cast(signature, body); keep[#keep + 1] = f; return f end
  return {
    _keep = keep,
    NADSF_Validate = cb('uint32_t(*)(const NADSF_Config *)', function(c)
      t('V(' .. cfgtext(c) .. ')')
      local r = w.validate; if r < 0 then r = 1 end
      return r
    end),
    NADSF_Begin = cb('uint32_t(*)(const NADSF_Config *,uint64_t *)', function(c, out)
      t('B(' .. cfgtext(c) .. ')')
      local r, g = w.begin, w.begin_gen
      if r < 0 then
        if sim.gen ~= 0 then r, g, sim.lasterr = 0, 0, 2
        else sim.next = sim.next + 1; sim.gen, sim.armed, sim.error = sim.next, true, 0; r, g = 1, sim.gen end
      elseif g ~= 0 then sim.gen, sim.armed, sim.error = g, r == 1, 0 end
      out[0] = g
      return r
    end),
    NADSF_Bind = cb('uint32_t(*)(uint64_t,uint64_t,uint32_t)', function(gen, request, weapon)
      t('b(' .. u64(gen) .. ' ' .. u64(request) .. ' ' .. weapon .. ')')
      local r = w.bind
      if r < 0 then
        if tonumber(gen) == sim.gen and sim.armed then r = 1 else r, sim.lasterr = 0, 17 end
      end
      return r
    end),
    NADSF_SetTransition = cb('uint32_t(*)(uint64_t,uint64_t,float,uint32_t)', function(gen, epoch, progress, ads)
      t('T(' .. u64(gen) .. ' ' .. u64(epoch) .. ' ' .. string.format('%.9g', progress) .. ' ' .. ads .. ')')
      local r = w.trans
      if r < 0 then
        if tonumber(gen) == sim.gen and sim.armed then r = 1 else r, sim.lasterr = 0, 17 end
      end
      return r
    end),
    NADSF_End = cb('uint32_t(*)(uint64_t)', function(gen)
      t('E(' .. u64(gen) .. ')')
      local r = pick(w.endr, 'end')
      if r < 0 then
        if tonumber(gen) == sim.gen and sim.gen ~= 0 then sim.gen, sim.armed, r = 0, false, 1 else r, sim.lasterr = 0, 17 end
      elseif r == 1 then sim.gen, sim.armed = 0, false end
      return r
    end),
    NADSF_ReadStats = cb('uint32_t(*)(uint64_t,NADSF_Stats *)', function(gen, s)
      t('S(' .. u64(gen) .. ' ' .. s.size .. ' ' .. s.abi .. ')')
      local a = pick(w.st, 'stats')
      if a.ret < 0 then
        gen = tonumber(gen)
        if (gen == sim.gen and gen ~= 0) or (gen == 0 and sim.gen == 0) then
          sim.seen = sim.seen + 3
          s.size, s.abi, s.installed, s.armed, s.error = 88, 1, sim.gen ~= 0 and 1 or 0, sim.armed and 1 or 0, sim.error
          s.seen, s.forwarded, s.replaced, s.immediate_applies, s.binds, s.ads_forwarded =
            sim.seen, sim.seen + 1, sim.seen * 2, sim.seen + 3, sim.seen + 4, sim.seen * 3
          s.last_native, s.last_applied, s.source_degrees = 1.25, sim.seen * 0.001, 70
          return 1
        end
        sim.lasterr = 17
        return 0
      end
      if a.fill then
        local b = ffi.cast('uint64_t', w.sbase)
        s.size, s.abi, s.installed, s.armed, s.error = w.ssize, w.sabi, a.inst, a.armed, a.err
        s.seen, s.forwarded, s.replaced, s.immediate_applies, s.binds, s.ads_forwarded = b, b + 1, b * 2, b + 3, b + 4, b * 3
        s.last_native, s.last_applied, s.source_degrees = w.f1, w.f2, w.f3
      end
      return a.ret
    end),
    NADSF_Error = cb('uint32_t(*)(void)', function()
      t('e')
      local r = pick(w.err, 'error')
      if r < 0 then r = sim.lasterr end
      return r
    end),
  }
end
local compat = {kind = type, number = tonumber, -- compat51 maps cdata to 'cdata' and converts it; LuaJIT's own do the same
  ffi = setmetatable({os = 'Windows', load = function() lib = build_library(); return lib end},
    {__index = ffi})}
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
local Fov = retail('src/first_person/fov.lua')

local function statstext(s)
  if type(s) ~= 'table' then return '-' end
  return table.concat({num(s.seen), num(s.forwarded), num(s.replaced), num(s.immediate_applies), num(s.binds), num(s.ads_forwarded),
    num(s.last_native), num(s.last_applied), num(s.source_degrees), num(s.installed), num(s.armed), num(s.error)}, ',')
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
  sim = {gen = 0, next = 100 * index, armed = false, error = 0, seen = 0, lasterr = 0}
  local backend, controller, leases = {}, {}, {}
  function backend:fov_config(lease)
    t('cfg(' .. (lease and lease.id or '-') .. ')')
    if w.cfg_throw then error(reason(w.cfg_why), 0) end
    if not w.cfg_ok then return nil, reason(w.cfg_why) end
    local c = {}
    for i, name in ipairs(NAMES) do if bit.band(w.cfg_missing, bit.lshift(1, i - 1)) == 0 then c[name] = w.cfg[i] end end
    return c
  end
  local function token()
    if w.pk == 1 then
      local p = {ads = nil}
      if w.pk_ads < 2 then p.ads = w.pk_ads == 1 end
      p.epoch = w.pk_epoch_num and w.pk_epoch or 'x'
      p.progress = w.pk_progress_num and w.pk_progress or 'x'
      return p
    elseif w.pk == 2 then return false, reason(w.pk_why) end
    return nil
  end
  local function packet()
    t('pkt')
    if w.pk == 3 then error('packet broke', 0) end
    return token()
  end
  function controller:result() t('res'); return {mode = w.mode and 'pose_offset' or 'other', phase = w.running and 'running' or 'waiting'} end
  function controller:is_enabled() t('en'); return w.en end
  local function clock()
    t('clk')
    if w.clk == 1 then error('clock broke', 0) elseif w.clk == 2 then return nil end
    return w.now
  end
  local function log(text) t('L[' .. text .. ']') end
  local fov = Fov.new(backend, not run.nolog and log or nil, nil, not run.noclock and clock or nil)
  local helper = assert(upvalue(fov.update, 'helper'))
  local other = Fov.Adapter.new(profile.fov, clock)
  local foreign = {owner = other, disarmed = false, generation = ffi.new('uint64_t[1]', 5)}
  local stale = {owner = helper, disarmed = false, generation = ffi.new('uint64_t[1]', 6)} -- ours by name, never begun
  local slots = {}
  return function(op)
    w, trace, counts = op, {}, {}
    if op.sim_disarm >= 0 then sim.armed, sim.error = false, op.sim_disarm end
    backend.presentation_transition = op.pk_fn and packet or nil
    local lease
    if op.lease > 0 then
      lease = leases[op.lease] or {id = op.lease}
      leases[op.lease] = lease
      lease.request = op.lr >= 0 and op.lr or nil
      lease.weapon = op.lw >= 0 and op.lw or nil
      lease.origin = op.lo and {player_identity = op.lp > 0 and ('p' .. op.lp) or nil, weapon = op.low >= 0 and op.low or nil} or nil
    end
    controller.lease = lease
    local handle
    if op.slot == -1 then handle = fov.handle elseif op.slot == -3 then handle = foreign elseif op.slot == -4 then handle = stale elseif op.slot >= 0 then handle = slots[op.slot] end
    local config
    if op.cfg_ok then
      config = {}
      for i, name in ipairs(NAMES) do if bit.band(op.cfg_missing, bit.lshift(1, i - 1)) == 0 then config[name] = op.cfg[i] end end
    end
    local a, b
    if op.call == 'update' then a, b = fov:update(controller)
    elseif op.call == 'stop' then a, b = fov:stop()
    elseif op.call == 'shutdown' then a, b = fov:shutdown()
    elseif op.call == 'preflight' then a, b = fov:preflight()
    elseif op.call == 'a_validate' then a, b = protected(helper.validate, helper, config); a = a == true
    elseif op.call == 'a_begin' then
      a, b = protected(helper.begin, helper, config)
      if a == true then slots[op.slot], b = b, nil else slots[op.slot] = helper:owned_handle() or nil end
      a = a == true
    elseif op.call == 'a_bind' then a, b = protected(helper.bind, helper, handle, assert(config)); a = a == true
    elseif op.call == 'a_transition' then a, b = protected(helper.transition, helper, handle, (token())); a = a == true
    elseif op.call == 'a_stats' then
      a, b = protected(helper.stats, helper, handle)
      assert(a == nil or not rawequal(a, fov.last) or (handle and handle.disarmed), 'script: statistics table shared with the lease')
      if type(a) == 'table' then a, b = true, a else a = false end
    elseif op.call == 'a_finish' then a, b = protected(helper.finish, helper, handle); a = a == true
    else error('unknown call ' .. tostring(op.call)) end
    local r = fov:result()
    local second = type(b) == 'table' and statstext(b) or (b == nil and '~' or tostring(b))
    print(table.concat({num(a), second,
      fov.phase, fov.handle and (fov.handle.disarmed and 2 or 1) or 0, fov.bound and fov.bound.id or 0, num(fov.bound_request),
      num(fov.bound_weapon), num(fov.bound_player), num(fov.closing), num(fov.fault), num(fov.failure_native_error),
      num(fov.failure_kind), num(fov.presentation_epoch), num(fov.presentation_progress), statstext(fov.last),
      helper.owned and 1 or 0, num(helper.last_failure_code), helper.stats_cache and 1 or 0, num(helper.stats_at), num(helper.stats_due),
      num(fov:is_clean()),
      table.concat({r.phase, num(r.cleanup_pending), num(r.error_message), num(r.failure_native_error), num(r.failure_kind),
        num(r.presentation_epoch), num(r.presentation_progress), num(r.transition_epoch), num(r.transition_progress), statstext(r)}, ';'),
      table.concat(trace, ' '), fov:status()}, '|'))
  end
end
local step, index = nil, 0
for line in io.lines(arg[2]) do
  local row = assert(loadstring('return ' .. line))()
  if row.reasons then REASONS = row.reasons
  elseif row.run then index = index + 1; step = start(row, index)
  else step(row) end
end

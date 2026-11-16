-- Replays scripts against the retail pose.lua, unmodified, under LuaJIT.
-- Usage: luajit run_pose_adapter.lua <Goose Gadget Kit folder> <script file>
-- The module gets LuaJIT's real ffi (its cdefs and struct sizes are real); ffi.load returns a
-- table of real C function pointers (ffi callbacks) whose bodies answer what the script says, so
-- argument and result conversions are LuaJIT's own. The script is one Lua table per line: for each
-- run its options followed by its operations. One line per operation: what the call returned, the
-- adapter's state, the script's handles and the ordered trace of every helper and clock call.
local ffi = require('ffi')
local bit = require('bit')
jit.off() -- callbacks must not be reached from compiled code; the interpreter converts uint64 with one rounding
local kit = arg[1]
local NAMES = {'game', 'exe', 'context', 'request', 'avatar_owner', 'avatar_index', 'avatar', 'weapon', 'body', 'viewmodel',
  'duration_ms', 'weapon_descriptor', 'weapon_unit', 'blend_ms', 'offset_x', 'offset_y', 'offset_z'}
local w, trace, counts, sim, options
local function t(text) trace[#trace + 1] = text end
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
  return table.concat({c.size, c.abi, u64(c.game), u64(c.exe), u64(c.context), u64(c.request), u64(c.avatar_owner), c.avatar_index,
    c.avatar, c.weapon, c.body, c.viewmodel, c.duration_ms, u64(c.weapon_descriptor), c.weapon_unit, c.blend_ms,
    num(c.offset_x), num(c.offset_y), num(c.offset_z), c.reserved}, ' ')
end
-- The model of the real helper's token rule (pose_helper.c read_stats_locked): while a generation
-- is reserved only that generation is served; otherwise only generation 0.
local function token(gen)
  gen = tonumber(gen)
  if sim.gen ~= 0 then return gen == sim.gen end
  return gen == 0
end
-- The helper. A scripted answer below zero asks the small model of the real helper instead.
local library, kernel
local function build_library()
  local keep = {}
  local function cb(signature, body) local f = ffi.cast(signature, body); keep[#keep + 1] = f; return f end
  local function validate(tag)
    return cb('uint32_t(*)(const NADSE_Config *)', function(c)
      t(tag .. '(' .. cfgtext(c) .. ')')
      local r = w.validate
      if r < 0 then
        if sim.gen ~= 0 then r, sim.lasterr = 0, 2 else r = 1 end
      end
      return r
    end)
  end
  local function begin(tag)
    return cb('uint32_t(*)(const NADSE_Config *,uint64_t *)', function(c, out)
      t(tag .. '(' .. cfgtext(c) .. ')')
      local r, g = w.begin, w.begin_gen
      if r < 0 then
        if sim.gen ~= 0 then r, g, sim.lasterr = 0, 0, 2
        else
          sim.next = sim.next + 1
          sim.gen, sim.armed, sim.expired, sim.error = sim.next, true, false, 0
          r, g = 1, sim.gen
        end
      elseif g ~= 0 then sim.gen, sim.armed, sim.expired, sim.error = tonumber(ffi.cast('uint64_t', g)), r == 1, false, 0 end
      out[0] = g
      return r
    end)
  end
  local function setter(gen)
    local r = w.setr
    if r < 0 then
      if tonumber(gen) == sim.gen and sim.armed then r = 1 else r, sim.lasterr = 0, 17 end
    end
    return r
  end
  local function xyz(tag)
    return cb('uint32_t(*)(uint64_t,float,float,float)', function(gen, x, y, z)
      t(tag .. '(' .. u64(gen) .. ' ' .. num(x) .. ' ' .. num(y) .. ' ' .. num(z) .. ')')
      return setter(gen)
    end)
  end
  local function transform(tag)
    return cb('uint32_t(*)(uint64_t,float,float,float,float,float,float,float)', function(gen, dx, dy, dz, qx, qy, qz, qw)
      t(tag .. '(' .. u64(gen) .. ' ' .. table.concat({num(dx), num(dy), num(dz), num(qx), num(qy), num(qz), num(qw)}, ' ') .. ')')
      return setter(gen)
    end)
  end
  local function diagnostic(tag, kind, fill)
    return cb('uint32_t(*)(uint64_t,void *)', function(gen, p)
      local s = ffi.cast(kind .. ' *', p)
      t(tag .. '(' .. u64(gen) .. ' ' .. s.size .. ' ' .. s.abi .. ' ' .. s.reserved .. ')')
      local d = w.dg
      if d.ret < 0 then
        if not token(gen) then sim.lasterr = 17; return 0 end
        fill(s, ffi.cast('uint64_t', sim.seen), sim.gen ~= 0 and 1 or 0, sim.armed and 1 or 0, 1)
        return 1
      end
      if d.fill then
        fill(s, ffi.cast('uint64_t', w.sbase), d.i1, d.i2, d.i3)
        s.size, s.abi, s.reserved = d.size, d.abi, d.reserved
      end
      return d.ret
    end)
  end
  return {
    _keep = keep,
    NADSE_Validate = validate('V'), NADSE_ValidateInput = validate('Vi'),
    NADSE_Begin = begin('B'), NADSE_BeginSteady = begin('Bs'), NADSE_BeginInput = begin('Bi'), NADSE_BeginSteadyInput = begin('Bsi'),
    NADSE_SetPlacement = xyz('P'), NADSE_SetADSPlacement = xyz('A'),
    NADSE_SetTransform = transform('M'), NADSE_SetAuthoredTransform = transform('Ma'),
    NADSE_SetZoomPolicy = cb('uint32_t(*)(uint64_t,uint32_t)', function(gen, active)
      t('Z(' .. u64(gen) .. ' ' .. active .. ')')
      return setter(gen)
    end),
    NADSE_SetTransition = cb('uint32_t(*)(uint64_t,uint64_t,float,uint32_t)', function(gen, epoch, progress, ads)
      t('T(' .. u64(gen) .. ' ' .. u64(epoch) .. ' ' .. num(progress) .. ' ' .. ads .. ')')
      return setter(gen)
    end),
    NADSE_End = cb('uint32_t(*)(uint64_t)', function(gen)
      t('E(' .. u64(gen) .. ')')
      local r = pick(w.endr, 'end')
      if r < 0 then
        if tonumber(gen) == sim.gen and sim.gen ~= 0 then sim.gen, sim.armed, sim.expired, sim.error, r = 0, false, false, 0, 1
        else r, sim.lasterr = 0, 17 end
      elseif r == 1 then sim.gen, sim.armed, sim.expired, sim.error = 0, false, false, 0 end
      return r
    end),
    NADSE_ReadStats = cb('uint32_t(*)(uint64_t,NADSE_Stats *)', function(gen, s)
      t('S(' .. u64(gen) .. ' ' .. s.size .. ' ' .. s.abi .. ')')
      local a = pick(w.st, 'stats')
      if a.ret < 0 then
        if not token(gen) then sim.lasterr = 17; return 0 end
        sim.seen = sim.seen + 3
        s.installed, s.armed, s.expired, s.error = sim.gen ~= 0 and 1 or 0, sim.armed and 1 or 0, sim.expired and 1 or 0, sim.error
        s.seen_position, s.seen_rotation, s.forwarded_position, s.forwarded_rotation, s.weapon_updates, s.zero_restores =
          sim.seen, sim.seen + 1, sim.seen * 2, sim.seen + 3, sim.seen + 4, sim.seen * 3
        s.blend, s.target_weight = sim.armed and 1 or 0, 0.5
        return 1
      end
      if a.fill then
        local b = ffi.cast('uint64_t', w.sbase)
        s.size, s.abi, s.installed, s.armed, s.expired, s.error = w.ssize, w.sabi, a.inst, a.armed, a.expired, a.err
        s.seen_position, s.seen_rotation, s.forwarded_position, s.forwarded_rotation, s.weapon_updates, s.zero_restores =
          b, b + 1, b * 2, b + 3, b + 4, b * 3
        s.blend, s.target_weight = w.f1, w.f2
      end
      return a.ret
    end),
    NADSE_ReadDepthStats = diagnostic('D', 'NADSE_DepthStats', function(s, b, i1, i2, i3)
      s.installed, s.armed, s.supported, s.reason, s.zoom_index = i1, i2, i3, b % 7, b % 5
      s.applied, s.unsupported, s.cache_refreshes, s.cache_checks, s.ads_bypassed = b, b + 1, b * 2, b + 3, b * 3
      s.native_distance, s.reference_distance, s.correction, s.zoom = w.f1, w.f2, w.f1 + w.f2, w.f1 * 2
      s.target_fov, s.current_fov, s.gain = w.f2 * 3, -w.f1, w.f2 - w.f1
    end),
    NADSE_ReadInputStats = diagnostic('I', 'NADSE_InputStats', function(s, b, i1, i2, i3)
      s.installed, s.active, s.reason, s.pending, s.cache_entries = i1, i2, b % 7, i3, b % 5
      s.observed, s.normalized, s.unsupported, s.restored, s.foreign_wins, s.quarantined = b, b + 1, b * 2, b + 3, b + 4, b * 3
      s.ratio, s.yaw_gain, s.pitch_gain = w.f1, w.f2, w.f1 + w.f2
    end),
    NADSE_Error = cb('uint32_t(*)(void)', function()
      t('e')
      local r = pick(w.err, 'error')
      if r < 0 then r = sim.lasterr end
      return r
    end),
  }
end
local profile
local loads = 0
local compat = {kind = type, number = tonumber, -- compat51 maps cdata to 'cdata' and converts it; LuaJIT's own do the same
  ffi = setmetatable({os = 'Windows', load = function(name)
    if name == 'kernel32' then -- pose.lua:33, the tick of the statistics display when Pose.new got no clock
      if not options.tick then error('cannot load module kernel32', 0) end
      kernel = kernel or {NativeADSPose_StatsTick = ffi.cast('uint64_t(*)(void)', function() t('tick'); return w.tick end)}
      return kernel
    end
    assert(name == 'KIT/' .. profile.pose.library, 'unexpected library ' .. tostring(name))
    loads = loads + 1
    library = library or build_library() -- one set of callbacks for all runs
    return library
  end}, {__index = ffi})}
mod = {dir = 'KIT', require = function(path)
  if path == 'src/first_person/compat51.lua' then return compat end
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
local Pose = retail('src/first_person/pose.lua')

local STATS = {'seen_position', 'seen_rotation', 'forwarded_position', 'forwarded_rotation', 'weapon_updates', 'zero_restores',
  'installed', 'armed', 'expired', 'error', 'blend', 'target_weight'}
local DEPTH = {'installed', 'armed', 'supported', 'reason', 'zoom_index', 'applied', 'unsupported', 'cache_refreshes', 'cache_checks',
  'ads_bypassed', 'native_distance', 'reference_distance', 'correction', 'zoom', 'target_fov', 'current_fov', 'gain'}
local INPUT = {'installed', 'active', 'reason', 'pending', 'cache_entries', 'observed', 'normalized', 'unsupported', 'restored',
  'foreign_wins', 'quarantined', 'ratio', 'yaw_gain', 'pitch_gain'}
local function fields(s, names)
  local out = {}
  for i, name in ipairs(names) do out[i] = num(s[name]) end
  return table.concat(out, ',')
end

local function start(run, index)
  mod.scope = run.scope and {} or nil
  options = run
  sim = {gen = 0, next = 100 * index, armed = false, expired = false, error = 0, seen = 0, lasterr = 0}
  local clock
  if run.clock then
    clock = function()
      t('clk')
      if w.clk == 1 then error('Engine FPV clock sample invalid', 0) elseif w.clk == 2 then return 'soon' elseif w.clk == 3 then return nil end
      return w.now
    end
  end
  -- normalize=false: a copy of the retail profile with the switch off.
  local given = profile.pose
  if not run.normalize then
    given = {}
    for k, v in pairs(profile.pose) do given[k] = v end
    given.normalize_zoom_input = false
  end
  local helper = Pose.new(given, clock)
  local other = Pose.new(profile.pose)
  local foreign = {owner = other, disarmed = false, stats = false, generation = ffi.new('uint64_t[1]', 5)}
  local stale = {owner = helper, disarmed = false, stats = false, generation = ffi.new('uint64_t[1]', 6)} -- ours by name, never begun
  local slots, prepared = {}, nil
  local before = loads
  local function which(h)
    for j = 0, 3 do if slots[j] == h then return j end end
    return 'x'
  end
  return function(op)
    w, trace, counts = op, {}, {}
    if op.sim_disarm >= 0 then sim.armed, sim.error, sim.expired = false, op.sim_disarm, op.sim_disarm == 18 end
    local handle
    if op.slot == -1 then handle = helper:owned_handle() or nil elseif op.slot == -3 then handle = foreign
    elseif op.slot == -4 then handle = stale elseif op.slot >= 0 then handle = slots[op.slot] end
    local config
    if op.cfg_ok then
      config = {steady_entry = op.steady}
      for i, name in ipairs(NAMES) do
        if bit.band(op.cfg_missing, bit.lshift(1, i - 1)) == 0 then config[name] = op.cfg[i] elseif op.cfg_false then config[name] = false end
      end
    end
    local v = {}
    for i = 1, 7 do if bit.band(op.nilmask, bit.lshift(1, i - 1)) == 0 then v[i] = op.v[i] end end
    local a, second = 'F', '~'
    local call = op.call
    if call == 'stats' then
      local s, why = helper:stats(handle)
      if type(s) == 'table' then a, second = 'T', fields(s, STATS) else assert(s == nil); second = why == nil and '~' or tostring(why) end
    elseif call == 'depth' or call == 'input' then
      local s, why
      if call == 'depth' then s, why = helper:depth_stats(handle) else s, why = helper:input_stats(handle) end
      if type(s) == 'table' then a, second = 'T', fields(s, call == 'depth' and DEPTH or INPUT) else assert(s == nil); second = tostring(why) end
    elseif call == 'prepare' then
      local called, done, value = pcall(helper.prepare, helper, config)
      if not called then a, second = 'R', tostring(done)
      elseif done == true then
        assert(type(value) == 'table' and value.owner == helper)
        a, prepared = 'T', value
      else assert(done == false); second = tostring(value) end
    elseif call == 'begin' then
      local given_token
      if op.prep == 1 then given_token = prepared or {owner = helper} elseif op.prep == 2 then given_token = {owner = other} end
      local called, done, value = pcall(helper.begin, helper, config, given_token)
      if not called then a, second = 'R', tostring(done)
      elseif done == true then
        assert(value == helper:owned_handle())
        a = 'T'
      else assert(done == false); second = tostring(value) end
      slots[op.slot] = helper:owned_handle() or nil
    elseif call == 'owned' then
      a = helper:owned_handle() and 'T' or 'F'
    elseif call == 'finish' then
      local done, value = helper:finish(handle)
      if done == true then
        a = 'T'
        if type(value) == 'table' then second = fields(value, STATS) else assert(value == nil) end
      else assert(done == false); second = tostring(value) end
    else
      local done, why
      if call == 'placement' then done, why = helper:set_placement(handle, v[1], v[2], v[3])
      elseif call == 'ads' then done, why = helper:set_ads_placement(handle, v[1], v[2], v[3])
      elseif call == 'motion' then done, why = helper:set_motion(handle, v[1], v[2], v[3], v[4], v[5], v[6], v[7], op.authored)
      elseif call == 'zoom' then
        local active
        if op.active == 0 then active = false elseif op.active == 1 then active = true end
        done, why = helper:set_zoom_policy(handle, active)
      elseif call == 'transition' then
        local packet
        if op.pk == 1 then
          packet = {epoch = op.pk_epoch_num and op.pk_epoch or 'x', progress = op.pk_progress_num and op.pk_progress or nil}
          if op.pk_ads == 0 then packet.ads = false elseif op.pk_ads == 1 then packet.ads = true end
        end
        done, why = helper:transition(handle, packet)
      else error('unknown call ' .. tostring(call)) end
      if done == true then assert(why == nil); a = 'T' else assert(done == false); second = tostring(why) end
    end
    if run.scope and loads > before then assert(#mod.scope.native_ads_pose_libraries == 1 and mod.scope.native_ads_pose_libraries[1] == library) end
    local owned = helper:owned_handle()
    -- What the C translation relies on; a script that breaks one of these fails here.
    assert(owned == false or (type(owned) == 'table' and owned.owner == helper and not owned.disarmed))
    assert(helper.stats_cache == nil or (owned and helper.stats_handle == owned and helper.stats_at ~= nil and helper.stats_due ~= nil))
    local list = {}
    for j = 0, 3 do
      local h = slots[j]
      list[#list + 1] = h and (u64(h.generation[0]) .. ':' .. (h.disarmed and 1 or 0) .. ':' ..
        (h.stats == false and 'F' or h.stats == nil and '~' or 'T')) or '-'
    end
    print(table.concat({a, second, owned and u64(owned.generation[0]) or '-', owned and which(owned) or '-',
      helper.stats_cache and fields(helper.stats_cache, STATS) or '-',
      helper.stats_handle == nil and '~' or (helper.stats_handle == owned and 'o' or 'x'), num(helper.stats_at), num(helper.stats_due),
      table.concat(list, ' '), table.concat(trace, ' ')}, '|'))
  end
end
local step, index = nil, 0
for line in io.lines(arg[2]) do
  local row = assert(loadstring('return ' .. line))()
  if row.run then index = index + 1; step = start(row, index)
  else step(row) end
end

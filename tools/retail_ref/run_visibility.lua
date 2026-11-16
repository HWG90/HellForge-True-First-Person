-- Replays scripts against the retail head_visibility.lua and model_shadow_source.lua, unmodified, under LuaJIT.
-- Usage: luajit run_visibility.lua <Goose Gadget Kit folder> <script file> [--dump <line>] [--cover <split dir> <out file>]
--
-- The world is a sparse byte memory, a table of named numbers and texts, a list of SDK Unit handles and a table
-- of named rig meshes. The backend, the engine (stingray) and the options answer from that world; tests/visibility_fake.h
-- builds the same world in C. A script line is one operation: tokens that change the world, tokens after `@N` that
-- change it when the N-th call of the operation arrives (so anything can change in the middle), and `!call`.
-- Every call the module makes outside itself is an event. All events go into a running pair of checksums in order;
-- the ones that are not plain memory reads are also listed as text. One output line per operation.
--
-- `pairs` order is unspecified in Lua (and random per process in LuaJIT: the string hash is seeded). The modules get a
-- `pairs` that walks keys in ascending order (numbers, then texts); the C walks its tables the same way.
local ffi = require('ffi')
local kit, script_path = arg[1], arg[2]
local dump_line, cover_dir, cover_out
do
  local i = 3
  while arg[i] do
    if arg[i] == '--dump' then dump_line = tonumber(arg[i + 1]); i = i + 2
    elseif arg[i] == '--cover' then cover_dir, cover_out = arg[i + 1], arg[i + 2]; i = i + 3
    else error('unknown argument ' .. arg[i]) end
  end
end
local floor = math.floor
local HIGH = 2 ^ 47

-- Numbers and texts as both sides print them.
local function num(x)
  if x == nil then return '~' end
  if x == true then return 'T' elseif x == false then return 'F' end
  if type(x) ~= 'number' then return '?' .. type(x) end
  if x ~= x then return 'nan' elseif x == math.huge then return 'inf' elseif x == -math.huge then return '-inf' end
  if x == floor(x) and x > -2 ^ 53 and x < 2 ^ 53 then return string.format('%.0f', x) end
  return string.format('%.17g', x)
end
local function hex(s) if s == nil then return '~' end return (s:gsub('.', function(c) return string.format('%02x', c:byte()) end)) end
local function unhex(s) return (s:gsub('%x%x', function(p) return string.char(tonumber(p, 16)) end)) end
local function text(s) if s == nil then return '~' end return '[' .. tostring(s) .. ']' end

-- A long part of a result line is printed as its length and checksums, unless the run asks for `full=1`.
local run
local function digest(s)
  local h1, h2 = 1, 2
  for i = 1, #s do
    local v = s:byte(i)
    h1 = (h1 * 65599 + v) % 4294967291; h2 = (h2 * 40503 + v + 1) % 4294967279
  end
  return string.format('#%d:%.0f.%.0f', #s, h1, h2)
end
local function part(s) if #s <= 64 or run.options.full == '1' then return s end return digest(s) end

-- The world.
local W
local TEXTS = {'Equip a firearm before starting the camera test', 'Local player unavailable', 'Memory unavailable'}
local function raw(at, n)
  local mem, t = W.mem, {}
  for i = 0, n - 1 do t[i + 1] = string.char(mem[at + i] or 0) end
  return table.concat(t)
end
local function rawbyte(at) return W.mem[at] or 0 end
local function rawu32(at) local m = W.mem; return (m[at] or 0) + (m[at + 1] or 0) * 256 + (m[at + 2] or 0) * 65536 + (m[at + 3] or 0) * 16777216 end
local function rawptr(at) return rawu32(at) + rawu32(at + 4) * 4294967296 end
local function poke(at, bytes) local m = W.mem; for i = 1, #bytes do m[at + i - 1] = bytes:byte(i) end end
local function uint(bytes, o) o = o or 0; local a, b, c, d = bytes:byte(o + 1, o + 4); return a + b * 256 + c * 65536 + d * 16777216 end
local function valid_id(n) return type(n) == 'number' and n > 0 and n < 4294967295 and n == floor(n) end

local bind -- defined with the fakes
local function apply(tokens)
  for _, token in ipairs(tokens) do
    local kind, rest = token:sub(1, 1), token:sub(2)
    if kind == 'P' then local at, bytes = rest:match('^(%d+)=(%x*)$'); poke(tonumber(at), unhex(bytes))
    elseif kind == 'V' then local name, value = rest:match('^([%w_]+)=(.+)$'); W.vars[name] = assert(tonumber(value), token)
    elseif kind == 'T' then local name, value = rest:match('^([%w_]+)=(.*)$'); W.strs[name] = value ~= '-' and unhex(value) or nil
    elseif kind == 'L' then W.units = {}; for a in rest:gmatch('[^,]+') do W.units[#W.units + 1] = tonumber(a) end
    elseif kind == 'X' then local lo, hi = rest:match('^(%d+),(%d+)$'); W.bad[#W.bad + 1] = {tonumber(lo), tonumber(hi)}
    elseif kind == 'x' then W.bad = {}
    elseif kind == 'M' then
      local name, addr, hash, node, mats, material, unit = rest:match('^([%w_]+),([^,]+),([^,]+),([^,]+),([^,]+),([^,]+),([^,]+)$')
      local row = {name = name, addr = tonumber(addr), hash = tonumber(hash), node = tonumber(node), mats = tonumber(mats),
        material = tonumber(material) ~= 0, unit = tonumber(unit)}
      local at = #W.rig + 1
      for i, old in ipairs(W.rig) do if old.name == name then at = i end end
      W.rig[at] = row
    elseif kind == 'm' then for i, old in ipairs(W.rig) do if old.name == rest then table.remove(W.rig, i); break end end
    elseif kind == 'K' then local at, value = rest:match('^(%d+)=(%d+)$'); W.keep[tonumber(at)] = tonumber(value)
    elseif kind == 'k' then for at, value in next, W.keep do W.mem[at] = value end
    elseif kind == 'B' then local code, mode = rest:match('^(%d+)=(%d+)$'); bind(tonumber(code), tonumber(mode))
    else error('bad token ' .. token) end
  end
end

-- Events.
local ev
local M1, M2 = 4294967291, 4294967279
local function mixi(v) ev.h1 = (ev.h1 * 65599 + v % M1) % M1; ev.h2 = (ev.h2 * 40503 + v % M2 + 1) % M2 end
local function mixs(s) mixi(#s); for i = 1, #s do mixi(s:byte(i)) end end
local function mixv(v)
  if type(v) == 'number' and v == floor(v) and v >= 0 and v < HIGH then mixi(v) else mixs(num(v)) end
end
local function tick()
  ev.n = ev.n + 1
  local trigger = W.trig[ev.n]
  if trigger then apply(trigger) end
end
local CODES = {'r', 'p', 'u', 'b', 'ctx', 'desc', 'insp', 'A'}
local function en(code, a, b, c)
  tick(); mixi(code); mixv(a)
  if b ~= nil or c ~= nil then mixv(b) end
  if c ~= nil then mixv(c) end
  if ev.full then ev.full[#ev.full + 1] = CODES[code] .. '(' .. num(a) .. (b ~= nil and ',' .. num(b) or '') .. (c ~= nil and ',' .. num(c) or '') .. ')' end
end
local function evis(s)
  tick(); mixi(255); mixs(s); ev.vis[#ev.vis + 1] = s
  if ev.full then ev.full[#ev.full + 1] = s end
end

-- Memory with the checks and failures of the backend's reads.
local function checked(at, n)
  if not (type(at) == 'number' and at == floor(at) and at >= 65536 and n > 0 and n <= 1024 and at + n < HIGH) then error('Invalid read', 0) end
  for _, range in ipairs(W.bad) do if at < range[2] and at + n > range[1] then error('Memory unavailable', 0) end end
  return raw(at, n)
end
local function cptr(at)
  local b = checked(at, 8); local value = uint(b) + uint(b, 4) * 4294967296
  if not (value >= 65536 and value < HIGH) then error('Pointer unavailable', 0) end
  return value
end
local function address(handle) return tonumber(ffi.cast('uintptr_t', handle)) end
local function handle(at) return ffi.cast('void *', at) end

-- Snapshots.
local function opt(v) if v >= 0 then return v end end
local function snapshot_table(full)
  local v, s = W.vars, W.strs
  local t = {base = v.s_base, record = v.s_record, head = v.s_head, tail = v.s_tail, camera_kind = v.s_kind,
    request_weapon = v.s_request_weapon, world = v.s_world, avatar_owner = v.s_avatar_owner, avatar_index = v.s_avatar_index,
    body = v.s_body, viewmodel = opt(v.s_viewmodel), safe = v.s_safe ~= 0, seated = v.s_seated ~= 0,
    weapon_transition = v.s_wt ~= 0, head_camera_transition = v.s_hct ~= 0, controller_active = v.s_active ~= 0,
    aiming = v.s_aiming ~= 0, player_identity = s.s_pid, controller_bytes = s.s_cbytes, flags = s.s_flags}
  if full then
    t.weapon, t.weapon_unit, t.weapon_component_descriptor = opt(v.s_weapon), opt(v.s_weapon_unit), opt(v.s_wcd)
    t.weapon_descriptor, t.identity = s.s_wdesc, s.s_identity
  else t.identity = s.s_pid end
  return t
end
local function snapshot_answer(name, switch, full)
  evis(name)
  local fail = W.vars[switch]   -- read after the call is counted: a change that arrives with this call applies to it
  if fail == 9 then return nil end
  if fail ~= 0 then return nil, TEXTS[fail] end
  return snapshot_table(full)
end

-- Engine failures: the fail_nth-th call (0: every call) of function fail_fn in this operation.
local function gate(code)
  local v = W.vars
  W.calls[code] = (W.calls[code] or 0) + 1
  if v.fail_fn == code and (v.fail_nth == 0 or v.fail_nth == W.calls[code]) then return v.fail_mode end
  return 0
end
local function rig_by_name(unit, name)
  for _, row in ipairs(W.rig) do if row.name == name and (row.unit == 0 or row.unit == unit) then return row end end
end
local function rig_by_address(at) for _, row in ipairs(W.rig) do if row.addr == at then return row end end end
local function set_mask(unit, index, visible, pass)
  local v = W.vars
  local ref = floor(unit / 4)
  local registry = rawptr(v.exe + 0x1ab76f8); if registry == 0 then return end
  local objects = rawptr(registry + 0x88); if objects == 0 then return end
  local object = rawptr(objects + (ref % 0x400000) * 8); if object == 0 then return end
  local array = rawptr(object + 0xD0); if array == 0 then return end
  local mesh = rawptr(array + (index - 1) * 16); if mesh == 0 then return end
  local bit = pass == 'default' and 1 or pass == 'shadow_caster' and 2 or 0
  if bit == 0 then return end
  local mask = rawbyte(mesh + 0xDC)
  local has = mask % (bit * 2) >= bit
  if visible and not has then mask = mask + bit elseif not visible and has then mask = mask - bit end
  W.mem[mesh + 0xDC] = mask
end

local targets, originals, alternates = {}, {}, {}
bind = function(code, mode)
  local target = targets[code]
  local fn = alternates[code]
  if mode == 0 then fn = nil elseif mode == 1 then fn = originals[code] end
  target[1][target[2]] = fn
end
local function define(code, holder, key, fn)
  targets[code] = {holder, key}; originals[code] = fn
  alternates[code] = function(...) return fn(...) end
  holder[key] = fn
end

local function build(options)
  local sr = {Application = {}, World = {}, Unit = {}, Mesh = {}}
  define(1, sr.Application, 'main_world', function()
    evis('mw'); local g = gate(1)
    if g == 1 or g == 2 then error('engine 1 failed', 0) end
    if g == 3 or W.vars.world_handle == 0 then return nil end
    return handle(W.vars.world_handle)
  end)
  define(2, sr.World, 'units', function(world)
    evis('un ' .. num(address(world))); local g = gate(2)
    if g == 1 or g == 2 then error('engine 2 failed', 0) end
    if g == 3 then return nil end
    local list = {}
    for i, at in ipairs(W.units) do list[i] = handle(at) end
    return list
  end)
  define(3, sr.Unit, 'set_mesh_visibility', function(unit, index, visible, pass)
    evis('set ' .. num(address(unit)) .. ' ' .. num(index) .. ' ' .. num(visible) .. ' ' .. tostring(pass)); local g = gate(3)
    if g == 1 then error('engine 3 failed', 0) end
    if g ~= 3 then set_mask(address(unit), index, visible, pass) end
    if g == 2 then error('engine 3 failed late', 0) end
  end)
  define(4, sr.Unit, 'has_mesh', function(unit, name)
    evis('hm ' .. num(address(unit)) .. ' ' .. name); local g = gate(4)
    if g == 1 or g == 2 then error('engine 4 failed', 0) end
    if g == 3 then return false end
    return rig_by_name(address(unit), name) ~= nil
  end)
  define(5, sr.Unit, 'mesh', function(unit, name)
    evis('me ' .. num(address(unit)) .. ' ' .. name); local g = gate(5)
    if g == 1 or g == 2 then error('engine 5 failed', 0) end
    local row = rig_by_name(address(unit), name)
    if g == 3 or not row then return nil end
    return handle(row.addr)
  end)
  local function mesh_number(code, name, field, wrong)
    define(code, sr.Mesh, name, function(mesh)
      evis('m' .. code .. ' ' .. num(address(mesh))); local g = gate(code)
      if g == 1 or g == 2 then error('engine ' .. code .. ' failed', 0) end
      local row = rig_by_address(address(mesh))
      if g == 3 or not row then return wrong end
      return row[field]
    end)
  end
  mesh_number(6, 'name', 'hash', 0); mesh_number(7, 'node', 'node', 0); mesh_number(8, 'num_materials', 'mats', 2)
  define(9, sr.Mesh, 'has_material', function(mesh, name)
    evis('m9 ' .. num(address(mesh)) .. ' ' .. name); local g = gate(9)
    if g == 1 or g == 2 then error('engine 9 failed', 0) end
    local row = rig_by_address(address(mesh))
    if g == 3 or not row then return false end
    return row.material and name == 'm_armor_standard'
  end)

  local b = {profile = {pose = {}}}
  function b:_read(at, n) en(1, at, n); return checked(at, n) end
  function b:_ptr(at) en(2, at); return cptr(at) end
  function b:_u32(at) en(3, at); return uint(checked(at, 4)) end
  function b:_u8(at) en(4, at); return checked(at, 1):byte() end
  function b:_alignment_executable()
    evis('exe')
    if W.vars.exe_fail ~= 0 then error('Executable unavailable', 0) end
    return W.vars.exe
  end
  function b:_alignment_context(t)
    local id = t.viewmodel
    en(5, id, t.avatar_owner, t.avatar_index)
    if not valid_id(id) then error('Native first-person viewmodel unavailable', 0) end
    if W.vars.exe_fail ~= 0 then error('Executable unavailable', 0) end
    local exe = W.vars.exe
    local registry = cptr(exe + 0x1ab76f8)
    local index = id % 0x400000
    local generation = floor(id / 0x400000) % 256
    local metadata = checked(registry + 0x88, 32)
    local capacity = uint(metadata, 16)
    if not (capacity > 0 and capacity <= 0x400000 and index < capacity) then error('Viewmodel registry index unavailable', 0) end
    local generations = uint(metadata, 24) + uint(metadata, 28) * 4294967296
    local units = uint(metadata) + uint(metadata, 4) * 4294967296
    if not (generations >= 65536 and generations < HIGH and units >= 65536 and units < HIGH) then error('Pointer unavailable', 0) end
    if checked(generations + index, 1):byte() ~= generation then error('Viewmodel generation retired', 0) end
    local object = cptr(units + index * 8)
    return {exe = exe, unit = id, index = index, generation = generation, object = object, registry = registry, units = units,
      generations = generations, owner = t.avatar_owner, avatar_index = t.avatar_index}
  end
  define(20, b, '_descriptor', function(_, world, id)
    en(6, world, id)
    if W.vars.desc_at == 0 then error('Entity lookup bound reached', 0) end
    local d = checked(W.vars.desc_at, 24)
    if not (uint(d, 8) == id and d:byte(21) % 2 == 1) then error('Entity generation unavailable', 0) end
    return d
  end)
  function b:snapshot() return snapshot_answer('snap', 'full_fail', true) end
  define(10, b, 'player_snapshot', function() return snapshot_answer('psnap', 'player_fail', false) end)
  define(11, b, 'pin_player_snapshot', function() return snapshot_answer('pin', 'pin_fail', false) end)
  define(12, b, 'backpack', function()
    evis('pack')
    if W.vars.pack_mode == 2 then error('Backpack inventory changed', 0) end
    if W.vars.pack_mode == 0 then return nil end
    return {ref = W.vars.pack_ref, entity = 7, identity = W.strs.pack_id}
  end)
  function b:action_state(s)
    evis('act'); local mode = W.vars.act_mode
    if mode == 0 then error('Action state unavailable', 0) end
    if mode == 4 then return nil end
    return {owner = mode == 2 and 'someone else' or s.player_identity, body = mode == 3 and s.body + 1 or s.body}
  end
  function b:on_foot()
    evis('foot')
    if W.vars.on_foot == 2 then error('On-foot test unavailable', 0) end
    return W.vars.on_foot == 1
  end
  define(13, b, 'foreign_camera', function()
    evis('fc'); local mode = W.vars.foreign
    if mode == 2 then error('Foreign camera query failed', 0) end
    if mode == 3 then return nil end
    return mode == 1
  end)

  local deps = {}
  define(14, deps, 'fallback_ready', function()
    evis('fb')
    if W.vars.fallback == 2 then error('fallback', 0) end
    return W.vars.fallback == 1
  end)
  define(15, deps, 'pinned_ready', function() evis('pr'); return W.vars.pinned == 1 end)
  define(16, deps, 'full_body', function() evis('fbd'); return W.vars.full_body == 1 end)
  define(17, deps, 'hide_torso', function() evis('ht'); return W.vars.hide_torso == 1 end)
  define(18, deps, 'show_torso', function() evis('st'); return W.vars.show_torso == 1 end)
  define(19, deps, 'seated_head', function() evis('sh'); return W.vars.seated_head == 1 end)
  local controller = {}
  function controller:result() evis('cr'); return {mode = W.vars.mode ~= 0 and 'pose_offset' or 'native'} end
  function controller:is_enabled() evis('ce'); return W.vars.enabled ~= 0 end
  local log = not options.nolog and function(message) evis('log:' .. tostring(message)) end or nil
  return sr, b, deps, controller, log
end

-- The modules: the retail bytes, with an environment that only adds the ordered `pairs` and `mod.require`.
local function ordered_pairs(t)
  local keys = {}
  for k in next, t do keys[#keys + 1] = k end
  table.sort(keys, function(a, b)
    local ta, tb = type(a), type(b)
    if ta ~= tb then return ta == 'number' end
    return a < b
  end)
  local i = 0
  return function()
    while true do
      i = i + 1
      local k = keys[i]
      if k == nil then return nil end
      local v = t[k]
      if v ~= nil then return k, v end
    end
  end
end
local kernel = {NADSH_GetTickCount64 = function() evis('clk'); return W.vars.tick end}
local PORT51 = {kind = type, number = tonumber, ffi = setmetatable({
  cast = function(kind, value)
    if kind ~= 'uintptr_t' then return ffi.cast(kind, value) end
    if not W.bridge then en(8, address(value)) end
    return address(value) + W.vars.addr_bias   -- a number: PORT51.number() of it is itself
  end,
  load = function() return kernel end}, {__index = ffi})}
-- Coverage (tools/retail_ref/cover_visibility.py): the split copies start at line 10001 and 20001, so a line
-- number alone names the file and the hook stays cheap.
local covered, executable = {}, {}
local function load_module(name)
  local chunk_name = 'src/first_person/' .. name
  local path = (cover_dir and cover_dir or kit .. '/src/first_person') .. '/' .. name
  local file = assert(io.open(path, 'rb')); local source = file:read('*a'); file:close()
  local chunk = assert(loadstring(source, '=' .. chunk_name))
  local env = setmetatable({pairs = ordered_pairs, mod = {require = function(what)
    assert(what == 'src/first_person/compat51.lua', what); return PORT51
  end}}, {__index = _G})
  setfenv(chunk, env)
  if cover_dir then
    local util = require('jit.util')
    local function walk(fn)
      local info = util.funcinfo(fn)
      for pc = 1, info.bytecodes - 1 do executable[util.funcinfo(fn, pc).currentline] = true end
      local i = -1
      while true do
        local k = util.funck(fn, i)
        if k == nil then break end
        if type(k) == 'proto' then walk(k) end
        i = i - 1
      end
    end
    walk(chunk)
  end
  return chunk()
end
if cover_dir then
  jit.off()
  debug.sethook(function(_, line) covered[line] = true end, 'l')
end
local Head, Model = load_module('head_visibility.lua'), load_module('model_shadow_source.lua')

local function upvalue(fn, name)
  local i = 1
  while true do
    local n, v = debug.getupvalue(fn, i)
    if not n then return nil end
    if n == name then return v end
    i = i + 1
  end
end
local function sorted_keys(t)
  local keys = {}
  for k in next, t do keys[#keys + 1] = k end
  table.sort(keys)
  return keys
end

local function start_run(header)
  local options = {}
  for key, value in header:gmatch('([%w_]+)=([%w_]+)') do options[key] = value end
  options.nolog = options.nolog == '1'
  W = {keep = {}, mem = {}, vars = {}, strs = {}, units = {}, bad = {}, rig = {}, trig = {}, calls = {}, bridge = options.bridge ~= nil and options.bridge ~= '0'}
  package.loaded.hd2_nativeinfo = nil
  if W.bridge then
    package.loaded.hd2_nativeinfo = {userdata_address = function(value)
      en(8, address(value))
      return address(value) + W.vars.addr_bias + (options.bridge == '2' and 4 or 0)
    end}
  end
  local sr, b, deps, controller, log = build(options)
  run = {options = options, sr = sr, b = b, deps = deps, controller = controller, changes = 0}
  if options.module == 'head' then
    local head_deps = deps
    if options.models ~= '0' then head_deps.model_shadows = Model.new(b) end
    if options.sr == '0' then sr = nil end
    run.head = Head.new(b, log, sr, head_deps)
  else run.model = Model.new(b) end
end

local function identity_text(s) return s == nil and '~' or '[' .. s .. ']' end
local function head_state(out)
  local m = run.head
  local r = m:result()
  out[#out + 1] = 'res=' .. table.concat({r.phase, num(r.hidden), num(r.hidden_head), num(r.hidden_torso), num(r.hidden_shoulders),
    num(r.hidden_body), num(r.hidden_backpack), num(r.preserved_partial), num(r.writes), num(r.restores), num(r.retired),
    num(r.foreign_masks), num(r.cleanup_pending), text(r.error_message), text(r.model_shadow_error), num(r.shadows_disabled),
    num(r.steady_observations), num(r.full_observations)}, ',')
  out[#out + 1] = 'status=' .. text(m:status()) .. ' clean=' .. num(m:is_clean())
  local appearance, owner, world = m:render_appearance()
  out[#out + 1] = 'ra=' .. part(hex(appearance)) .. ',' .. identity_text(owner) .. ',' .. num(world)
  local handles = m:sdk_handles()
  if handles ~= run.last_handles then run.changes = run.changes + 1; run.last_handles = handles end
  local list = {}
  if handles then for _, ref in ipairs(sorted_keys(handles)) do
    list[#list + 1] = num(ref) .. '=' .. (handles[ref] and num(address(handles[ref])) or 'F')
  end end
  out[#out + 1] = 'sh=' .. (handles and part(table.concat(list, ',')) or '~') .. ' chg=' .. run.changes
  local owned, ignored = upvalue(m.is_clean, 'owned'), upvalue(m.update, 'ignored')
  list = {}
  for _, key in ipairs(sorted_keys(owned)) do
    local row = owned[key]
    list[#list + 1] = table.concat({key, num(row.bits), num(row.before), num(row.written), num(row.last), num(row.current),
      num(row.restoring_default), num(row.shadow_suppressed), num(row.shadow_only), tostring(row.part), num(row.reset_epoch),
      num(row.reset_bits)}, ',')
  end
  out[#out + 1] = 'own=' .. part(table.concat(list, ';'))
  list = {}
  for _, key in ipairs(sorted_keys(ignored)) do list[#list + 1] = key .. '=' .. num(ignored[key]) end
  out[#out + 1] = 'ign=' .. part(table.concat(list, ';'))
  local epoch, resolve = upvalue(m.stop, 'epoch'), upvalue(m.update, 'resolve')
  out[#out + 1] = 'st=' .. (upvalue(m.update, 'steady') and 1 or 0) .. ' ep=' .. (epoch and num(epoch.serial) .. ',' .. num(epoch.started) or '~')
    .. ' id=' .. identity_text(upvalue(m.update, 'identity')) .. ' us=' .. num(upvalue(m.update, 'update_serial'))
    .. ' cr=' .. part(text(upvalue(resolve, 'cache_request'))) .. ' ct=' .. num(upvalue(resolve, 'cache_time'))
    .. ' ts=' .. num(upvalue(resolve, 'torso_steady_since'))
end

local function owner_text(o)
  return table.concat({identity_text(o.identity), num(o.rig), num(o.rig_object), num(o.weapon), num(o.weapon_object), num(o.world),
    num(o.parent), num(o.unit), num(o.object)}, ',')
end
local function custom_appearance()
  local v = W.vars
  return raw(v.s_avatar_owner + 0x546ab0 + v.s_avatar_index * 0x1b8 + 0xdc, 120)
end
local function fake_inspect(id, expected)
  en(7, id, expected)
  local v = W.vars
  local registry = rawptr(v.exe + 0x1ab76f8)
  local objects, generations = rawptr(registry + 0x88), rawptr(registry + 0xA0)
  local index, generation = id % 0x400000, floor(id / 0x400000) % 256
  if rawbyte(generations + index) ~= generation then error('inspect: generation retired', 0) end
  local object = rawptr(objects + index * 8)
  if v.insp_mode == 1 then object = object + 8 end
  if expected and expected ~= object then error('inspect: object changed', 0) end
  return object, raw(object, 32)
end
local function model_call(call, args, out)
  local m = run.model
  if call == 'probe' then
    local expected = snapshot_table(false)
    local appearance = custom_appearance()
    local mode = tonumber(args[1] or 0)
    if mode == 1 then appearance = run.appearance or appearance elseif mode == 2 then appearance = appearance:sub(1, 100) end
    run.appearance = appearance
    local rows, signature = m:probe(expected, appearance)
    if rows then
      run.rows = rows
      local list = {}
      for _, unit in ipairs(sorted_keys(rows)) do
        local row = rows[unit]
        list[#list + 1] = table.concat({num(unit), tostring(row.part), num(row.rig), num(row.shadow_rig), num(row.model_shadow),
          num(row.model_parent), num(#row.offsets), row.model_owner and owner_text(row.model_owner) or '~'}, ',')
      end
      out[#out + 1] = 'rows=' .. part(table.concat(list, ';')) .. ' sig=' .. part(hex(signature))
    else out[#out + 1] = 'rows=~ err=' .. text(signature) end
  elseif call == 'unchanged' then
    local s = snapshot_table(false)
    local appearance = custom_appearance()
    if tonumber(args[1] or 0) == 1 then appearance = run.appearance or appearance end
    local ok, why = m:unchanged(s, appearance, W.vars.insp_mode ~= 2 and fake_inspect or nil)
    out[#out + 1] = 'unchanged=' .. num(ok) .. ',' .. text(why)
  elseif call == 'valid' then
    local unit, tweak = tonumber(args[1]), tonumber(args[2] or 0)
    local row = run.rows and run.rows[unit]
    if not row or not row.model_owner then out[#out + 1] = 'valid=norow'
    else
      local owner = {}
      for k, v in next, row.model_owner do owner[k] = v end
      local fields = {'identity', 'rig', 'rig_object', 'weapon', 'weapon_object', 'world', 'parent', 'unit', 'object'}
      local field = fields[tweak]
      if field == 'identity' then owner.identity = owner.identity .. '!' elseif field then owner[field] = owner[field] + 8 end
      out[#out + 1] = 'valid=' .. num(m:valid(snapshot_table(false), owner))
    end
  elseif call ~= 'has' then error('unknown call ' .. call) end
  out[#out + 1] = 'has=' .. num(m:has_observation())
  local f = upvalue(m.has_observation, 'fingerprint')
  if f then
    local list = {}
    for _, id in ipairs(sorted_keys(f.inspected)) do
      local row = f.inspected[id]
      list[#list + 1] = num(id) .. ':' .. num(row.object) .. ',' .. num(row.parent) .. ',' .. num(row.world) .. ',' .. hex(row.links)
    end
    out[#out + 1] = 'fp=' .. part(table.concat({identity_text(f.owner), num(f.body), num(f.rig), num(f.weapon), num(f.weapon_unit), num(f.record),
      num(f.world), hex(f.descriptor), num(f.component), hex(f.appearance)}, ',') .. ' ' .. table.concat(list, ';'))
  end
end

local line_number = 0
for line in io.lines(script_path) do
  line_number = line_number + 1
  if line:sub(1, 4) == '#run' then start_run(line)
  else
    local now, call = {}, nil
    local target = now
    W.trig, W.calls = {}, {}
    for token in line:gmatch('%S+') do
      local kind = token:sub(1, 1)
      if kind == '@' then local n = tonumber(token:sub(2)); target = W.trig[n] or {}; W.trig[n] = target
      elseif kind == '!' then call = token:sub(2)
      else target[#target + 1] = token end
    end
    apply(now)
    ev = {n = 0, h1 = 1, h2 = 2, vis = {}, full = dump_line == line_number and {} or nil}
    local args = {}
    for part in call:gmatch('[^,]+') do args[#args + 1] = part end
    call = table.remove(args, 1)
    local out = {call}
    if run.head then
      local ok, a, b = pcall(run.head[call], run.head, run.controller)
      if not ok then out[#out + 1] = 'RAISED=' .. text(a) else out[#out + 1] = 'ret=' .. num(a) .. ',' .. text(b) end
    else
      local ok, why = pcall(model_call, call, args, out)
      if not ok then out[#out + 1] = 'RAISED=' .. text(why) end
    end
    local events = {n = ev.n, h1 = ev.h1, h2 = ev.h2, vis = ev.vis, full = ev.full}
    if run.head then head_state(out) end
    out[#out + 1] = 'ev=' .. events.n .. ',' .. num(events.h1) .. ',' .. num(events.h2) .. ' tr=' .. part(table.concat(events.vis, ';'))
    io.write(table.concat(out, ' '), '\n')
    if events.full then io.stderr:write(table.concat(events.full, '\n'), '\n') end
  end
end

if cover_dir then
  debug.sethook()
  local out = assert(io.open(cover_out, 'w'))
  for line in next, executable do
    out:write(line >= 20000 and 'src/first_person/model_shadow_source.lua' or 'src/first_person/head_visibility.lua', ' ',
      line % 10000, ' ', covered[line] and 1 or 0, '\n')
  end
  out:close()
end

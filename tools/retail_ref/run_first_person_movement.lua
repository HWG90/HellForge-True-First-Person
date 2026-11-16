-- Drives the retail first_person_movement.lua, unmodified, under LuaJIT (see effects_common.lua).
-- Usage: luajit run_first_person_movement.lua <Goose Gadget Kit folder>
-- Fakes: the backend (player snapshots, guarded memory reads, action_state / on_foot, foreign_camera and
-- _windows() with LuaJIT's real ffi: a real NativeADS_Region and Lua functions standing in for
-- VirtualQuery and WriteProcessMemory), the engine's Keyboard and Window, the controller and deps.
package.path = arg[0]:gsub('[^/]*$', '') .. '?.lua;' .. package.path
local F = require('effects_common')
local ffi = require('ffi')
F.slots({'mem',
  's_ok', 's_why', 's_base', 's_world', 's_owner', 's_index', 's_avatar', 's_body', 's_desc', 's_safe', 's_seated', 's_kind', 's_ctl',
  'p_ok', 'p_why', 'pin_first', 'pin_fn', 'has_fc', 'fc',
  'c_has', 'c_enabled', 'c_pose', 'c_has_focus', 'c_focus', 'has_fb', 'fb', 'on_foot',
  'kb', 'kb_button', 'kb_index', 'kb_bid', 'key', 's_down', 'has_mf', 'mf', 'has_sc', 'sc',
  'q_ret', 'q_state', 'q_kind', 'q_protect', 'q_base', 'q_size', 'w_ret', 'w_count', 'w_effect',
  'has_dw', 'dw', 'has_dwr', 'dwr',
  'x_read', 'x_ptr', 'x_u32', 'x_player_snapshot', 'x_pin_player_snapshot', 'x_action_state', 'x_on_foot', 'x_foreign_camera',
  'x_button_index', 'x_button_id', 'x_button', 'x_mouse_focus', 'x_show_cursor', 'x_focus_owned', 'x_writable', 'x_write', 'x_any'})
local function g(name) return F.world[name] end

-- The backend. Memory reads raise as backend.lua's do; the texts are the fake's own.
local function read(name, address, size)
  F.tick(name, F.num(address) .. (name == 'read' and ' ' .. F.num(size) or ''))
  local bytes = F.read(address, size)
  if not bytes then error('Memory unavailable', 0) end
  return bytes
end
local function uint(b, o) o = o or 0; local a, b2, c, d = b:byte(o + 1, o + 4); return a + b2 * 256 + c * 65536 + d * 16777216 end
-- A snapshot carries the number of the host call that made it (retail does not read `serial`), so
-- the trace shows which snapshot the module hands to focus_owned, fallback_ready, action_state and on_foot.
local function snapshot(name, prefix)
  F.tick(name)
  if g(prefix .. 'ok') ~= true then return nil, g(prefix .. 'why') end
  return {serial = F.calls, base = g('s_base'), world = g('s_world'), avatar_owner = g('s_owner'), avatar_index = g('s_index'),
    avatar = g('s_avatar'), body = g('s_body'), avatar_descriptor = g('s_desc'), safe = g('s_safe'), seated = g('s_seated'),
    camera_kind = g('s_kind'), controller_active = g('s_ctl')}
end
-- What the write stand-ins do to the byte: 0 store it, 1 nothing, 2 store another value.
local function effect(address, byte)
  local kind = g('w_effect') or 0
  if kind == 0 then F.poke('mem', address, string.char(byte)) elseif kind == 2 then F.poke('mem', address, string.char((byte + 2) % 256)) end
end
ffi.cdef([[typedef struct NativeADS_Region {
  uint64_t base, allocation; uint32_t allocation_protect;
  uint16_t partition, reserved; uint64_t size;
  uint32_t state, protect, kind, padding;
} NativeADS_Region;]])
local function backend()
  local b = {}
  function b:_read(address, size) return read('read', address, size) end
  function b:_ptr(address)
    local v = read('ptr', address, 8)
    local value = uint(v) + uint(v, 4) * 4294967296
    if value < 65536 then error('Pointer unavailable', 0) end
    return value
  end
  function b:_u32(address) return uint(read('u32', address, 4)) end
  function b:player_snapshot() return snapshot('player_snapshot', 's_') end
  function b:action_state(s) F.tick('action_state', F.num(s.serial)); return {serial = F.calls} end
  function b:on_foot(s, action) F.tick('on_foot', F.num(s.serial) .. ' ' .. F.num(action.serial)); return g('on_foot') end
  -- _windows() is not a host call of the C (kernel32 is simply there); it does not count.
  local win = {ffi = ffi, process = ffi.cast('void *', 0), region = ffi.new('NativeADS_Region'), k = {
    NativeADS_Query = function(pointer, r, size)
      local address = tonumber(ffi.cast('uintptr_t', pointer))
      assert(size == ffi.sizeof('NativeADS_Region'))
      F.tick('virtual_query', F.num(address))
      r.base, r.size, r.state, r.protect, r.kind = g('q_base') or 0, g('q_size') or 0, g('q_state') or 0, g('q_protect') or 0, g('q_kind') or 0
      return ffi.new('size_t', g('q_ret') or size)
    end,
    NativeADSMovement_Write = function(_, pointer, out, size, count)
      local address = tonumber(ffi.cast('uintptr_t', pointer))
      assert(size == 1)
      F.tick('write_process_memory', F.num(address) .. ' ' .. F.num(out[0]))
      effect(address, out[0])
      if g('w_count') ~= false then count[0] = g('w_count') or 1 end       -- false: the count is left as it was
      return g('w_ret') or 1
    end}}
  function b:_windows() return win end
  return b
end

local M = F.load('first_person_movement.lua')
local b, sr, deps, controller, movement
local keyboards = {}
local function pin_snapshot() return snapshot('pin_player_snapshot', 'p_') end
local function foreign_camera() F.tick('foreign_camera'); return g('fc') end
local function find(name) return function(what) F.tick(name, F.show(what)); return g('key') end end
local button_index, button_id = find('button_index'), find('button_id')
local function button(key) F.tick('button', F.show(key)); return g('s_down') end
local function mouse_focus() F.tick('mouse_focus'); return g('mf') end
local function show_cursor() F.tick('show_cursor'); return g('sc') end
local function is_enabled() F.tick('is_enabled'); return g('c_enabled') end
local function focus_owned(_, s) F.tick('focus_owned', F.num(s.serial)); return g('c_focus') end
local function fallback_ready(s) F.tick('fallback_ready', F.num(s.serial)); return g('fb') end
local function dep_writable(address) F.tick('writable', F.num(address)); return g('dw') end
local function dep_write(address, value)
  F.tick('write', F.num(address) .. ' ' .. F.num(value:byte()))
  effect(address, value:byte())
  return g('dwr')
end
-- Fields the module reads at call time follow the world; they are set before each operation.
-- A Keyboard table has an identity: another one whenever its number or its functions change.
local function sync()
  b.pin_first_person = g('pin_first')
  b.pin_player_snapshot = g('pin_fn') and pin_snapshot or nil
  b.foreign_camera = g('has_fc') and foreign_camera or nil
  if g('kb') then
    local ident = g('kb') * 8 + (g('kb_button') and 1 or 0) + (g('kb_index') and 2 or 0) + (g('kb_bid') and 4 or 0)
    keyboards[ident] = keyboards[ident] or {ident = ident, button = g('kb_button') and button or nil,
      button_index = g('kb_index') and button_index or nil, button_id = g('kb_bid') and button_id or nil}
    sr.Keyboard = keyboards[ident]
  else sr.Keyboard = nil end
  sr.Window = (g('has_mf') or g('has_sc')) and {mouse_focus = g('has_mf') and mouse_focus or nil, show_cursor = g('has_sc') and show_cursor or nil} or nil
  controller.is_enabled = g('c_has') and is_enabled or nil
  controller.focus_owned = g('c_has_focus') and focus_owned or nil
  deps.fallback_ready = g('has_fb') and fallback_ready or nil
  deps.writable = g('has_dw') and dep_writable or nil
  deps.write = g('has_dwr') and dep_write or nil
end
local function upvalue(fn, wanted)
  for i = 1, 60 do
    local name, value = debug.getupvalue(fn, i)
    if not name then return nil end
    if name == wanted then return value end
  end
end
local function record(r)
  if not r then return '~' end
  return table.concat({F.num(r.base), F.num(r.world), F.num(r.avatar_owner), F.num(r.avatar_index), F.num(r.avatar), F.num(r.body),
    F.num(r.descriptor), F.show(r.descriptor_bytes), F.num(r.pool), F.num(r.index), F.num(r.descriptors), F.num(r.rows), F.num(r.address),
    F.show(r.before)}, ' ')
end
local function fresh(with_log)
  b = backend(); sr = {}; deps = {}; keyboards = {}
  controller = {result = function() F.tick('result'); return {mode = g('c_pose') and 'pose_offset' or 'other'} end}
  movement = M.new(b, with_log and F.log or nil, sr, deps)
end
function F.dispatch(call, a)
  if call == 'new' then fresh(a[1]) end                 -- a[1]: with a log function
  sync()
  local out
  if call == 'new' then out = 'new'
  else
    local r1, r2
    if call == 'update' or call == 'update_fird' then r1, r2 = movement[call](movement, controller, a[1])   -- a[1]: skip
    else r1, r2 = movement[call](movement) end
    out = F.show(r1) .. ' ' .. F.show(r2)
  end
  local r = movement:result()
  local inner = upvalue(movement.update, 'update')
  local backward = upvalue(inner, 'backward')
  local location = upvalue(movement.stop, 'location')
  local keyboard = upvalue(backward, 'keyboard')
  return table.concat({out, '|', F.show(movement:is_clean()), r.phase, F.num(r.writes), F.num(r.restores), F.num(r.foreign_changes),
    F.show(r.reason), F.show(r.scope), F.show(r.owned), F.show(r.cleanup_pending), F.show(r.closing), F.show(movement:status()), '|',
    F.show(upvalue(movement.stop, 'draining')), F.show(upvalue(upvalue(location, 'validate'), 'verified')), F.show(upvalue(backward, 'key')),
    keyboard and F.num(keyboard.ident) or '~', '|', record(upvalue(movement.stop, 'owned'))}, ' ')
end

-- A consistent game: the fingerprints, the locomotion pool and two avatars.
local BASE, POOL, OWNER, BUCKETS, DESCS, ROWS, BIG = 0x7ff600000000, 0x20000000, 0x30000000, 0x50000000, 0x60000000, 0x70000000, 0x58000000
local MULT, EMPTY = 0x9E3779B1, 0xFFFFFFFF
local ANCHORS = {
  {0x9de28c, '4c8b1d253c3002'},
  {0x9de309, '8bc8498b4350488d148948c1e2054488b40294000000415e'},
  {0x829b92, '8bc3488d0c8048c1e1054280bc31940000000074ceb001'},
  {0x9dd574, '488b6950488bf18bda488d3c9b48c1e70580bc2f94000000007407b001'},
  {0x9dc7db, '4863d348c1e205420fb6442a0c428b0c2a4388843793000000'},
  {0x839073, '488bd6498bcde80208ffff84c07448488b4538f3420f10bc281c7c0a00f3420f10b428187c0a00f3460f108428207c0a00'},
}
-- avatar id, row index, slot under the owner, body, descriptor, descriptor address
local A = {id = 4194703, index = 2, slot = 0, body = 4194710, desc = string.rep('d', 20), at = 0x40000000}
local B = {id = 4194999, index = 1, slot = 3, body = 4195000, desc = string.rep('e', 20), at = 0x41000000}
local function mem(at, bytes) return {slot = 'mem', key = at, v = bytes} end
local function set(slot, v, at) return {slot = slot, v = v, at = at} end
local function join(...)
  local out = {}
  for _, list in ipairs({...}) do for _, e in ipairs(list) do out[#out + 1] = e end end
  return out
end
local function product(a, b2)       -- the low 32 bits of a*b (scaffolding of the fixture only)
  local lo, hi = b2 % 65536, math.floor(b2 / 65536)
  return ((a % 65536) * lo + ((math.floor(a / 65536) * lo + (a % 65536) * hi) % 65536) * 65536) % 4294967296
end
local function flag(p) return ROWS + p.index * 0xA0 + 0x94 end
-- The hash table: `cap` rows at `at`, the ids placed by linear probing in the order given. A table of
-- more than 256 rows is sparse: only the rows of the ids are mapped.
local function buckets(at, cap, mult, entries, filler)
  local free = filler or (F.le(EMPTY, 4) .. F.le(0, 4))
  local rows, taken = {}, {}
  for _, p in ipairs(entries) do
    local n = product(p.id, mult) % cap
    while taken[n] do n = (n + 1) % cap end
    taken[n] = true; rows[n] = F.le(p.id, 4) .. F.le(p.index, 4)
  end
  local out = {}
  if cap <= 256 then
    local bytes = {}
    for i = 0, cap - 1 do bytes[#bytes + 1] = rows[i] or free end
    out[1] = mem(at, table.concat(bytes))
  else
    for n, row in pairs(rows) do out[#out + 1] = mem(at + n * 8, row) end
    table.sort(out, function(x, y) return x.key < y.key end)
  end
  out[#out + 1] = mem(POOL + 0x20, F.le(at, 8) .. F.le(cap, 4) .. F.le(EMPTY, 4) .. F.le(mult, 4))
  return out
end
local function player(p)
  return {set('s_avatar', p.id), set('s_index', p.slot), set('s_body', p.body), set('s_desc', p.desc)}
end
local function avatar(p)
  return {mem(DESCS + p.index * 8, F.le(p.at, 8)), mem(OWNER + 0x110 + p.slot * 8, F.le(p.at, 8)), mem(p.at, p.desc), mem(flag(p), '\0')}
end
local function region() return {set('q_ret', nil), set('q_state', 0x1000), set('q_kind', 0x20000), set('q_protect', 4), set('q_base', ROWS), set('q_size', 0x10000)} end
local function memory()
  local e = {mem(BASE + 0x2ce1eb8, F.le(POOL, 8)), mem(POOL + 0xC, F.le(4, 4)), mem(POOL + 0x38, F.le(DESCS, 8)), mem(POOL + 0x50, F.le(ROWS, 8))}
  for _, anchor in ipairs(ANCHORS) do e[#e + 1] = mem(BASE + anchor[1], F.unhex(anchor[2])) end
  return join(e, buckets(BUCKETS, 8, MULT, {A, B}), avatar(A), avatar(B))
end
local function game(kind)
  return join(memory(), player(A), region(), {
    set('s_ok', true), set('s_base', BASE), set('s_world', 0x12340000), set('s_owner', OWNER), set('s_safe', true), set('s_seated', false),
    set('s_kind', kind or 6), set('s_ctl', false), set('p_ok', true), set('pin_first', false), set('pin_fn', false), set('has_fc', true), set('fc', false),
    set('c_has', true), set('c_enabled', true), set('c_pose', true), set('c_has_focus', true), set('c_focus', true), set('has_fb', true), set('fb', true),
    set('on_foot', true), set('kb', 1), set('kb_button', true), set('kb_index', true), set('kb_bid', false), set('key', 83), set('s_down', 0),
    set('has_mf', true), set('mf', true), set('has_sc', true), set('sc', false), set('w_ret', nil), set('w_count', nil), set('w_effect', 0),
    set('has_dw', false), set('dw', true), set('has_dwr', false), set('dwr', true)})
end
local function op(call, events, skip) return F.op(call, {skip}, events) end
local function up(events, skip) return op('update', events, skip) end
local function fird(events, skip) return op('update_fird', events, skip) end
local function hold() return up({set('s_down', 1)}) end
local function release() return up({set('s_down', 0)}) end

-- Hand-written: the life of the manual S flag.
F.run('life', function()
  F.op('new', {true})
  F.op('stop'); up(); F.op('stop', nil, game()); up(); hold(); up(); up(); release(); up()
  hold(); F.op('stop'); hold(); F.op('shutdown'); up(); F.op('stop'); F.op('shutdown')
  F.op('new', {false}); hold(); up({set('s_ok', false), set('s_why', 'Player unavailable')}); release()
  up({set('s_ok', true)}); F.op('shutdown')
end)
-- Every way an update ends before, at and after the write.
F.run('gates', function()
  F.op('new', {true}); F.op('stop', nil, game())
  local function cycle(events, back, skip)       -- owned, then the change, then everything back in order
    hold(); up(events, skip); up(); up(back); release()
  end
  cycle(nil, nil, true)
  cycle({set('c_has', false)}, {set('c_has', true)})
  cycle({set('c_enabled', false)}, {set('c_enabled', true)})
  cycle({set('c_enabled', nil)}, {set('c_enabled', true)})
  cycle({set('c_pose', false)}, {set('c_pose', true)})
  cycle({set('fc', true)}, {set('fc', false)})
  cycle({set('fc', nil)}, {set('fc', false)})
  cycle({set('fc', 1)}, {set('fc', false)})
  cycle({set('has_fc', false)}, {set('has_fc', true)})
  cycle({set('kb', nil)}, {set('kb', 1)})
  cycle({set('kb_button', false)}, {set('kb_button', true)})
  cycle({set('kb_index', false)}, {set('kb_index', true)})
  cycle({set('kb_index', false), set('kb_bid', true)}, {set('kb_index', true), set('kb_bid', false)})
  cycle({set('kb_bid', true)}, {set('kb_bid', false)})
  cycle({set('kb', 2)}, {set('kb', 1)})
  for _, key in ipairs({511, 512, 0, -1, 83.5, 'nil'}) do
    if key == 'nil' then key = nil end
    cycle({set('kb', 3), set('key', key)}, {set('kb', 4), set('key', 83)})
    cycle({set('key', 84)}, {set('kb', 1), set('key', 83)})
  end
  cycle({set('mf', false)}, {set('mf', true)})
  cycle({set('mf', 1)}, {set('mf', true)})
  cycle({set('sc', true)}, {set('sc', false)})
  cycle({set('sc', nil)}, {set('sc', false)})
  cycle({set('has_mf', false), set('sc', true)}, {set('has_mf', true), set('sc', false)})
  cycle({set('has_sc', false), set('mf', false)}, {set('has_sc', true), set('mf', true)})
  cycle({set('has_mf', false), set('has_sc', false)}, {set('has_mf', true), set('has_sc', true)})
  for _, value in ipairs({2, -1, 0.5, 'nil'}) do
    if value == 'nil' then value = nil end
    cycle({set('s_down', value)}, {set('s_down', 1)})
  end
  cycle({set('s_safe', false)}, {set('s_safe', true)})
  cycle({set('s_seated', true)}, {set('s_seated', false)})
  for _, kind in ipairs({5, 3, 7, 4}) do cycle({set('s_kind', kind)}, {set('s_kind', 6)}) end
  cycle({set('s_kind', 4), set('fb', false)}, {set('s_kind', 6), set('fb', true)})
  cycle({set('s_kind', 4), set('fb', 1)}, {set('s_kind', 6), set('fb', true)})
  cycle({set('s_kind', 4), set('has_fb', false)}, {set('s_kind', 6), set('has_fb', true)})
  cycle({set('has_fb', false)}, {set('has_fb', true)})
  cycle({set('on_foot', false)}, {set('on_foot', true)})
  cycle({set('on_foot', 1)}, {set('on_foot', true)})
  cycle({set('s_ctl', true)}, {set('s_ctl', false)})
  cycle({set('s_ok', false)}, {set('s_ok', true)})
  cycle({set('s_ok', false), set('s_why', 'Player unavailable')}, {set('s_ok', true)})
  cycle({set('pin_first', true)}, {set('pin_first', false)})
  cycle({set('pin_first', true), set('pin_fn', true)}, {set('pin_first', false)})
  cycle({set('pin_first', true), set('pin_fn', true), set('p_ok', false)}, {set('p_ok', true)})
  cycle({set('p_why', 'Pinned body unavailable'), set('p_ok', false)}, {set('p_ok', true), set('pin_first', false), set('pin_fn', false)})
  cycle({set('pin_first', 1), set('pin_fn', true)}, {set('pin_first', false), set('pin_fn', false)})
  -- the flag itself
  for _, value in ipairs({'\0', '\2', '\255'}) do
    hold(); up({mem(flag(A), value)}); up(); up(); release(); up({mem(flag(A), '\0')}); hold(); F.op('stop')
    hold(); F.op('stop', nil, {mem(flag(A), value)}); hold(); release(); up({mem(flag(A), '\0')})
  end
  up({mem(flag(A), '\1'), set('s_down', 1)}); up(); release(); F.op('stop'); hold(); up({mem(flag(A), '\0')}); release()
  up({mem(flag(A), '\2'), set('s_down', 1)}); up(); up({mem(flag(A), '\0')}); release()
  -- another player takes the avatar: the lease retires without a write
  hold(); up(player(B)); up(); up(player(A)); up(); release(); up({mem(flag(B), '\0')})
  hold(); F.op('stop', nil, player(B)); F.op('stop', nil, player(A)); hold(); release()
  for _, e in ipairs({set('s_base', BASE + 0x1000), set('s_world', 0x12350000), set('s_owner', OWNER + 8), set('s_index', 1), set('s_body', 5),
      set('s_desc', string.rep('d', 19) .. 'x'), set('s_avatar', B.id)}) do
    hold(); F.op('stop', nil, {e}); F.op('stop'); up(); up(game(6)); hold(); up({e}); up(); up(game(6)); up({set('s_down', 0)})
  end
  -- a lease that differs from the current place in one identity field only
  local OWNER2, alias = OWNER + 0x1000, {id = A.id + 1, index = A.index}
  for _, events in ipairs({
      join(buckets(BUCKETS, 8, MULT, {A, B, alias}), {set('s_avatar', alias.id)}),
      {set('s_owner', OWNER2), mem(OWNER2 + 0x110, F.le(A.at, 8))},
      {set('s_index', 1), mem(OWNER + 0x118, F.le(A.at, 8))},
      {set('s_desc', string.rep('x', 20)), mem(A.at, string.rep('x', 20))},
      {set('s_body', 77)}, {set('s_world', 0x12350000)}}) do
    hold(); up(events); up(); up(join(game(6), {mem(flag(A), '\0')})); release()
    hold(); F.op('stop', nil, events); up(game(6)); release()
  end
  for _, id in ipairs({0, 0xFFFFFFFF, 0xFFFFFFFE, 1}) do
    hold(); up({set('s_avatar', id)}); F.op('stop'); up(player(A)); release()
  end
end)
-- The pool walk: every refusal of location() and of the fingerprints.
F.run('location', function()
  F.op('new', {true}); F.op('stop', nil, game())
  local function broken(events, back)
    up(join({set('s_down', 1)}, events)); up(); up(back or memory()); up(); up(events); F.op('stop'); up(back or memory()); release()
  end
  -- each fingerprint, first and last byte, at a module base that was not verified before
  local function at_base(base2, extra)
    local out = {}
    for _, e in ipairs(memory()) do if e.key >= BASE then out[#out + 1] = mem(e.key - BASE + base2, e.v) end end
    out[#out + 1] = set('s_base', base2)
    return join(out, extra)
  end
  local k = 0
  for _, anchor in ipairs(ANCHORS) do
    local bytes = F.unhex(anchor[2])
    for _, offset in ipairs({0, #bytes - 1}) do
      k = k + 1
      local base2, good = BASE + k * 0x10000000, bytes:sub(offset + 1, offset + 1)
      up(join({set('s_down', 1)}, at_base(base2, {mem(base2 + anchor[1] + offset, string.char((good:byte() + 1) % 256))}))); up()
      up({mem(base2 + anchor[1] + offset, good)}); up(); release()              -- repaired: verified there
      hold(); up({mem(base2 + anchor[1] + offset, 'x')}); release()               -- not read again
      up({set('s_base', BASE)}); hold(); release()
    end
  end
  -- another module base: the fingerprints are read again there, once
  local moved = {}
  for _, e in ipairs(memory()) do if e.key >= BASE then moved[#moved + 1] = mem(e.key + 0x10000000, e.v) end end
  hold(); up(join(moved, {set('s_base', BASE + 0x10000000)})); up(); hold(); up(); up({set('s_base', BASE)}); up(); release()
  broken({mem(BASE + 0x2ce1eb8, 8)}); broken({mem(BASE + 0x2ce1eb8, F.le(1, 8))})
  for _, active in ipairs({0, 16385, 2, 1}) do broken({mem(POOL + 0xC, F.le(active, 4))}) end
  for _, active in ipairs({3, 16384}) do hold(); up({mem(POOL + 0xC, F.le(active, 4))}); release(); up(memory()) end
  broken({mem(POOL + 0xC, 4)}); broken({mem(POOL + 0x20, 20)}); broken({mem(POOL + 0x33, 1)})
  for _, cap in ipairs({0, 3, 6, 131072, 4294967295}) do broken({mem(POOL + 0x28, F.le(cap, 4))}) end
  broken({mem(POOL + 0x2C, F.le(A.id, 4))})
  broken({mem(BUCKETS, 64)}); broken({mem(POOL + 0x20, F.le(2, 8))})
  broken(buckets(BUCKETS, 8, MULT, {B}))                                         -- absent: an empty row first
  broken(buckets(BUCKETS, 8, MULT, {B}, F.le(7, 4) .. F.le(0, 4)))               -- absent: every row someone else's
  broken(buckets(BIG, 256, MULT, {}, F.le(7, 4) .. F.le(0, 4)))                  -- 128 rows probed, none ours
  broken(buckets(BUCKETS, 1, MULT, {}, F.le(7, 4) .. F.le(0, 4)))
  broken(buckets(BUCKETS, 8, MULT, {{id = A.id, index = 4}, B})); broken(buckets(BUCKETS, 8, MULT, {{id = A.id, index = 5}, B}))
  -- found behind other rows, in tables of several sizes and multipliers
  for _, shape in ipairs({{8, 1}, {8, 0}, {1, MULT}, {2, 3}, {128, MULT}, {256, MULT}, {65536, 0xFFFFFFFF}, {65536, 65537}, {4096, 0x10001}}) do
    local others = {}
    for n = 1, math.min(shape[1] - 1, 5) do others[n] = {id = A.id + n * shape[1], index = 0} end
    others[#others + 1] = A
    local at = shape[1] > 8 and BIG or BUCKETS
    hold(); up(buckets(at, shape[1], shape[2], others)); up(); release(); up(memory())
  end
  do      -- our row is the 128th probed, then the 129th
    local filler = F.le(7, 4) .. F.le(0, 4)
    local first = product(A.id, MULT) % 256
    for _, n in ipairs({127, 128}) do
      local table_events = buckets(BIG, 256, MULT, {}, filler)
      table_events[#table_events + 1] = mem(BIG + ((first + n) % 256) * 8, F.le(A.id, 4) .. F.le(A.index, 4))
      broken(table_events)
    end
  end
  broken({mem(POOL + 0x38, 8)}); broken({mem(DESCS + A.index * 8, 8)}); broken({mem(DESCS + A.index * 8, F.le(B.at, 8))})
  broken({mem(OWNER + 0x110, 8)}); broken({mem(OWNER + 0x110, F.le(B.at, 8))}); broken({mem(A.at + 19, 'x')}); broken({mem(A.at, 'x')})
  broken({mem(A.at + 19, 1)}); broken({mem(POOL + 0x50, 8)}); broken({mem(POOL + 0x50, F.le(ROWS + 0x100000, 8))})
  broken({mem(flag(A), 1)})
  -- the same avatar at another row, descriptor table or pool: a different lease
  local other = {mem(DESCS + 3 * 8, F.le(A.at, 8)), mem(ROWS + 3 * 0xA0 + 0x94, '\0')}
  hold(); up(join(buckets(BUCKETS, 8, MULT, {{id = A.id, index = 3}, B}), other)); up(); up(); release(); up(memory())
  hold(); up({mem(POOL + 0x38, F.le(DESCS + 0x1000, 8)), mem(DESCS + 0x1000 + A.index * 8, F.le(A.at, 8))}); up(); release(); up(memory())
  hold(); up({mem(POOL + 0x50, F.le(ROWS + 0x4000, 8)), mem(ROWS + 0x4000 + A.index * 0xA0 + 0x94, '\0')}); up(); release(); up(memory())
  local pool2 = {}
  for _, e in ipairs(memory()) do if e.key >= POOL and e.key < POOL + 0x100 then pool2[#pool2 + 1] = mem(e.key + 0x1000, e.v) end end
  pool2 = join(pool2, buckets(BUCKETS, 8, MULT, {A, B}))
  pool2[#pool2].key = POOL + 0x1020
  hold(); up(join(pool2, {mem(BASE + 0x2ce1eb8, F.le(POOL + 0x1000, 8))})); up(); release(); up(memory())
  local desc2 = {mem(DESCS + A.index * 8, F.le(A.at + 0x100, 8)), mem(OWNER + 0x110, F.le(A.at + 0x100, 8)), mem(A.at + 0x100, A.desc)}
  hold(); up(desc2); up(); release(); up(memory())
  -- the retiring stop fails: the update reports it and the next one cleans up
  hold(); up(join(desc2, {set('q_protect', 2)})); up(); up(region()); up(); release(); up(memory())
  hold(); up(join(desc2, {set('q_protect', 2)})); F.op('stop'); F.op('shutdown', nil, region()); up(memory())
  -- the stop that retires the old place fails in the middle of the update (line 210), for good or for one call
  F.op('new', {true}); F.op('stop', nil, game()); hold()
  local calls = F.last_calls
  release()
  for at = 1, calls do
    hold(); up(join(desc2, {set('s_ok', false, at)})); up(); up({set('s_ok', true)}); up(memory()); release(); up({mem(flag(A), '\0')})
    hold(); up(join(desc2, {set('s_ok', false, at), set('s_ok', true, at + 1)})); up(); up(memory()); release(); up({mem(flag(A), '\0')})
    hold(); up(join(desc2, {set('x_any', 'Invalid read', at)})); up(); up(memory()); release(); up({mem(flag(A), '\0')})
  end
end)
-- The write: the page query, WriteProcessMemory and the two test hooks of deps.
F.run('store', function()
  F.op('new', {true}); F.op('stop', nil, game())
  local function attempt(events, back)
    up(join({set('s_down', 1)}, events)); up(); up(back or join(region(), {set('w_ret', nil), set('w_count', nil), set('w_effect', 0)})); release()
    hold(); F.op('stop', nil, events); F.op('stop'); F.op('stop', nil, back or join(region(), {set('w_ret', nil), set('w_count', nil), set('w_effect', 0)}))
    up({mem(flag(A), '\0'), set('s_down', 0)})
  end
  for _, e in ipairs({set('q_ret', 0), set('q_ret', 47), set('q_ret', 49), set('q_state', 0x2000), set('q_state', 0), set('q_kind', 0x40000),
      set('q_kind', 0x1000000), set('q_protect', 2), set('q_protect', 0x40), set('q_protect', 0x104), set('q_base', flag(A) + 1),
      set('q_base', ROWS + 0x1000), set('q_size', 0x100), set('q_size', A.index * 0xA0 + 0x94), set('q_size', 0),
      set('w_ret', 0), set('w_count', 0), set('w_count', 2), set('w_count', false), set('w_effect', 1), set('w_effect', 2)}) do
    attempt({e})
  end
  for _, e in ipairs({set('q_protect', 8), set('q_base', flag(A)), set('q_size', A.index * 0xA0 + 0x95), set('w_ret', 5), set('w_ret', -1)}) do
    hold(); up({e}); release(); up(join(region(), {set('w_ret', nil)}))
  end
  attempt({set('q_base', flag(A)), set('q_size', 0)}); attempt({set('q_base', flag(A)), set('q_size', 1)})
  -- a reported failure that still stored the byte: the next stop verifies and retires
  hold(); F.op('stop', nil, {set('w_ret', 0)}); F.op('stop'); up({set('w_ret', nil)}); release()
  hold(); F.op('stop', nil, {set('w_count', 0)}); hold(); F.op('stop', nil, {set('w_count', nil)}); release()
  -- deps.writable / deps.write
  local hooks = {set('has_dw', true), set('has_dwr', true), set('dw', true), set('dwr', true)}
  local plain = {set('has_dw', false), set('has_dwr', false), set('dw', true), set('dwr', true), set('w_effect', 0)}
  up(hooks); hold(); up(); release()
  for _, e in ipairs({set('dw', false), set('dw', 1), set('dw', nil), set('dwr', false), set('dwr', 1), set('dwr', nil), set('w_effect', 1), set('w_effect', 2)}) do
    attempt(join(hooks, {e}), hooks)
  end
  attempt({set('has_dw', true), set('dw', false)}, plain); attempt({set('has_dwr', true), set('dwr', false)}, plain)
  up({set('has_dw', true)}); hold(); release(); up(plain); up({set('has_dwr', true)}); hold(); release(); up(plain)
end)
-- Changes in the middle of an acquisition and of a restoration.
F.run('midcall', function()
  F.op('new', {true}); F.op('stop', nil, game())
  hold()
  local calls_hold = F.last_calls
  release()
  local calls_release = F.last_calls
  local changes = {function() return set('s_safe', false) end, function() return set('s_seated', true) end, function() return set('s_kind', 5) end,
    function() return set('s_kind', 4) end, function() return set('s_avatar', B.id) end, function() return set('s_body', 9) end,
    function() return mem(flag(A), '\1') end, function() return mem(flag(A), '\2') end, function() return set('s_ok', false) end,
    function() return mem(POOL + 0x50, F.le(ROWS + 0x4000, 8)) end, function() return mem(A.at, 'x') end, function() return set('fb', false) end,
    function() return set('x_any', 'Invalid read') end, function() return set('mf', false) end, function() return set('on_foot', false) end}
  for _, change in ipairs(changes) do
    for at = 1, calls_hold + 1 do
      local e = change(); e.at = at
      up({set('s_down', 1), e}); up(); F.op('stop', nil, join(game(), {mem(ROWS + 0x4000 + A.index * 0xA0 + 0x94, '\0')}))
    end
    for at = 1, calls_release + 1 do
      local e = change(); e.at = at
      hold(); up({set('s_down', 0), e}); up(); F.op('stop', nil, join(game(), {mem(ROWS + 0x4000 + A.index * 0xA0 + 0x94, '\0')}))
    end
  end
  -- camera kind 4 uses the fallback camera callback
  F.op('stop', nil, game(4)); hold()
  local calls4 = F.last_calls
  release()
  for at = 1, calls4 + 1 do
    up({set('s_down', 1), set('fb', false, at)}); up(); F.op('stop', nil, game(4))
    up({set('s_down', 1), set('s_kind', 6, at)}); up(); F.op('stop', nil, game(4))
  end
end)
-- Natural Fird: the focused interval (parked upstream; update_fird is still translated).
F.run('fird', function()
  F.op('new', {true}); F.op('stop', nil, game(4))
  fird(); fird(); fird(nil, true); fird(); F.op('stop'); fird(); F.op('shutdown'); fird()
  F.op('new', {true}); fird()
  local function cycle(events, back)
    fird(); fird(events); fird(); fird(back); F.op('stop')
  end
  cycle({set('c_has_focus', false)}, {set('c_has_focus', true)})
  cycle({set('c_focus', false)}, {set('c_focus', true)}); cycle({set('c_focus', 1)}, {set('c_focus', true)})
  cycle({set('mf', false)}, {set('mf', true)}); cycle({set('sc', true)}, {set('sc', false)})
  cycle({set('s_kind', 6)}, {set('s_kind', 4)}); cycle({set('s_ctl', true)}, {set('s_ctl', false)})
  cycle({set('s_safe', false)}, {set('s_safe', true)}); cycle({set('s_seated', true)}, {set('s_seated', false)})
  cycle({set('has_fb', false)}, {set('has_fb', true)}); cycle({set('fb', false)}, {set('fb', true)}); cycle({set('fb', 1)}, {set('fb', true)})
  cycle({set('on_foot', false)}, {set('on_foot', true)}); cycle({set('fc', true)}, {set('fc', false)})
  cycle({set('c_enabled', false)}, {set('c_enabled', true)}); cycle({set('kb', nil)}, {set('kb', 1)})
  cycle({set('x_focus_owned', 'Focused camera owner unavailable')})
  -- a foreign change drains until the scope is left
  fird(); fird({mem(flag(A), '\0')}); fird(); fird(); F.op('stop'); fird(); F.op('stop')
  fird(); fird({mem(flag(A), '\2')}); fird(); up(); fird({mem(flag(A), '\0')}); fird(); F.op('stop')
  fird(); F.op('stop', nil, {mem(flag(A), '\0')}); fird(); F.op('stop')
  fird({mem(flag(A), '\1')}); fird(); F.op('stop', nil, {mem(flag(A), '\0')})
  -- manual and fird take turns
  fird(); hold(); up(); fird(); release(); fird(); hold(); F.op('stop'); fird(); release(); F.op('stop')
  hold(); up({mem(flag(A), '\0')}); fird(); fird(); up(); release(); fird(); F.op('stop')
  fird()
  local calls = F.last_calls
  F.op('stop')
  for at = 1, calls + 1 do
    for _, change in ipairs({function() return set('c_focus', false) end, function() return set('s_ctl', true) end, function() return set('s_kind', 6) end,
        function() return set('sc', true) end, function() return set('fb', false) end, function() return set('s_safe', false) end,
        function() return set('x_any', 'Invalid read') end}) do
      local e = change(); e.at = at
      fird({e}); fird(); F.op('stop', nil, game(4))
    end
  end
end)

-- Seeded walks: random world changes, some landing in the middle of an operation.
local function walk(seed, count)
  F.seed(seed)
  local kind = F.pick({6, 6, 4})
  F.op('new', {F.chance(.9)})
  F.op('stop', nil, game(kind))
  local raises = {'x_read', 'x_ptr', 'x_u32', 'x_player_snapshot', 'x_pin_player_snapshot', 'x_action_state', 'x_on_foot', 'x_foreign_camera',
    'x_button_index', 'x_button_id', 'x_button', 'x_mouse_focus', 'x_show_cursor', 'x_focus_owned', 'x_writable', 'x_write'}
  local function change()
    local at = F.chance(.4) and F.rnd(F.last_calls + 3) or 0
    local k, e = F.rnd(52)
    if k == 1 then e = set('c_enabled', F.chance(.7))
    elseif k == 2 then e = set('c_pose', F.chance(.7))
    elseif k == 3 then e = set('fc', F.pick({false, false, false, true, 1}))
    elseif k == 4 then e = set('has_fc', F.chance(.8))
    elseif k == 5 or k == 6 or k == 7 then e = set('s_down', F.pick({0, 1, 1, 1, 1, 1, 1, 2}))
    elseif k == 8 then e = set('s_safe', F.chance(.8))
    elseif k == 9 then e = set('s_seated', F.chance(.2))
    elseif k == 10 then e = set('s_kind', F.pick({4, 4, 6, 6, 6, 5}))
    elseif k == 11 then e = set('s_ctl', F.chance(.2))
    elseif k == 12 then e = set('s_ok', F.chance(.8))
    elseif k == 13 then e = set('s_why', F.pick({'Player unavailable', 'Memory unavailable'}))
    elseif k == 14 then return player(F.pick({A, A, B}))
    elseif k == 15 then e = set('s_base', F.pick({BASE, BASE, BASE + 0x10000000}))
    elseif k == 16 then e = set('s_world', F.pick({0x12340000, 0x12340000, 0x12350000}))
    elseif k == 17 then e = set('pin_first', F.chance(.3))
    elseif k == 18 then e = set('pin_fn', F.chance(.5))
    elseif k == 19 then e = set('p_ok', F.chance(.8))
    elseif k == 20 then e = set('c_focus', F.chance(.8))
    elseif k == 21 then e = set('c_has_focus', F.chance(.8))
    elseif k == 22 then e = set('fb', F.chance(.8))
    elseif k == 23 then e = set('has_fb', F.chance(.8))
    elseif k == 24 then e = set('on_foot', F.chance(.8))
    elseif k == 25 then e = set('kb', F.pick({1, 1, 2})); at = 0
    elseif k == 26 then e = set('key', F.pick({83, 83, 84, 600})); at = 0
    elseif k == 27 then e = set('mf', F.chance(.8))
    elseif k == 28 then e = set('sc', F.chance(.2))
    elseif k == 29 then e = mem(flag(F.pick({A, A, B})), F.pick({'\0', '\0', '\0', '\1', '\1', '\2'}))
    elseif k == 30 then e = set('q_protect', F.pick({4, 4, 8, 2}))
    elseif k == 31 then e = set('q_ret', F.pick({48, 48, 0}))
    elseif k == 32 then e = set('w_ret', F.pick({1, 1, 0}))
    elseif k == 33 then e = set('w_count', F.pick({1, 1, 0}))
    elseif k == 34 then e = set('w_effect', F.pick({0, 0, 0, 1, 2}))
    elseif k == 35 then e = set('has_dw', F.chance(.3))
    elseif k == 36 then e = set('has_dwr', F.chance(.3))
    elseif k == 37 then e = set('dw', F.chance(.8))
    elseif k == 38 then e = set('dwr', F.chance(.8))
    elseif k == 39 then e = set(F.pick(raises), F.pick({'Memory unavailable', 'Invalid read'})); at = F.rnd(F.last_calls + 3)
    elseif k == 40 then e = mem(F.pick({POOL + 0xC, POOL + 0x28, POOL + 0x2C, POOL + 0x30, BUCKETS + 8, BUCKETS + 40, DESCS + 16, OWNER + 0x110, A.at + 3,
      BASE + 0x9de28c, BASE + 0x839073 + 20}), F.pick({'\0\0\0\0', '\1\0\0\0', 'H', '\3'}))
    elseif k == 41 then e = mem(F.pick({POOL + 0xC, POOL + 0x20, BUCKETS, DESCS + 16, A.at, POOL + 0x50, BASE + 0x2ce1eb8, flag(A), OWNER + 0x110}), 4)
    elseif k == 42 then return buckets(BUCKETS, F.pick({8, 8, 4, 16, 2}), F.pick({MULT, 1, 0x10001}), F.pick({{A, B}, {B, A}, {B}, {A}}))
    elseif k == 43 then e = set('q_base', F.pick({ROWS, ROWS, flag(A), flag(A) + 1}))
    elseif k == 44 then e = set('q_size', F.pick({0x10000, 0x10000, 1, 0}))
    elseif k == 45 then e = set('q_state', F.pick({0x1000, 0x1000, 0x2000}))
    elseif k == 46 then e = set('q_kind', F.pick({0x20000, 0x20000, 0x40000}))
    elseif k == 47 then e = set('has_mf', F.chance(.8)); at = 0
    elseif k == 48 then e = set('has_sc', F.chance(.8)); at = 0
    elseif k == 49 then e = set('c_has', F.chance(.9)); at = 0
    elseif k == 50 then e = set('kb_index', F.chance(.8)); at = 0
    elseif k == 51 then e = set('kb_bid', F.chance(.3)); at = 0
    else return game(kind) end
    e.at = at
    return {e}
  end
  for _ = 1, count do
    local events = {}
    for _ = 1, F.pick({0, 0, 1, 1, 1, 2, 3}) do events = join(events, change()) end
    if F.chance(.12) then events[#events + 1] = set('x_any', F.pick({'Memory unavailable', 'Invalid read'}), F.rnd(F.last_calls + 2)) end
    if F.chance(.25) then events = join(events, game(kind), {set('s_down', F.pick({1, 1, 1, 0}))}) end    -- everything back in order
    local call = F.pick({'update', 'update', 'update', 'update', 'update', 'stop', 'stop', F.chance(.02) and 'shutdown' or 'update'})
    if seed % 3 == 0 and call == 'update' and F.chance(.6) then call = 'update_fird' end
    F.op(call, {F.chance(.05)}, events)
  end
end
for seed = 1, 9 do F.run('walk' .. seed, function() walk(seed, 900) end) end
F.finish()

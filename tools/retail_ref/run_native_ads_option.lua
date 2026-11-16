-- Drives the retail native_ads_option.lua, unmodified, under LuaJIT (see effects_common.lua).
-- Usage: luajit run_native_ads_option.lua <Goose Gadget Kit folder>
-- Fakes: the backend (snapshots, guarded memory reads, the hash-map lookup, foreign_camera), the
-- memory writer of src/memory.lua, the menu switch, the camera-ownership callback and the controller.
package.path = arg[0]:gsub('[^/]*$', '') .. '?.lua;' .. package.path
local F = require('effects_common')
F.slots({'mem', 'map',
  's_ok', 's_why', 's_base', 's_owner', 's_pid', 's_id', 's_wd', 's_weapon', 's_safe', 's_kind',
  'p_ok', 'p_why', 'p_base', 'p_owner', 'p_pid', 'pin_first', 'pin_fn',
  'has_fc', 'fc', 'copy_ok', 'copy_why', 'copy_effect',
  'enabled', 'has_foreign', 'foreign', 'c_enabled', 'c_pose',
  'x_read', 'x_ptr', 'x_u32', 'x_u8', 'x_map', 'x_copy', 'x_any'})
local W = F.world
local function g(name) return F.world[name] end

-- The backend. Memory reads raise as backend.lua's do; the texts are the fake's own.
local function read(name, address, size)
  F.tick(name, F.num(address) .. (name == 'read' and ' ' .. F.num(size) or ''))
  local bytes = F.read(address, size)
  if not bytes then error('Memory unavailable', 0) end
  return bytes
end
local function uint(b, o) o = o or 0; local a, b2, c, d = b:byte(o + 1, o + 4); return a + b2 * 256 + c * 65536 + d * 16777216 end
local function snapshot(name, prefix, full)
  F.tick(name)
  if g(prefix .. 'ok') ~= true then return nil, g(prefix .. 'why') end
  local s = {base = g(prefix .. 'base'), avatar_owner = g(prefix .. 'owner'), player_identity = g(prefix .. 'pid')}
  if full then
    s.identity, s.weapon_descriptor, s.weapon, s.safe, s.camera_kind = g('s_id'), g('s_wd'), g('s_weapon'), g('s_safe'), g('s_kind')
  end
  return s
end
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
  function b:_u8(address) return read('u8', address, 1):byte() end
  function b:_map(address, id, limit)
    F.tick('map', F.num(address) .. ' ' .. F.num(id) .. ' ' .. F.show(limit))
    local index = F.getk('map', (address % 65536) * 4294967296 + id)
    if index == nil then error('Entity absent', 0) end
    if index >= (limit or 4096) then error('Entity index out of bounds', 0) end
    return index
  end
  function b:snapshot() return snapshot('snapshot', 's_', true) end
  function b:player_snapshot() return snapshot('player_snapshot', 'p_') end
  return b
end
-- Fields the module reads at call time follow the world.
local function sync(b)
  b.pin_first_person = g('pin_first')
  b.pin_player_snapshot = g('pin_fn') and function() return snapshot('pin_player_snapshot', 'p_') end or nil
  b.foreign_camera = g('has_fc') and function() F.tick('foreign_camera'); return g('fc') end or nil
end
local function memory()
  return {copy = function(_, at, bytes)
    F.tick('copy', F.num(at) .. ' ' .. F.str(bytes))
    if g('copy_ok') ~= true then return false, g('copy_why') end
    local effect = g('copy_effect') or 0
    if effect == 0 then F.poke('mem', at, bytes) elseif effect == 2 then F.poke('mem', at, string.char(1 - bytes:byte())) end
    return true
  end}
end

local M = F.load('native_ads_option.lua', {['src/memory.lua'] = function() return {new = memory} end})
local controller = {}
function controller:is_enabled() F.tick('is_enabled'); return g('c_enabled') end
function controller:result() F.tick('result'); return {mode = g('c_pose') and 'pose_offset' or 'other'} end

local b, source, lease, records
local function upvalue(fn, wanted)
  for i = 1, 60 do
    local name, value = debug.getupvalue(fn, i)
    if not name then return nil end
    if name == wanted then return value end
  end
end
local function record(r)
  if not r then return '~' end
  return table.concat({F.num(r.base), F.num(r.player), F.num(r.settings), F.num(r.mode), F.show(r.owner), F.num(r.weapon), F.num(r.at),
    F.show(r.key), F.show(r.descriptor), F.num(r.value), F.show(r.before)}, ' ')
end
local function fresh(with_foreign)
  b = backend(); source = M.source(b); records = {}
  lease = M.new(source, function() F.tick('enabled'); return g('enabled') end,
    with_foreign and function() F.tick('foreign'); return g('foreign') end or nil)
end
function F.dispatch(call, a)
  if call == 'new' then fresh(a[1]) end                 -- a[1]: with the camera-ownership callback
  sync(b)
  local out
  if call == 'new' then out = 'new'
  elseif call == 'update' or call == 'stop' or call == 'shutdown' then
    local r1, r2
    if call == 'update' then r1, r2 = lease:update(controller) else r1, r2 = lease[call](lease) end
    out = F.show(r1) .. ' ' .. F.show(r2)
  elseif call == 'probe' then      -- a[1]: the slot that receives the record
    local ok, r = pcall(source.probe, source)
    if ok then records[a[1]] = r; out = 'ok' else out = 'raised ' .. F.show(r) end
  elseif call == 'read' then
    local ok, v, why = pcall(source.read, source, records[a[1]])
    out = ok and F.show(v) .. ' ' .. F.show(why) or 'raised ' .. F.show(v)
  elseif call == 'write' then      -- a[2] expected, a[3] value
    local ok, why = pcall(source.write, source, records[a[1]], a[2], a[3])
    out = ok and 'ok' or 'raised ' .. F.show(why)
  else error(call) end
  if call == 'probe' or call == 'read' or call == 'write' then return out .. ' | ' .. record(records[a[1]]) end
  local r = lease:result()
  return table.concat({out, '|', r.phase, F.show(lease:is_clean()), F.show(r.owned), F.num(r.writes), F.num(r.restores),
    F.show(r.error_message), F.show(lease.phase), '|', record(upvalue(lease.stop, 'lease'))}, ' ')
end

-- A consistent game: the fingerprints, the player, the settings and both preference stores.
local BASE, PLAYER, SETTINGS, OWNER, POOL, DESCS, PREFS, SELECTED = 0x7ff600000000, 0x20000000, 0x21000000, 0x22000000, 0x23000000,
  0x24000000, 0x26000000, 0x27000000
local ANCHORS = {[0xa5cf03] = '488b05d64f2802488b4840f781f0000000fdffffff', [0xa5cff5] = '8bc8498b42500fb60448',
  [0xa5d0b3] = '486bc138420fb68408ec020000'}
local function mem(at, bytes) return {slot = 'mem', key = at, v = bytes} end
local function set(slot, v, at) return {slot = slot, v = v, at = at} end
local function weapon_events(weapon, index, value)
  local descriptor = 0x25000000 + index * 0x100
  return {{slot = 'map', key = ((POOL + 0x30) % 65536) * 4294967296 + weapon, v = index}, mem(DESCS + index * 8, F.le(descriptor, 8)),
    mem(descriptor, 'WEAPONID' .. F.le(weapon, 4) .. 'descrip' .. string.char(index)), mem(PREFS + index * 2, string.char(value))}
end
local function input_events(entity, index, value)
  return {mem(PLAYER + 0xe8, F.le(SELECTED, 8)), mem(SELECTED, 'INPUTDEV' .. F.le(entity, 4) .. 'selecte' .. string.char(index)),
    {slot = 'map', key = ((PLAYER + 0xd0) % 65536) * 4294967296 + entity, v = index}, mem(PLAYER + 0x2ec + index * 0x38, string.char(value))}
end
local function join(...)
  local out = {}
  for _, list in ipairs({...}) do for _, e in ipairs(list) do out[#out + 1] = e end end
  return out
end
local function game(mode)
  local e = {mem(BASE + 0x2ce1618, F.le(PLAYER, 8)), mem(OWNER + 0x40, F.le(SETTINGS, 8)), mem(SETTINGS + 0xf0, F.le(mode, 4)),
    mem(BASE + 0x2ce1ea0, F.le(POOL, 8)), mem(POOL + 0x48, F.le(DESCS, 8)), mem(POOL + 0x50, F.le(PREFS, 8)), mem(PLAYER + 0x88, F.le(1, 4)),
    set('s_ok', true), set('s_base', BASE), set('s_owner', OWNER), set('s_pid', 'player:1'), set('s_id', 'player:1:gun:7'),
    set('s_wd', 'weapon-descriptor-20'), set('s_weapon', 700), set('s_safe', true), set('s_kind', 6),
    set('p_ok', true), set('p_base', BASE), set('p_owner', OWNER), set('p_pid', 'player:1'),
    set('pin_first', false), set('pin_fn', false), set('has_fc', true), set('fc', false), set('copy_ok', true), set('copy_effect', 0),
    set('enabled', true), set('has_foreign', true), set('foreign', false), set('c_enabled', true), set('c_pose', true)}
  for at, bytes in pairs(ANCHORS) do e[#e + 1] = mem(BASE + at, F.unhex(bytes)) end
  return join(e, weapon_events(700, 3, 0), weapon_events(701, 5, 0), input_events(900, 2, 0))
end

-- Hand-written: the life of the option in each aim-memory mode.
local function life(mode, with_foreign)
  F.op('new', {with_foreign})
  F.op('stop', nil, game(mode)); F.op('update'); F.op('update'); F.op('update')
end
local function session(mode, with_foreign)
  local weapon_mode = mode == 1 or mode == 3
  local at = weapon_mode and PREFS + 3 * 2 or PLAYER + 0x2ec + 2 * 0x38
  life(mode, with_foreign)
  F.op('update', nil, {set('enabled', false)})                       -- switched off in the menu: restore
  F.op('update'); F.op('update', nil, {set('enabled', true)})
  F.op('update', nil, {set('c_enabled', false)}); F.op('update', nil, {set('c_enabled', true), set('c_pose', false)})
  F.op('update', nil, {set('c_pose', true)})
  F.op('update', nil, {set('foreign', true)}); F.op('stop'); F.op('update', nil, {set('foreign', false)}); F.op('stop')
  F.op('update'); F.op('update', nil, {mem(at, '\0')})               -- the game cleared it: take it again
  F.op('update', nil, {mem(at, '\2')}); F.op('stop'); F.op('update', nil, {mem(at, '\1')}); F.op('stop')
  F.op('update', nil, {mem(at, '\0')})
  if weapon_mode then
    F.op('update', nil, {set('s_weapon', 701), set('s_id', 'player:1:gun:8')})     -- another weapon: release, take the new one
    F.op('update'); F.op('stop')
    F.op('update', nil, {set('s_weapon', 702)}); F.op('update', nil, {set('s_weapon', 700)})
  else
    F.op('update', nil, input_events(901, 4, 0)); F.op('update'); F.op('stop')
  end
  F.op('update', nil, {set('copy_ok', false), set('copy_why', 'Target is not private heap')}); F.op('update'); F.op('stop')
  F.op('update', nil, {set('copy_why', nil)}); F.op('update', nil, {set('copy_ok', true), set('copy_effect', 1)})
  F.op('update', nil, {set('copy_effect', 2)}); F.op('update', nil, {set('copy_effect', 0)})
  F.op('stop', nil, {set('copy_ok', false), set('copy_why', 'late')}); F.op('update'); F.op('stop', nil, {set('copy_ok', true)})
  F.op('update'); F.op('stop', nil, {set('fc', true)}); F.op('update'); F.op('stop', nil, {set('fc', false)})
  F.op('update'); F.op('stop', nil, {set('has_fc', false)}); F.op('update', nil, {set('has_fc', true)})
  F.op('stop', nil, {set('p_pid', 'player:2')})                      -- another player: the lease retires without a write
  F.op('update', nil, {set('p_pid', 'player:1')}); F.op('stop', nil, {set('p_base', BASE + 4096)}); F.op('update', nil, {set('p_base', BASE)})
  F.op('stop', nil, {set('p_ok', false), set('p_why', 'Player unavailable')}); F.op('stop', nil, {set('p_why', nil)})
  F.op('stop', nil, {set('p_ok', true), set('pin_first', true)}); F.op('update'); F.op('stop', nil, {set('pin_fn', true)})
  F.op('update', nil, {set('pin_first', false)})
  F.op('update', nil, {set('s_ok', false), set('s_why', 'Equip a firearm before starting the camera test')})
  F.op('update', nil, {set('s_why', nil)}); F.op('update', nil, {set('s_ok', true), set('s_safe', false)})
  F.op('update', nil, {set('s_safe', true), set('s_kind', 4)}); F.op('update', nil, {set('s_kind', 5)}); F.op('update', nil, {set('s_kind', 6)})
  F.op('update', nil, {mem(BASE + 0xa5cff5, 'x')}); F.op('update', nil, {mem(BASE + 0xa5cff5, '\139')})
  F.op('update', nil, {mem(SETTINGS + 0xf0, F.le(4, 4))}); F.op('stop'); F.op('update', nil, {mem(SETTINGS + 0xf0, F.le(mode, 4))})
  F.op('update', nil, {mem(SETTINGS + 0xf0, F.le((mode + 1) % 4, 4))}); F.op('update'); F.op('stop')
  F.op('update', nil, {mem(SETTINGS + 0xf0, F.le(mode, 4))})
  -- Same byte and descriptor under another player; same key and byte under another descriptor.
  F.op('update', nil, {set('s_pid', 'player:2'), set('p_pid', 'player:2')}); F.op('update')
  F.op('update', nil, {mem(weapon_mode and 0x25000300 + 14 or SELECTED + 14, 'Z')}); F.op('update'); F.op('stop')
  F.op('shutdown'); F.op('update'); F.op('stop'); F.op('shutdown')
end
for mode = 0, 3 do
  F.run('session_mode' .. mode, function() session(mode, true) end)
end
F.run('session_no_foreign', function() session(1, false) end)

-- Seeded walks: random world changes, some landing in the middle of an operation.
local function walk(seed, count, direct)
  F.seed(seed)
  local mode = F.rnd(4) - 1
  F.op('new', {F.chance(.8)})
  F.op(direct and 'probe' or 'stop', {1}, game(mode))
  local raises = {'x_read', 'x_ptr', 'x_u32', 'x_u8', 'x_map', 'x_copy'}
  local function change()
    local at = F.chance(.4) and F.rnd(F.last_calls + 3) or 0
    local k, e = F.rnd(30)
    if k == 1 then e = set('enabled', F.chance(.5))
    elseif k == 2 then e = set('c_enabled', F.chance(.6))
    elseif k == 3 then e = set('c_pose', F.chance(.6))
    elseif k == 4 then e = set('foreign', F.chance(.3))
    elseif k == 5 then e = set('fc', F.chance(.3))
    elseif k == 6 then e = set('has_fc', F.chance(.8))
    elseif k == 7 then mode = F.rnd(5) - 1; e = mem(SETTINGS + 0xf0, F.le(mode, 4))
    elseif k == 8 then e = set('s_weapon', F.pick({700, 701, 702}))
    elseif k == 9 then e = mem(PREFS + F.pick({3, 5}) * 2, string.char(F.pick({0, 0, 1, 1, 2})))
    elseif k == 10 then e = mem(PLAYER + 0x2ec + F.pick({2, 4}) * 0x38, string.char(F.pick({0, 0, 1, 1, 2})))
    elseif k == 11 then e = set('copy_ok', F.chance(.7))
    elseif k == 12 then e = set('copy_why', F.pick({'Target is not private heap', 'Write refused'}))
    elseif k == 13 then e = set('copy_effect', F.pick({0, 0, 0, 1, 2}))
    elseif k == 14 then e = set('s_ok', F.chance(.8))
    elseif k == 15 then e = set('s_why', F.pick({'Wield ownership changed', 'Memory unavailable'}))
    elseif k == 16 then e = set('s_safe', F.chance(.8))
    elseif k == 17 then e = set('s_kind', F.pick({4, 6, 6, 6, 5}))
    elseif k == 18 then e = set('s_id', F.pick({'player:1:gun:7', 'player:1:gun:8'}))
    elseif k == 19 then e = set('s_wd', F.pick({'weapon-descriptor-20', 'weapon-descriptor-21'}))
    elseif k == 20 then local pid = F.pick({'player:1', 'player:1', 'player:2'}); e = set(F.pick({'s_pid', 'p_pid'}), pid)
    elseif k == 21 then e = set('p_ok', F.chance(.8))
    elseif k == 22 then e = set('p_base', F.pick({BASE, BASE, BASE + 4096}))
    elseif k == 23 then e = set('pin_first', F.chance(.3))
    elseif k == 24 then e = set('pin_fn', F.chance(.5))
    elseif k == 25 then e = set(F.pick(raises), F.pick({'Memory unavailable', 'Invalid read'})); at = F.rnd(F.last_calls + 3)
    elseif k == 26 then e = mem(F.pick({PLAYER + 0x88, SELECTED + 8, 0x25000308, 0x2500030e, SELECTED + 14, DESCS + 24, BASE + 0xa5d0b3}), F.pick({'\0\0\0\0', '\1\0\0\0', 'H'}))
    elseif k == 27 then e = mem(F.pick({PLAYER + 0x88, SELECTED, 0x25000300, PREFS, POOL + 0x50, OWNER + 0x40, BASE + 0x2ce1618}), 4)
    elseif k == 28 then return game(mode)                                      -- everything back in order
    elseif k == 29 then return input_events(F.pick({900, 901}), F.pick({2, 4}), F.pick({0, 1}))
    else return weapon_events(F.pick({700, 701}), F.pick({3, 5}), F.pick({0, 1})) end
    e.at = at
    return {e}
  end
  for _ = 1, count do
    local events = {}
    for _ = 1, F.pick({0, 0, 1, 1, 1, 2, 3}) do events = join(events, change()) end
    if F.chance(.15) then events[#events + 1] = set('x_any', F.pick({'Memory unavailable', 'Invalid read'}), F.rnd(F.last_calls + 2)) end
    if F.chance(.05) then events = join(events, {set('s_pid', 'player:2'), set('p_pid', 'player:2')}) end
    if direct then
      local slot = F.rnd(2)
      local call = F.pick({'probe', 'probe', 'read', 'read', 'write', 'write'})
      if call ~= 'probe' and not records[slot] then call = 'probe' end
      F.op(call, {slot, F.pick({0, 0, 1}), F.pick({0, 1, 1})}, events)
    else
      F.op(F.pick({'update', 'update', 'update', 'update', 'update', 'stop', 'stop', F.chance(.02) and 'shutdown' or 'update'}), nil, events)
    end
  end
end
for seed = 1, 6 do F.run('walk' .. seed, function() walk(seed, 700) end) end
for seed = 11, 14 do F.run('source' .. seed, function() walk(seed, 500, true) end) end
F.finish()

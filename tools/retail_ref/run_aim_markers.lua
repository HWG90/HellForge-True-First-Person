-- Drives the retail aim_markers.lua, unmodified, under LuaJIT (see effects_common.lua).
-- Usage: luajit run_aim_markers.lua <Goose Gadget Kit folder>
-- Three layers, each called directly and through the one above: the HUD adapter (M.Adapter), the
-- owned-weapon source (M.source) and the lease (M.new, built on the two as runtime.lua:724-732 does).
-- Fakes: the backend (module base, guarded memory reads, snapshots, the pose context, _windows with
-- VirtualQuery and the game's own show/hide function), the controller, the camera-ownership and
-- readiness callbacks, the saved settings (mod.config / mod.set) and the logger.
--
-- The region VirtualQuery fills is a real LuaJIT struct (backend.lua's NativeADS_Region) and
-- ffi.sizeof / ffi.cast('const void *') are LuaJIT's; only the cast to the function type is faked
-- (it must not be called for real). The saved per-weapon table lives in the world slot `saved` as
-- a text: entries separated by commas, `key` (value true) or `key=f` (value false); nil = no table.
-- The fake `save` prints and stores the keys it is handed SORTED, because retail builds that table
-- with pairs() and its order is LuaJIT's; the C test sorts in the same way.
package.path = arg[0]:gsub('[^/]*$', '') .. '?.lua;' .. package.path
local F = require('effects_common')
local ffi = require('ffi')
F.slots({'mem', 'set_fx', 'nil_fault',
  'game', 'wine', 'q_ret', 'q_state', 'q_kind', 'q_protect', 'q_alloc', 'set_effect',
  'foreign', 's_ok', 's_safe', 's_kind', 's_id', 's_pid', 's_sa', 's_ca', 's_aim', 's_wobj',
  'pin_first', 'pin_fn', 'has_pinned_ready', 'pinned_ready', 'p_ok', 'p_foreign', 'p_pid', 'p_sa', 'p_ca', 'p_aim',
  'c_enabled', 'c_pose', 'fallback_ready',
  'saved', 'global', 'has_global_fn', 'save_ok', 'save_why', 'has_sg', 'sg_ok', 'sg_why',
  'x_module', 'x_read', 'x_ptr', 'x_u32', 'x_u8', 'x_windows', 'x_set_shown', 'x_setter', 'x_pose_context', 'x_save_global', 'x_any'})
local function g(name) return F.world[name] end
local num, show = F.num, F.show

ffi.cdef([[typedef struct NativeADS_Region {
  uint64_t base, allocation; uint32_t allocation_protect;
  uint16_t partition, reserved; uint64_t size;
  uint32_t state, protect, kind, padding;
} NativeADS_Region;]])

-- The backend. Memory reads raise as backend.lua's do; the texts are the fake's own.
local function read(name, address, size)
  F.tick(name, num(address) .. (name == 'read' and ' ' .. num(size) or ''))
  local bytes = F.read(address, size)
  if not bytes then
    if g('nil_fault') then error(nil, 0) end        -- an error value that is not a text
    error('Memory unavailable', 0)
  end
  return bytes
end
local function uint(b, o) o = o or 0; local a, b2, c, d = b:byte(o + 1, o + 4); return a + b2 * 256 + c * 65536 + d * 16777216 end
-- What the game's show/hide function (or the test setter) does to the widget's flag word.
local function set_shown(widget, value)
  local effect = F.getk('set_fx', widget) or g('set_effect') or 0
  local word = F.read(widget, 4)
  if word and effect ~= 1 then
    local now = uint(word)
    local bit16 = math.floor(now / 16) % 2
    local want = (value and 1 or 0)
    if effect == 3 then want = 1 - want end
    F.poke('mem', widget, F.le(now + (want - bit16) * 16, 4))
  end
  if effect == 2 then error('failed after write', 0) end
end
local function backend()
  local b = {}
  local region = ffi.new('NativeADS_Region')
  local fake_ffi = {sizeof = ffi.sizeof, cast = function(kind, value)
    if kind == 'const void *' then return ffi.cast(kind, value) end
    assert(kind == 'void (*)(uintptr_t,uint8_t)', kind)
    return function(widget, shown)
      F.tick('set_shown', num(value) .. ' ' .. num(widget) .. ' ' .. num(shown))
      set_shown(widget, shown == 1)
    end
  end}
  local kernel = {NativeADS_Query = function(pointer, r, size)
    F.tick('virtual_query', num(tonumber(ffi.cast('uintptr_t', pointer))) .. ' ' .. num(size))
    local ret = g('q_ret')
    if ret == nil then ret = size end
    if ret ~= 0 then
      r.state, r.kind, r.protect, r.allocation = g('q_state') or 0, g('q_kind') or 0, g('q_protect') or 0, g('q_alloc') or 0
    end
    return ret
  end}
  function b:_module(scope) F.tick('module', show(scope)); return g('game') end
  function b:_read(address, size) return read('read', address, size) end
  function b:_ptr(address)
    local v = read('ptr', address, 8)
    local value = uint(v) + uint(v, 4) * 4294967296
    if value < 65536 then error('Pointer unavailable', 0) end
    return value
  end
  function b:_u32(address) return uint(read('u32', address, 4)) end
  function b:_u8(address) return read('u8', address, 1):byte() end
  function b:_windows()
    F.tick('windows')
    return {ffi = fake_ffi, k = kernel, region = region, wine = g('wine') == true}
  end
  function b:snapshot()
    F.tick('snapshot')
    if g('s_ok') ~= true then return nil, 'Equip a firearm before starting the camera test' end
    return {safe = g('s_safe'), camera_kind = g('s_kind'), identity = g('s_id'), player_identity = g('s_pid'),
      shoulder_aim = g('s_sa'), controller_active = g('s_ca'), aiming = g('s_aim')}
  end
  function b:_pose_context(s)
    F.tick('pose_context', show(s.identity))
    return {weapon_object = g('s_wobj')}
  end
  return b
end
local function pin_player_snapshot()
  F.tick('pin_player_snapshot')
  if g('p_ok') ~= true then return nil, 'Player unavailable' end
  return {foreign_camera = g('p_foreign'), player_identity = g('p_pid'), shoulder_aim = g('p_sa'), controller_active = g('p_ca'),
    aiming = g('p_aim')}
end
local function decode_saved()
  local text = g('saved')
  if text == nil then return nil end
  local out = {}
  for entry in text:gmatch('[^,]+') do
    local key, off = entry:match('^(.-)(=f)$')
    if key then out[key] = false else out[entry] = true end
  end
  return out
end

local M = F.load('aim_markers.lua')
local controller = {}
function controller:is_enabled() F.tick('is_enabled'); return g('c_enabled') end
function controller:result() F.tick('result'); return {mode = g('c_pose') and 'pose_offset' or 'other'} end

local b, adapter, source_options, source_fn, lease_options, lease
local huds = {}
local function upvalue(fn, wanted)
  for i = 1, 60 do
    local name, value = debug.getupvalue(fn, i)
    if not name then return nil end
    if name == wanted then return value end
  end
end
local function fresh(use_setter, no_log)
  b = backend(); huds = {}
  local lease_adapter = use_setter and M.Adapter.new(b, function(widget, value)
    F.tick('setter', num(widget) .. ' ' .. show(value))
    set_shown(widget, value)
  end) or nil
  adapter = lease_adapter or M.Adapter.new(b)
  source_options = {foreign = function() F.tick('foreign'); return g('foreign') end,
    fallback_ready = function() F.tick('fallback_ready'); return g('fallback_ready') end,
    global_setting = function() error('the source does not read the setting') end,
    -- Fird Person is parked (runtime.lua: `fird` stays nil): the C makes no such call, so no tick.
    fird = function() return nil end}
  source_fn = M.source(b, controller, source_options)
  lease_options = {
    settings = function() F.tick('settings'); return decode_saved() end,
    save = function(values)
      local keys = {}
      for key in pairs(values) do keys[#keys + 1] = key end
      table.sort(keys)
      local text = table.concat(keys, ',')
      F.tick('save', text)
      if g('save_ok') ~= true then return false, g('save_why') end
      F.poke('saved', nil, text)
      return true
    end,
    current = source_fn}
  lease = M.new(b, not no_log and F.log or nil, lease_options, lease_adapter)
end
-- Fields the module tests at call time follow the world.
local function sync()
  b.pin_first_person = g('pin_first')
  b.pin_player_snapshot = g('pin_fn') and function() return pin_player_snapshot() end or nil
  source_options.pinned_ready = g('has_pinned_ready') and function(s)
    F.tick('pinned_ready', show(s.player_identity)); return g('pinned_ready')
  end or nil
  lease_options.global_setting = g('has_global_fn') and function() F.tick('global_setting'); return g('global') end or nil
  lease_options.save_global = g('has_sg') and function(value)
    F.tick('save_global', show(value))
    if g('sg_ok') == 0 then error(nil, 0) end
    if g('sg_ok') ~= true then return false, g('sg_why') end
    F.poke('global', nil, value)
    return true
  end or nil
end
local function hudtext(h)
  if not h then return '~' end
  return table.concat({num(h.game), num(h.root), num(h.state), num(h.parent), num(h.context), num(h.world), num(h.widgets[1]),
    num(h.widgets[2])}, ' ')
end
local function state()
  local l, text = lease.lease, '~'
  if l then
    local rows = {}
    for i, e in ipairs(l.rows) do
      assert(l.by_widget[e.widget] == e)
      rows[i] = num(e.widget) .. ':' .. show(e.before) .. ':' .. show(e.done)
    end
    text = hudtext(l.hud) .. ' ' .. show(l.weapon) .. ' ' .. show(l.player) .. ' ' .. show(l.releasing) .. ' [' .. table.concat(rows, ' ') .. ']'
  end
  local r = lease:result()
  return table.concat({show(lease.closing), show(lease.fault), '|', text, '|', show(r.hidden), show(r.error), show(r.cleanup_pending),
    show(lease:is_clean()), '|', show(upvalue(source_fn, 'cached_identity')), show(upvalue(source_fn, 'cached_key')), '|',
    hudtext(huds[1]), '|', hudtext(huds[2])}, ' ')
end
function F.dispatch(call, a)
  if call == 'new' then fresh(a[1], a[2]) end        -- a[1]: the adapter gets a test setter; a[2]: no logger
  sync()
  local out
  if call == 'new' then out = 'new'
  elseif call == 'update' or call == 'stop' or call == 'shutdown' then
    local r1, r2 = lease[call](lease)
    out = show(r1) .. ' ' .. show(r2)
  elseif call == 'set_setting' then
    local r1, r2 = lease:set_setting(a[1])
    out = show(r1) .. ' ' .. show(r2)
  elseif call == 'toggle' then
    local r1, r2 = lease:toggle(a[1], a[2])
    out = show(r1) .. ' ' .. show(r2)
  elseif call == 'setting' then out = show(lease:setting())
  elseif call == 'target' then
    local ok, t = pcall(lease.target, lease)
    if not ok then out = 'raised ' .. show(t)
    elseif t then out = 'target ' .. show(t.key) .. ' ' .. show(t.identity) .. ' ' .. show(t.allowed)
    else out = 'none' end
  elseif call == 'current' then                      -- a[1]: fresh
    local ok, t = pcall(source_fn, a[1])
    if not ok then out = 'raised ' .. show(t)
    elseif t then
      out = table.concat({'target', show(t.view_only), show(t.key), show(t.identity), show(t.player_identity), show(t.aiming)}, ' ')
    else out = 'none' end
  elseif call == 'probe' then                        -- a[1]: mission only; a[2]: the slot that receives the HUD
    local ok, r = pcall(adapter.probe, adapter, a[1])
    if ok then huds[a[2]] = r; out = r and 'hud' or 'none' else out = 'raised ' .. show(r) end
  elseif call == 'show' then                         -- a[1]: slot, a[2]: widget address, a[3]: shown
    local ok, why = pcall(adapter.show, adapter, huds[a[1]], a[2], a[3])
    out = ok and 'ok' or 'raised ' .. show(why)
  elseif call == 'visible' then
    local ok, r = pcall(adapter.visible, adapter, a[1])
    out = ok and show(r) or 'raised ' .. show(r)
  elseif call == 'same' then                         -- retail answers nil / false / true; its callers only test it
    out = show(not not adapter:same(huds[a[1]], huds[a[2]]))
  else error(call) end
  return out .. ' | ' .. state()
end

-- A consistent game: the four code fingerprints, the HUD of two roots, the weapon key.
local GAME, GAME2 = 0x7ff600000000, 0x7ff700000000
local ROOT, ROOT2, STATE, STATE2, CONTEXT, CONTEXT2, WORLD, WORLD2 = 0x20000000, 0x21000000, 0x30000000, 0x31000000, 0x40000000, 0x41000000,
  0x50000000, 0x51000000
local WOBJ, WOBJ2 = 0x60000000, 0x61000000
local A, B, ZERO = '0011223344556677', '8899aabbccddeeff', '0000000000000000'
local CHECKS = {{0x12e4e20, '4883ec28448b014c8bd1418bc0458bc883e0ef4183c91084d2440f44c8448909'}, {0x1635ccf, '4d8da510010000'},
  {0x1635d61, '498db520020000'}, {0x117c88d, '498d8f78a61900e887934b00498d9778a61900'}}
local SETTER = 0x12e4e20
local function mem(at, bytes) return {slot = 'mem', key = at, v = bytes} end
local function set(slot, v, at) return {slot = slot, v = v, at = at} end
local function join(...)
  local out = {}
  for _, list in ipairs({...}) do for _, e in ipairs(list) do out[#out + 1] = e end end
  return out
end
local function cross(root) return root + 0x24e340 + 0x19a678 end
local function widget(root, i) return cross(root) + (i == 1 and 0x110 or 0x220) end
local function code(game)
  local e = {}
  for _, check in ipairs(CHECKS) do e[#e + 1] = mem(game + check[1], F.unhex(check[2])) end
  return e
end
local function hud(root, context, world)
  local c = cross(root)
  local e = {mem(root + 0x24e334, '\1'), mem(c + 0xf0, F.le(root + 0x820, 8)), mem(c + 0xf8, F.le(context, 8)), mem(context, '\1'),
    mem(context + 0x10, F.le(world, 8))}
  for i = 1, 2 do
    local w = widget(root, i)
    e = join(e, {mem(w + 0xf0, F.le(c, 8)), mem(w + 0xf8, F.le(context, 8)), mem(w, F.le(0x1010, 4))})
  end
  return e
end
local function point(game, root, state) return {mem(game + 0x2ce1508, F.le(state, 8)), mem(game + 0x2e28788, F.le(root, 8))} end
local function key_at(object, key) return mem(object + 0x358, F.unhex(key)) end
local function settings_events()
  return {set('saved', ''), set('global', nil), set('has_global_fn', true), set('save_ok', true), set('save_why', nil), set('has_sg', true),
    set('sg_ok', true), set('sg_why', nil)}
end
local function owner_events()
  return {set('foreign', false), set('s_ok', true), set('s_safe', true), set('s_kind', 6), set('s_id', 'player:1:gun:7'),
    set('s_pid', 'player:1'), set('s_sa', false), set('s_ca', false), set('s_aim', false), set('s_wobj', WOBJ),
    set('pin_first', false), set('pin_fn', false), set('has_pinned_ready', true), set('pinned_ready', false), set('p_ok', true),
    set('p_foreign', false), set('p_pid', 'player:1'), set('p_sa', false), set('p_ca', false), set('p_aim', false),
    set('c_enabled', true), set('c_pose', true), set('fallback_ready', true), key_at(WOBJ, A), key_at(WOBJ2, B)}
end
local function native_events()
  return {set('game', GAME), set('wine', false), set('q_ret', 48), set('q_state', 0x1000), set('q_kind', 0x1000000), set('q_protect', 0x20),
    set('q_alloc', GAME), set('set_effect', 0), set('nil_fault', false), {slot = 'set_fx', key = widget(ROOT, 1), v = nil}, {slot = 'set_fx', key = widget(ROOT, 2), v = nil}}
end
-- Everything in order, without touching the saved settings.
local function order()
  return join(code(GAME), code(GAME2), point(GAME, ROOT, STATE), point(GAME2, ROOT, STATE), {mem(STATE + 0xac29c, F.le(4, 4)),
    mem(STATE2 + 0xac29c, F.le(4, 4))}, hud(ROOT, CONTEXT, WORLD), hud(ROOT2, CONTEXT2, WORLD2), native_events(), owner_events())
end
local function game() return join(order(), settings_events()) end
local W1, W2 = widget(ROOT, 1), widget(ROOT, 2)
local SHOWN, HIDDEN = F.le(0x1010, 4), F.le(0x1000, 4)
local function op(call, args, events) return F.op(call, args, events) end
local function up(events) return F.op('update', nil, events) end
local function start(use_setter, no_log) F.op('new', {use_setter, no_log}); F.op('stop', nil, game()) end
local function weapon_b() return {set('s_id', 'player:1:gun:8'), set('s_wobj', WOBJ2)} end

-- Hand-written. The situations of retail's own tests/first_person/aim_markers.lua on the whole stack.
local function hand(use_setter)
  local name = use_setter and '_setter' or ''
  local raise_set = use_setter and 'x_setter' or 'x_set_shown'
  F.run('aim_and_hip' .. name, function()
    start(use_setter)
    up(); op('setting'); op('target')
    up({set('s_aim', true)}); up(); up(); op('setting'); op('target')
    up({mem(W1, SHOWN)})                                         -- the game showed one again
    up({set('s_aim', false)}); up()
    up({set('s_sa', true)}); up({set('s_sa', false), set('s_ca', true)}); up({set('s_ca', false)})
    up({set('s_aim', 1)})                                        -- only the value true is aiming
    up({set('s_aim', true), mem(W2, HIDDEN)}); up({set('s_aim', false)})      -- hidden by the game: stays hidden
    up({set('s_aim', true), mem(W1, HIDDEN)}); op('stop'); op('stop')         -- nothing to hide: an empty lease
    up({mem(W1, SHOWN), mem(W2, SHOWN)}); op('shutdown'); up(); op('stop'); op('shutdown')
    op('toggle', {A, 'player:1:gun:7'}); op('set_setting', {true}); op('target'); op('setting')
  end)
  F.run('per_weapon' .. name, function()
    start(use_setter)
    up({set('s_aim', true)})
    op('toggle', {A, 'player:1:gun:7'}); op('target'); op('setting'); up()
    up(weapon_b()); op('target'); op('setting')
    op('toggle', {B, 'player:1:gun:8'}); op('toggle', {B, 'player:1:gun:8'}); op('toggle', {B, 'player:1:gun:8'})
    up({set('s_id', 'player:2:gun:1'), set('s_pid', 'player:2'), set('s_wobj', WOBJ)})
    op('toggle', {A, 'player:2:gun:1'}); up()                    -- A switched off again: hide
    up({set('s_id', 'player:2:gun:2')}); up({set('s_pid', 'player:3')})       -- another weapon / player: a new lease
    op('toggle', {A, 'player:1:gun:7'}); op('toggle', {B, 'player:2:gun:2'}); op('toggle', {'junk', 'player:2:gun:2'})
    op('toggle', {A, 'player:2:gun:2'}, {set('save_ok', false), set('save_why', 'save failed')})
    op('toggle', {A, 'player:2:gun:2'}, {set('save_why', nil)})
    op('toggle', {A, 'player:2:gun:2'}, {set('save_ok', true), set('saved', nil)})
    op('toggle', {A, 'player:2:gun:2'}, {set('saved', 'extra,' .. B .. ',' .. ZERO .. ',0011223344556677a,ABCDEF0011223344,8899aabbccddeef=f,' .. A .. '=f')})
    op('toggle', {A, 'player:2:gun:2'}); op('toggle', {A, 'player:2:gun:2'}, {set('saved', nil)})
    -- A host list may repeat a key (a Lua table cannot); the choice may arrive between the two reads of the click.
    op('toggle', {A, 'player:2:gun:2'}, {set('saved', B .. ',' .. B .. ',' .. A .. ',' .. A)})
    op('toggle', {A, 'player:2:gun:2'}, {set('saved', B .. ',1122334455667788,1122334455667788')})
    op('toggle', {A, 'player:2:gun:2'}, {set('saved', A .. ',' .. B), set('global', false, 7)})
    op('toggle', {A, 'player:2:gun:2'}, {set('saved', B .. ',' .. A), set('global', nil), set('global', false, 7)})
    op('toggle', {A, 'player:2:gun:2'}, {set('saved', B .. ',' .. A), set('global', nil), set('global', true, 7)})
    op('toggle', {A, 'player:2:gun:2'}, {set('saved', ''), set('global', nil)})
    -- Keys that are almost valid: one character outside 0-9a-f at either end or inside; zeros but for one digit.
    op('toggle', {B, 'player:2:gun:2'}, {key_at(WOBJ, B), set('saved', 'x011223344556677,001122334455667g,00112233445566:7,0011223344556/77,00112233`4556677,' ..
      'G011223344556677,1000000000000000,0000000000000001,0000000100000000,' .. A)})
    for _, odd in ipairs({'1000000000000000', '0000000000000001', ZERO}) do
      op('target', nil, {key_at(WOBJ, odd), set('s_id', 'player:2:gun:' .. odd)}); op('toggle', {odd, 'player:2:gun:' .. odd}); op('setting')
    end
    up({key_at(WOBJ, A), set('s_id', 'player:2:gun:2')})
    -- The C holds at most TFP_AIM_MARKERS_SAVED (64) entries: 63 and one more come back; a 65th is still
    -- handed to save (here refused, because the C host could not hand 65 back).
    local full = {}
    for i = 1, 64 do full[i] = string.format('%016x', i * 0x1111) end
    op('toggle', {A, 'player:2:gun:2'}, {set('saved', table.concat(full, ',')), set('save_ok', false)})
    op('toggle', {A, 'player:2:gun:2'}, {set('saved', table.concat(full, ',', 1, 63)), set('save_ok', true)}); op('target')
    op('toggle', {A, 'player:2:gun:2'}); op('target'); op('toggle', {A, 'player:2:gun:2'}, {set('saved', '')})
    up({set('x_read', string.rep('A long text from the host. ', 9), 3), set('s_id', 'player:2:gun:3')}); up({set('s_id', 'player:2:gun:2')})
    up({set('saved', A .. '=f')}); up({set('saved', nil)}); op('setting'); op('target')
    op('toggle', {A, 'player:2:gun:2'}, {key_at(WOBJ, B)})        -- a fresh read: the weapon behind the click changed
    op('toggle', {B, 'player:2:gun:2'}, {set('s_ok', false)}); op('toggle', {B, 'player:2:gun:2'}, {set('s_ok', true), set('x_read', 'Invalid read')})
    op('toggle', {B, 'player:2:gun:2'}, {set('global', true)}); op('toggle', {B, 'player:2:gun:2'}, {set('global', false)})
    op('toggle', {B, 'player:2:gun:2'}, {set('global', 0)})       -- not a Boolean: unset
    op('toggle', {B, 'player:2:gun:2'}, {set('global', nil), set('global', true, 4)})    -- chosen during the click
    op('toggle', {B, 'player:2:gun:2'}, {set('global', nil), set('global', false, 4)})
    op('shutdown')
  end)
  F.run('owner_gaps' .. name, function()
    start(use_setter)
    for _, gap in ipairs({{set('s_ok', false)}, {set('foreign', true)}, {set('s_safe', false)}, {set('s_kind', 5)}, {set('s_kind', nil)},
        {set('s_kind', 4), set('c_enabled', false)}, {set('s_kind', 4), set('c_pose', false)}, {set('s_kind', 4), set('fallback_ready', false)},
        {set('s_id', nil)}, {set('s_pid', nil)}, {key_at(WOBJ, ZERO), set('s_id', 'player:1:gun:9')}, {set('x_pose_context', 'Weapon component descriptor changed')},
        {mem(WOBJ + 0x358, 8), set('s_id', 'player:1:gun:9')}}) do
      up(join(order(), {set('s_aim', true)})); op('target'); op('setting')
      up(gap); op('target'); op('setting'); op('current', {false}); op('current', {true}); up(); op('stop')
    end
    up(join(order(), {set('s_aim', true), set('s_kind', 4)})); op('target'); up({set('s_kind', 6)}); op('shutdown')
  end)
  F.run('hud_changes' .. name, function()
    start(use_setter)
    up({set('s_aim', true)})
    up(join(point(GAME, ROOT2, STATE), {set('s_aim', false)}))    -- replaced HUD: never restored with the old state
    up(join(point(GAME, ROOT, STATE), {set('s_aim', true)}))      -- (the old one stays hidden: retail forgot it)
    up({mem(W1, SHOWN), mem(W2, SHOWN)}); up(point(GAME, ROOT2, STATE)); up(point(GAME, ROOT, STATE))
    up({mem(STATE + 0xac29c, F.le(3, 4))}); up({mem(STATE + 0xac29c, F.le(4, 4))})      -- outside a mission
    op('stop', nil, {mem(ROOT + 0x24e334, '\0')}); up(); up({mem(ROOT + 0x24e334, '\1')})  -- retired HUD
    up({mem(CONTEXT, '\0')}); up({mem(CONTEXT, '\1')})
    up(point(GAME, ROOT, STATE2)); up({mem(CONTEXT + 0x10, F.le(WORLD2, 8))}); up({set('game', GAME2)})
    up({set('game', GAME), mem(CONTEXT + 0x10, F.le(WORLD, 8))})
    up({mem(cross(ROOT) + 0xf0, F.le(ROOT + 0x828, 8))}); up(order()); up({set('s_aim', true)})
    up({mem(W2 + 0xf0, F.le(ROOT, 8))}); up({mem(W2 + 0xf0, F.le(cross(ROOT), 8)), mem(W1 + 0xf8, F.le(CONTEXT2, 8))})
    up({mem(W1 + 0xf8, F.le(CONTEXT, 8)), mem(W1, F.le(0, 4))}); up({mem(W1, SHOWN)})
    up({mem(GAME + 0x1635ccf, 'M')}); up({mem(GAME + 0x1635ccf, 'L')}); up({mem(GAME + 0x1635ccf, 'M')})
    for _, x in ipairs({'x_module', 'x_read', 'x_ptr', 'x_u32', 'x_u8', 'x_windows', raise_set, 'x_any'}) do
      up({set(x, 'Invalid read')}); up(); up({set('s_aim', false), set(x, 'Memory unavailable')}); up({set('s_aim', true)})
    end
    -- An error value that is not a text (tostring gives "nil").
    up({mem(W1, 4), set('nil_fault', true)}); up(); op('target', nil, {set('s_id', 'player:1:gun:9'), mem(WOBJ + 0x358, 8)})
    op('toggle', {A, 'player:1:gun:9'}); op('setting'); op('current', {false}); op('probe', {false, 1}); op('visible', {W1})
    up(order()); up({set('s_aim', true)}); up({set('s_aim', false), set('nil_fault', true), mem(W2, 4)}); up(); up(order())
    op('shutdown', nil, {set('x_u32', 'late')}); op('shutdown')
  end)
  F.run('writes' .. name, function()
    start(use_setter)
    up({set('s_aim', true), set('set_effect', 2)})                 -- fails after writing: owned before, restored
    up({set('set_effect', 0)}); up({set('s_aim', false), set('set_effect', 1)})         -- restoration changes nothing
    up(); op('stop'); up({set('s_aim', true)}); up({set('set_effect', 0)}); up()
    op('shutdown', nil, {set('set_effect', 1)}); op('shutdown'); op('shutdown', nil, {set('set_effect', 0)})
    start(use_setter)
    up({set('s_aim', true)}); up({set('s_aim', false), {slot = 'set_fx', key = W2, v = 1}})   -- one of two restored
    up({set('s_aim', true)}); up(); up({{slot = 'set_fx', key = W2, v = nil}}); up({set('s_aim', false)})
    up({set('s_aim', true), {slot = 'set_fx', key = W2, v = 2}}); up({{slot = 'set_fx', key = W2, v = 0}})
    up({set('s_aim', false), set('set_effect', 3)}); up({set('set_effect', 0)})
    up({set('s_aim', true), set('set_effect', 3)}); up({set('set_effect', 0)}); up({set('s_aim', false)})
    up({set('s_aim', true), set('set_effect', 1)}); up({set('set_effect', 0)}); up({set('s_aim', false)})
    -- The read after the native call fails: of the second group while hiding, then while restoring.
    up({set('s_aim', true)}); local hide = F.last_calls
    up({set('s_aim', false)}); local restore = F.last_calls
    up({set('s_aim', true), set('x_u32', 'late', hide)}); up(); up({set('s_aim', false), set('x_u32', 'late', restore)}); up()
    up({set('s_aim', true), mem(W2, 4, hide)}); up(order()); up({set('s_aim', true)}); up({set('s_aim', false), mem(W2, 4, restore)}); up(order())
    if not use_setter then
      for _, q in ipairs({{set('q_ret', 0)}, {set('q_ret', 47)}, {set('q_ret', nil)}, {set('q_state', 0x2000)}, {set('q_kind', 0x40000)},
          {set('q_kind', 0x40000), set('wine', true)}, {set('q_kind', 0x40000), set('wine', true), set('q_alloc', GAME + 0x1000)},
          {set('q_kind', 0x20000), set('wine', true)}, {set('q_protect', 0x40)}, {set('q_protect', 0x80)}, {set('q_protect', 0x04)},
          {set('q_protect', 0x10)}}) do
        up(join(order(), {set('s_aim', true)}, q)); up({set('s_aim', false)}); up(order())
      end
    end
    op('shutdown')
  end)
  F.run('global' .. name, function()
    start(use_setter)
    op('setting', nil, {set('saved', A .. ',extra'), set('s_ok', false)}); op('set_setting', {true}); op('setting')
    op('set_setting', {false}); op('setting'); up({set('s_ok', true)})
    up({set('s_aim', true)}); up(join(weapon_b(), {set('s_aim', false)})); up({set('s_aim', true)}); op('setting'); op('target')
    op('toggle', {B, 'player:1:gun:8'})
    op('set_setting', {true}); op('target'); up(); op('set_setting', {false})
    up({mem(STATE + 0xac29c, F.le(3, 4))})                         -- the explicit hide also covers the ship HUD
    op('set_setting', {true}); up({set('global', nil)}); up({set('global', false)}); up({set('global', nil)})
    up({mem(STATE + 0xac29c, F.le(4, 4))}); up({set('global', true)}); up({set('global', nil)}); up({set('global', 'no')})
    op('set_setting', {false}, {set('sg_ok', false), set('sg_why', 'global save failed')}); op('set_setting', {false}, {set('sg_why', nil)})
    op('set_setting', {false}, {set('sg_ok', true), set('x_save_global', 'global save error')})
    op('set_setting', {false}, {set('sg_ok', 0)}); op('set_setting', {false}, {set('sg_ok', 0), set('sg_why', 'unused')}); op('setting', nil, {set('sg_ok', true)})
    op('set_setting', {false}, {set('has_sg', false)}); op('set_setting', {false}, {set('has_sg', true), set('set_effect', 1)})
    op('set_setting', {true}); op('setting'); up({set('set_effect', 0)})
    op('set_setting', {false}); op('set_setting', {true}, {set('set_effect', 1)}); op('setting'); up({set('set_effect', 0)})
    up({set('has_global_fn', false), set('saved', '')}); op('setting'); op('target'); op('toggle', {B, 'player:1:gun:8'}); up()
    op('setting', nil, {set('x_read', 'Invalid read'), set('s_id', 'player:1:gun:9')})
    op('target', nil, {set('x_read', 'Invalid read'), set('s_id', 'player:1:gun:7')}); op('target')
    op('setting', nil, {set('has_global_fn', true), set('global', nil), set('saved', nil)}); op('setting', nil, {set('saved', B)})
    op('setting', nil, {set('s_aim', false)}); op('shutdown'); op('set_setting', {true}); op('setting'); op('target')
  end)
  F.run('pinned' .. name, function()
    start(use_setter)
    up({set('pin_first', true), set('pin_fn', true), set('pinned_ready', true), set('s_ok', false), set('p_aim', true)})
    op('target'); op('current', {false}); op('current', {true}); op('setting')
    op('set_setting', {false}); op('target'); op('current', {false}); up(); op('toggle', {A, 'player:1:pinned-view'})
    up({mem(W2, SHOWN)}); up({set('p_aim', false)}); up({set('p_sa', true)}); up({set('p_sa', false), set('p_ca', true)})
    up({set('p_ca', false)}); up({set('p_aim', true), mem(STATE + 0xac29c, F.le(3, 4))})
    for _, gap in ipairs({{set('pin_first', false)}, {set('pin_fn', false)}, {set('has_pinned_ready', false)}, {set('c_enabled', false)},
        {set('c_pose', false)}, {set('p_ok', false)}, {set('p_foreign', true)}, {set('p_pid', nil)}, {set('p_pid', '')},
        {set('pinned_ready', false)}, {set('pinned_ready', 1)}, {set('global', true)}, {set('global', nil)}, {set('has_global_fn', false)},
        {set('foreign', true)}}) do
      up(gap); op('current', {false}); op('target')
      up(join(order(), {set('pin_first', true), set('pin_fn', true), set('pinned_ready', true), set('s_ok', false), set('p_aim', true),
        set('has_global_fn', true), set('global', false)}))
    end
    up({set('p_pid', 'player:2')}); up({set('s_ok', true), set('s_safe', false)}); up({set('s_safe', true), set('s_kind', 5)})
    up({set('s_kind', 6), set('x_read', 'Invalid read')}); op('current', {false}, {set('x_read', 'Invalid read')})
    op('current', {true}, {set('x_read', 'Invalid read')}); op('current', {false}); op('current', {false}, {set('s_id', nil)})
    op('current', {false}); op('current', {true}); op('target'); up()
    -- The pinned owner is accepted while the choice is "hide" and the choice is unset when update reads it.
    -- (host calls: foreign, snapshot, is_enabled, result, pin_player_snapshot, pinned_ready, the source's reader of the choice, update's)
    up({set('s_ok', false), set('global', false), set('global', nil, 8)}); up({set('global', false)}); up({set('saved', nil), set('global', nil, 8)})
    up({set('saved', 'x,' .. A), set('global', false)}); up({set('p_aim', false)})
    up({set('p_aim', true), set('global', nil, 8)}); up({set('global', false)}); op('setting', nil, {set('global', nil), set('global', false, 8)})
    up({set('global', false)}); up({set('global', true, 8)}); up({set('global', false)})
    op('set_setting', {true}); op('shutdown')
  end)
end
hand(false); hand(true)
F.run('no_logger', function()
  start(false, true)
  up({set('s_aim', true), set('x_u32', 'Invalid read')}); up(); up({set('s_aim', false), set('set_effect', 1)}); op('shutdown', nil, {set('set_effect', 0)})
end)
-- The adapter alone, in the order of retail's own adapter test and beyond.
local function adapter_hand(use_setter)
  F.run('adapter' .. (use_setter and '_setter' or ''), function()
    local C = cross(ROOT)
    start(use_setter)
    op('probe', {true, 1}); op('probe', {true, 2}, {mem(STATE + 0xac29c, F.le(3, 4))}); op('probe', {false, 2}); op('same', {1, 2})
    op('same', {1, 3}); op('same', {3, 1}); op('same', {3, 3})
    op('probe', {true, 1}, {mem(STATE + 0xac29c, F.le(4, 4))})
    op('visible', {W1}); op('show', {1, W1, false}); op('visible', {W1}); op('visible', {W2}); op('show', {1, W1, false})
    op('show', {1, W1, true}); op('show', {1, W2, false}); op('show', {1, W2, true}); op('show', {1, C, false}); op('show', {1, W1 + 1, false})
    op('visible', {W1}, {mem(W1, F.le(0x10, 4))}); op('visible', {W1}, {mem(W1, F.le(0xffffffef, 4))}); op('visible', {W1}, {mem(W1, F.le(0x30, 4))})
    op('visible', {W1}, {mem(W1, 4)}); op('visible', {W1}, {mem(W1, SHOWN)})
    op('show', {1, W1, true}, {mem(W2 + 0xf0, F.le(ROOT, 8))}); op('show', {1, W1, false}, {mem(W2 + 0xf0, F.le(C, 8)), mem(ROOT + 0x24e334, '\0')})
    op('show', {1, W1, false}, {mem(ROOT + 0x24e334, '\1'), mem(STATE + 0xac29c, F.le(3, 4))})    -- show probes outside missions too
    for _, other in ipairs({point(GAME, ROOT2, STATE), point(GAME, ROOT, STATE2), {mem(CONTEXT + 0x10, F.le(WORLD2, 8))}, {set('game', GAME2)},
        {mem(C + 0xf8, F.le(CONTEXT2, 8)), mem(W1 + 0xf8, F.le(CONTEXT2, 8)), mem(W2 + 0xf8, F.le(CONTEXT2, 8))}}) do
      op('probe', {false, 2}, other); op('same', {1, 2}); op('same', {2, 1}); op('show', {1, W1, true}); op('show', {2, W1, true})
      op('probe', {false, 2}, order()); op('same', {1, 2})
    end
    for i, check in ipairs(CHECKS) do
      op('probe', {true, 2}, {mem(GAME + check[1] + #check[2] / 2 - 1, 'x')}); op('probe', {true, 2}, {mem(GAME + check[1], 1)})
      op('show', {1, W1, false}); op('probe', {true, 2}, code(GAME))
    end
    for _, broken in ipairs({{mem(GAME + 0x2ce1508, 8)}, {mem(GAME + 0x2ce1508, F.le(1, 8))}, {mem(STATE + 0xac29c, 4)}, {mem(GAME + 0x2e28788, 8)},
        {mem(ROOT + 0x24e334, 1)}, {mem(ROOT + 0x24e334, '\2')}, {mem(C + 0xf0, 8)}, {mem(C + 0xf0, F.le(ROOT + 0x821, 8))}, {mem(C + 0xf8, 8)},
        {mem(CONTEXT, 1)}, {mem(CONTEXT, '\2')}, {mem(CONTEXT + 0x10, 8)}, {mem(W1 + 0xf0, 8)}, {mem(W1 + 0xf0, F.le(C + 8, 8))},
        {mem(W1 + 0xf8, 8)}, {mem(W1 + 0xf8, F.le(CONTEXT2, 8))}, {mem(W1, 4)}, {mem(W1, F.le(0, 4))}, {mem(W2 + 0xf0, F.le(C + 8, 8))},
        {mem(W2 + 0xf8, F.le(CONTEXT2, 8))}, {mem(W2, F.le(0, 4))}, {mem(W2, 4)}, {set('x_module', 'game.dll unavailable')}}) do
      op('probe', {true, 2}, broken); op('probe', {false, 2}); op('show', {1, W2, false}); op('probe', {false, 2}, order())
    end
    for _, effect in ipairs({1, 2, 3}) do
      op('show', {1, W1, false}, {set('set_effect', effect)}); op('show', {1, W1, true}); op('show', {1, W1, true}, {set('set_effect', 0)})
    end
    op('show', {1, W1, false}, {set(use_setter and 'x_setter' or 'x_set_shown', 'native fault')}); op('show', {1, W1, false}, {set('x_windows', 'Windows x64 required')})
    for _, q in ipairs({{set('q_ret', 0)}, {set('q_ret', 49)}, {set('q_state', 0x10000)}, {set('q_kind', 0x40000)}, {set('q_kind', 0x40000), set('wine', true)},
        {set('q_kind', 0x40000), set('wine', true), set('q_alloc', GAME2)}, {set('q_kind', 0x1000000), set('wine', true), set('q_alloc', GAME2)},
        {set('q_protect', 0x40)}, {set('q_protect', 0x80)}, {set('q_protect', 0x02)}}) do
      op('show', {1, W1, false}, join(order(), q)); op('show', {1, W1, true}, order())
    end
    local last = use_setter and 20 or 22                            -- the read after the native call
    op('show', {1, W1, false}, {mem(W1, 4, last)}); op('show', {1, W1, true}, join(order(), {set('x_u32', 'late', last)}))
    op('show', {1, W1, false}, {set('nil_fault', true), mem(W1, 4, last)}); op('show', {1, W1, false}, order())
  end)
end
adapter_hand(false); adapter_hand(true)

-- Seeded walks: random world changes, some landing in the middle of an operation; what a change breaks is
-- put back in order a few operations later, so the walk spends most of its time in a working game.
local IDS, PIDS = {'player:1:gun:7', 'player:1:gun:8', 'player:2:gun:1'}, {'player:1', 'player:1', 'player:2'}
local RAISES = {'x_module', 'x_read', 'x_ptr', 'x_u32', 'x_u8', 'x_windows', 'x_set_shown', 'x_setter', 'x_pose_context', 'x_save_global'}
local function walk(seed, count, kind)
  F.seed(seed)
  local use_setter = F.chance(.3)
  F.op('new', {use_setter, false})
  F.op(kind == 'lease' and 'stop' or 'probe', kind == 'lease' and {} or {true, 1}, game())
  local key, id, repair = A, IDS[1], false
  local function change()
    local at = F.chance(.4) and F.rnd(F.last_calls + 3) or 0
    local k, e = F.rnd(48)
    local root = F.pick({ROOT, ROOT, ROOT2})
    if k <= 6 then e = set(F.pick({'s_aim', 's_aim', 's_aim', 's_sa', 's_ca', 'p_aim'}), F.chance(.6))
    elseif k == 7 then e = set('global', F.pick({true, false, false, 0})); repair = true
    elseif k == 8 then e = set('saved', F.pick({'', A, B, A .. ',' .. B, 'extra,' .. A, B .. '=f,' .. A, ZERO .. ',' .. B}))
    elseif k == 9 then e = set('saved', nil); repair = true
    elseif k == 10 then id = F.pick(IDS); e = set('s_id', id)
    elseif k == 11 then e = set('s_id', nil); repair = true
    elseif k == 12 then e = set(F.pick({'s_pid', 'p_pid'}), F.pick(PIDS))
    elseif k == 13 then e = set(F.pick({'s_pid', 'p_pid', 'p_pid'}), F.pick({'', false})); if e.v == false then e.v = nil end; repair = true
    elseif k == 14 then key = F.pick({A, B, B, ZERO}); e = key_at(F.pick({WOBJ, WOBJ, WOBJ2}), key)
    elseif k == 15 then e = set('s_wobj', F.pick({WOBJ, WOBJ2}))
    elseif k == 16 then e = set('s_kind', F.pick({4, 4, 5})); repair = true
    elseif k == 17 then e = set(F.pick({'s_safe', 's_ok'}), false); repair = true
    elseif k == 18 then e = set('foreign', true); repair = true
    elseif k == 19 then e = set(F.pick({'c_enabled', 'c_pose', 'fallback_ready'}), false); repair = true
    elseif k == 20 then e = set(F.pick({'pin_first', 'pin_fn', 'pinned_ready', 'pin_first', 'pin_fn', 'pinned_ready', 'p_foreign'}), true); repair = true
    elseif k == 21 then e = set(F.pick({'has_pinned_ready', 'p_ok'}), false); repair = true
    elseif k <= 24 then e = mem(widget(root, F.rnd(2)), F.pick({SHOWN, SHOWN, HIDDEN, HIDDEN, F.le(0x10, 4), F.le(0xffffffef, 4)}))
    elseif k == 25 then e = mem(widget(root, F.rnd(2)), F.pick({F.le(0, 4), 4})); repair = true
    elseif k == 26 then e = mem(STATE + 0xac29c, F.le(F.pick({3, 3, 5}), 4)); repair = true
    elseif k == 27 then e = mem(F.pick({ROOT + 0x24e334, CONTEXT}), F.pick({'\0', '\2', 1})); repair = true
    elseif k == 28 then repair = true; return point(GAME, F.pick({ROOT2, ROOT2, ROOT}), F.pick({STATE, STATE, STATE2}))
    elseif k == 29 then e = mem(cross(ROOT) + 0xf0, F.pick({F.le(ROOT + 0x828, 8), F.le(ROOT2 + 0x820, 8), 8})); repair = true
    elseif k == 30 then e = mem(widget(ROOT, F.rnd(2)) + F.pick({0xf0, 0xf8}), F.pick({F.le(ROOT, 8), F.le(CONTEXT2, 8), 8, F.le(7, 8)})); repair = true
    elseif k == 31 then local c = F.pick(CHECKS); e = mem(GAME + c[1] + F.rnd(#c[2] / 2) - 1, F.pick({'x', 1})); repair = true
    elseif k == 32 then e = mem(F.pick({GAME + 0x2ce1508, GAME + 0x2e28788, cross(ROOT) + 0xf8, CONTEXT + 0x10, WOBJ + 0x358}), 8); repair = true
    elseif k == 33 then e = mem(CONTEXT + 0x10, F.le(WORLD2, 8)); repair = true
    elseif k == 34 then e = set('game', GAME2); repair = true
    elseif k == 35 then e = set('set_effect', F.pick({1, 2, 3})); repair = true
    elseif k == 36 then e = {slot = 'set_fx', key = widget(ROOT, F.rnd(2)), v = F.pick({1, 2, 3})}; repair = true
    elseif k == 37 then e = set(F.pick({'q_ret', 'q_state', 'q_kind', 'q_kind', 'q_protect', 'q_protect', 'q_alloc'}), F.pick({0, 48, 0x1000, 0x40000, 0x40, 0x80})); repair = true
    elseif k == 38 then e = set('wine', F.chance(.5))
    elseif k == 39 then e = set(F.pick({'save_ok', 'sg_ok', 'has_sg', 'has_global_fn'}), false); repair = true
    elseif k == 40 then e = set(F.pick({'save_why', 'sg_why'}), F.pick({'save failed', 'Disk is full'}))
    elseif k <= 43 then e = set(F.pick(RAISES), F.pick({'Memory unavailable', 'Invalid read'})); at = F.rnd(F.last_calls + 3)
    elseif k == 44 then e = set('global', nil)
    elseif k == 45 then e = set('nil_fault', true); repair = true
    elseif k == 46 then e = set('sg_ok', 0); repair = true
    else repair = false; return order() end
    e.at = at
    return {e}
  end
  for _ = 1, count do
    local events = {}
    if repair and F.chance(.4) then
      events = join(order(), {set('global', F.pick({false, false, true, 0})), set('s_aim', F.chance(.7)), set('saved', F.pick({'', '', A, B})),
        set('has_global_fn', true), set('save_ok', true), set('has_sg', true), set('sg_ok', true)})
      if F.chance(.6) then events[#events + 1] = set('global', nil) end
      repair = false; key = A; id = IDS[1]
    end
    for _ = 1, F.pick({0, 0, 1, 1, 1, 2, 3}) do events = join(events, change()) end
    if F.chance(.12) then events[#events + 1] = set('x_any', F.pick({'Memory unavailable', 'Invalid read'}), F.rnd(F.last_calls + 2)) end
    local call, args
    if kind == 'lease' then
      call = F.pick({'update', 'update', 'update', 'update', 'update', 'update', 'update', 'update', 'stop', 'stop', 'setting', 'target', 'toggle', 'toggle',
        'set_setting', 'current', 'probe', 'visible', F.chance(.01) and 'shutdown' or 'update'})
    elseif kind == 'source' then call = 'current'
    else call = F.pick({'probe', 'probe', 'show', 'show', 'show', 'visible', 'same'}) end
    if call == 'toggle' then args = {F.chance(.8) and key or F.pick({A, B, 'junk'}), F.chance(.8) and id or F.pick(IDS)}
    elseif call == 'set_setting' then args = {F.chance(.5)}
    elseif call == 'current' then args = {F.chance(.3)}
    elseif call == 'probe' then args = {F.chance(.5), F.rnd(2)}
    elseif call == 'visible' then args = {widget(F.pick({ROOT, ROOT2}), F.rnd(2))}
    elseif call == 'same' then args = {F.rnd(3), F.rnd(3)}
    elseif call == 'show' then
      local slot = F.rnd(2)
      if not huds[slot] then call, args = 'probe', {false, slot}
      else args = {slot, F.chance(.9) and huds[slot].widgets[F.rnd(2)] or widget(F.pick({ROOT, ROOT2}), F.rnd(2)) + F.pick({0, 0, 1}), F.chance(.5)} end
    end
    F.op(call, args, events)
  end
end
for seed = 1, 14 do F.run('walk' .. seed, function() walk(seed, 600, 'lease') end) end
for seed = 21, 24 do F.run('source' .. seed, function() walk(seed, 500, 'source') end) end
for seed = 31, 34 do F.run('adapter' .. seed, function() walk(seed, 500, 'adapter') end) end
F.finish()

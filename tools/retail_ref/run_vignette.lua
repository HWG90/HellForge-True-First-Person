-- Drives the retail vignette.lua, unmodified, under LuaJIT (see effects_common.lua).
-- Usage: luajit run_vignette.lua <Goose Gadget Kit folder>
-- Fakes: the stingray table (Application.main_world, World.entities, EntityManager.render_data_component
-- and .alive, RenderDataComponent.instances / get_property / set_property), the clock, the camera-compat
-- object, backend.foreign_camera / :player_snapshot, deps.active_view and the controller.
--
-- The world. Handles (world, manager, entity) are plain numbers, never 0.
--   world, mgr            what main_world() / render_data_component(world) return (nil, false: that value)
--   nent, ent[i]          the entity list: nent entries, entry i is ent[i] (absent: 100000+i; 0: false)
--   ent_mode              1: entities() returns nil, 2: false, 3: a number
--   dead[e]               true: alive(e) is false
--   ninst[e], inst[e*256+i]  the instance ids of entity e (absent: 7; a text: an id that is not a number)
--   inst_table            true: instances() returns one table instead of several results
--   map[k], en[k]         the two properties of the instance at key k = e*256+i (map true: the vignette mapping)
--   set_mode              what set_property does: nil applies, 1 refuses (false), 2 applies and returns false,
--                         3 does nothing, 4 applies and raises, 5 applies and returns true
--   b_<name>              the seven engine functions: a number is the identity of a function (the fake prints
--                         it as @n in the trace), nil is absent, anything else is a value that is not a function
--   no_api                1..4: stingray.Application / World / EntityManager / RenderDataComponent is nil
--   now                   what the clock returns
--   has_av, av            deps.active_view and its answer; has_fc, fc: backend.foreign_camera;
--   has_compat            update() is given a compat object; has_cf, cf: its foreign method
--   p_ok, p_safe, p_kind  backend:player_snapshot(); c_enabled, c_pose: the controller
package.path = arg[0]:gsub('[^/]*$', '') .. '?.lua;' .. package.path
local F = require('effects_common')
local NAMES = {'world', 'entities', 'manager', 'alive', 'instances', 'get', 'set'}
F.slots({'world', 'mgr', 'nent', 'ent', 'ent_mode', 'dead', 'ninst', 'inst', 'inst_table', 'map', 'en', 'set_mode',
  'b_world', 'b_entities', 'b_manager', 'b_alive', 'b_instances', 'b_get', 'b_set', 'no_api', 'now',
  'has_av', 'av', 'has_fc', 'fc', 'has_compat', 'has_cf', 'cf', 'p_ok', 'p_safe', 'p_kind', 'c_enabled', 'c_pose',
  'x_clock', 'x_main_world', 'x_entities', 'x_manager', 'x_alive', 'x_instances', 'x_get', 'x_set',
  'x_compat_foreign', 'x_foreign_camera', 'x_active_view', 'x_player_snapshot', 'x_any'})
local function g(name) return F.world[name] end
local CLASS = 0x35b0f6c2
local MAPPING = 'core/stingray_renderer/shading_environment_components/vignette'

-- Fields the module reads at call time follow the world, also in the middle of a call.
local sync
local function tick(name, args)
  local ok, why = pcall(F.tick, name, args)
  sync()
  if not ok then error(why, 0) end
end
local function handle(v) if v == false then return false end; if type(v) == 'number' and v ~= 0 then return v end; return nil end
local function count(e) local n = F.getk('ninst', e); return type(n) == 'number' and n or 0 end
local function id_at(e, i) local v = F.getk('inst', e * 256 + i); if v == nil then return 7 end; return v end
local function find(e, id)
  for i = 1, count(e) do if id_at(e, i) == id then return e * 256 + i end end
  return e * 256
end
local make = {}
function make.world(t) return function() tick('main_world', t); return handle(g('world')) end end
function make.entities(t) return function(world)
  tick('entities', t .. ' ' .. F.show(world))
  local mode = g('ent_mode')
  if mode == 1 then return nil elseif mode == 2 then return false elseif mode == 3 then return 5 end
  local list = {}
  for i = 1, g('nent') or 0 do
    local v = F.getk('ent', i)
    if v == nil then v = 100000 + i elseif v == 0 then v = false end
    list[i] = v
  end
  return list
end end
function make.manager(t) return function(world) tick('manager', t .. ' ' .. F.show(world)); return handle(g('mgr')) end end
function make.alive(t) return function(e) tick('alive', t .. ' ' .. F.show(e)); return not F.getk('dead', e) end end
function make.instances(t) return function(manager, e)
  tick('instances', t .. ' ' .. F.show(manager) .. ' ' .. F.show(e))
  local n, list = count(e), {}
  for i = 1, n do list[i] = id_at(e, i) end
  if g('inst_table') then return list end
  return unpack(list, 1, n)
end end
function make.get(t) return function(manager, e, id, path)
  assert(#path == 1)
  tick('get', table.concat({t, F.show(manager), F.show(e), F.show(id), path[1]}, ' '))
  local v = F.getk(path[1] == 'shading_environment_mapping' and 'map' or 'en', find(e, id))
  if v == true and path[1] == 'shading_environment_mapping' then return MAPPING end
  return v
end end
function make.set(t) return function(manager, e, id, path, value)
  assert(#path == 1)
  tick('set', table.concat({t, F.show(manager), F.show(e), F.show(id), path[1], F.show(value)}, ' '))
  local mode = g('set_mode') or 0
  if mode ~= 1 and mode ~= 3 then F.poke('en', find(e, id), value) end
  if mode == 4 then error('setter uncertain', 0) end
  if mode == 1 or mode == 2 then return false end
  if mode == 5 then return true end
end end
local made, token_of = {}, {}
local function bind(name)
  local v = g('b_' .. name)
  if type(v) ~= 'number' then return v end
  made[name] = made[name] or {}
  if not made[name][v] then made[name][v] = make[name]('@' .. F.num(v)); token_of[made[name][v]] = v end
  return made[name][v]
end

local application, world_api, entity_manager, render_data = {}, {}, {}, {}
local sr, backend, compat, deps, controller = {}, {}, {}, {}, {}
local function compat_foreign(self) tick('compat_foreign', F.show(self == compat)); return g('cf') end
local function foreign_camera(...) tick('foreign_camera', F.num(select('#', ...))); return g('fc') end
local function active_view(...) tick('active_view', F.num(select('#', ...))); return g('av') end
function sync()
  local missing = g('no_api')
  application.main_world, world_api.entities = bind('world'), bind('entities')
  entity_manager.render_data_component, entity_manager.alive = bind('manager'), bind('alive')
  render_data.instances, render_data.get_property, render_data.set_property = bind('instances'), bind('get'), bind('set')
  sr.Application, sr.World = missing ~= 1 and application or nil, missing ~= 2 and world_api or nil
  sr.EntityManager, sr.RenderDataComponent = missing ~= 3 and entity_manager or nil, missing ~= 4 and render_data or nil
  backend.foreign_camera = g('has_fc') and foreign_camera or nil
  compat.foreign = g('has_cf') and compat_foreign or nil
  deps.active_view = g('has_av') and active_view or nil
end
function backend:player_snapshot()
  tick('player_snapshot', F.show(self == backend))
  if g('p_ok') ~= true then return nil, 'Player unavailable' end
  return {safe = g('p_safe'), camera_kind = g('p_kind')}
end
function controller:result() tick('result'); return {mode = g('c_pose') and 'pose_offset' or 'other'} end
function controller:is_enabled() tick('is_enabled'); return g('c_enabled') end
deps.clock = function() tick('clock'); return g('now') end

local M = F.load('vignette.lua')
local v
local function upvalue(fn, wanted)
  for i = 1, 60 do
    local name, value = debug.getupvalue(fn, i)
    if not name then return nil end
    if name == wanted then return value end
  end
end
local function state()
  local r = v:result()
  local out = {F.show(v.phase), F.num(v.writes), F.num(v.restores), F.num(v.reassertions), F.show(v.error_message), '|',
    F.show(r.phase), F.show(r.disabled), F.show(r.owned), F.num(r.profiles), F.num(r.writes), F.num(r.restores), F.num(r.reassertions),
    F.show(r.error_message), F.show(r.cleanup_pending), '|', F.show(v:status()), '|', F.show(v:is_clean()),
    F.show(upvalue(v.shutdown, 'closing')), F.num(upvalue(v.stop, 'next_scan')), F.num(upvalue(v.stop, 'next_poll')), '|'}
  local context, scan, rows = upvalue(v.stop, 'context'), upvalue(v.stop, 'scan'), upvalue(v.stop, 'rows')
  if context then
    out[#out + 1] = F.show(context.world) .. ' ' .. F.show(context.manager)
    for _, name in ipairs(NAMES) do out[#out + 1] = '@' .. F.num(token_of[context.api[name]]) end
  else out[#out + 1] = '~' end
  out[#out + 1] = '|'
  out[#out + 1] = scan and F.num(scan.index) .. '/' .. F.num(#scan.entities) or '~'
  out[#out + 1] = '|'
  for _, row in ipairs(rows) do
    out[#out + 1] = '[' .. F.show(row.entity) .. ' ' .. F.show(row.id) .. ' ' .. F.show(row.owned and true or false) .. ' ' .. F.show(row.before) .. ']'
  end
  return table.concat(out, ' ')
end
function F.dispatch(call, a)
  -- new: a[1] true builds the instance without the stingray table.
  if call == 'new' then v = M.new(backend, F.log, not a[1] and sr or nil, deps) end
  sync()
  local out
  if call == 'new' then out = 'new'
  elseif call == 'update' or call == 'stop' or call == 'shutdown' then
    local r1, r2
    if call == 'update' then r1, r2 = v:update(controller, nil, g('has_compat') and compat or nil) else r1, r2 = v[call](v) end
    out = F.show(r1) .. ' ' .. F.show(r2)
  elseif call == 'peek' then out = 'peek'
  else error(call) end
  return out .. ' | ' .. state()
end

local function set(slot, value, at) return {slot = slot, v = value, at = at} end
local function key(slot, k, value, at) return {slot = slot, key = k, v = value, at = at} end
local function join(...)
  local out = {}
  for _, list in ipairs({...}) do for _, e in ipairs(list) do out[#out + 1] = e end end
  return out
end
-- Entity e at list position `index` with the given instances: {id, mapping, enabled} each.
local function entity(index, e, instances)
  local out = {key('ent', index, e), key('ninst', e, #instances), key('dead', e, nil)}
  for i, inst in ipairs(instances) do
    out[#out + 1] = key('inst', e * 256 + i, inst[1]); out[#out + 1] = key('map', e * 256 + i, inst[2]); out[#out + 1] = key('en', e * 256 + i, inst[3])
  end
  return out
end
local T
local function tm(d) T = T + d; return set('now', T) end
-- A consistent game: one vignette on entity 11, a second on entity 12 beside an unrelated instance, an instance
-- of the same class under another mapping on entity 13. Every slot is set, so it also puts a world back in order.
local function game()
  T = 10
  local e = {set('world', 5), set('mgr', 9), set('nent', 3), set('ent_mode', nil), set('inst_table', nil), set('set_mode', nil),
    set('no_api', nil), set('now', T), set('has_av', true), set('av', true), set('has_fc', true), set('fc', false),
    set('has_compat', true), set('has_cf', true), set('cf', false), set('p_ok', true), set('p_safe', true), set('p_kind', 6),
    set('c_enabled', true), set('c_pose', true)}
  for _, name in ipairs(NAMES) do e[#e + 1] = set('b_' .. name, 1) end
  for i = 4, 8 do e[#e + 1] = key('ent', i, nil) end
  for h = 14, 16 do e[#e + 1] = key('ninst', h, nil); e[#e + 1] = key('dead', h, nil) end
  return join(e, entity(1, 11, {{CLASS, true, true}}), entity(2, 12, {{7, true, true}, {CLASS + 4294967296, true, true}}),
    entity(3, 13, {{CLASS, 'foreign', true}}))
end
local K11, K12 = 11 * 256 + 1, 12 * 256 + 2
local function U(...) return F.op('update', nil, join(...)) end
local function step(...) return F.op('update', nil, join({tm(.11)}, ...)) end
local function stop(...) return F.op('stop', nil, join(...)) end
local function one(e) return {e} end

F.run('life', function()
  F.op('new'); F.op('peek'); stop(); F.op('shutdown'); F.op('new')
  U(game()); U({tm(.05)}); U({tm(.06)}); step(); F.op('peek')
  step({key('en', K11, true)}); step({key('en', K11, true), key('en', K12, true)}); step()
  -- A later vignette is found by the next inventory, one second after the last.
  step(entity(4, 14, {{CLASS + 2 * 4294967296, true, true}}), {set('nent', 4)})
  for _ = 1, 9 do step() end
  stop(); stop(); F.op('peek')
  U(); step(); F.op('shutdown'); U(); step(); stop(); F.op('shutdown'); F.op('peek')
end)
F.run('ignore', function()
  F.op('new'); U(game(), {key('en', K11, false), key('en', K12, false)}); step(); stop()
  -- Nothing with the mapping: reported once, again after the message was cleared.
  U({key('map', K11, 'foreign'), key('map', K12, nil)}); step(); step(); stop(); U(); step({set('nent', 0)}); step()
  T = T + 2; U({set('now', T), key('map', K11, true), key('en', K11, true), set('nent', 3)}); step(); stop()
  -- Mapping texts that only resemble the vignette's.
  for _, text in ipairs({MAPPING .. 'x', MAPPING:sub(1, -2), MAPPING .. '\0', '', MAPPING:upper(), 5, false}) do
    T = T + 2; U({set('now', T), key('map', K11, text)})
  end
  -- The enabled property must be Boolean when a profile is taken in, and at every poll.
  for _, value in ipairs({1, 'true', 0}) do
    stop(); T = T + 2; U({set('now', T), key('map', K11, true), key('map', K12, 'x'), key('en', K11, value)})
    stop(); U({key('en', K11, true)}); step({key('en', K11, value)}); step({key('en', K11, nil)}); stop({key('en', K11, false)})
  end
  stop({key('en', K11, nil)}); stop({key('en', K11, false)})
end)
F.run('retire', function()
  F.op('new'); U(game()); stop({key('dead', 11, true)})
  U({key('dead', 11, nil), key('en', K12, true)}); stop({key('map', K11, 'foreign')})
  U({key('map', K11, true), key('en', K12, true)}); stop({key('ninst', 11, 0)})
  U(game()); step({key('ninst', 11, 0)}); step(); step({key('ninst', 11, 1), key('en', K11, true)})
  T = T + 1; U({set('now', T)}); step({key('dead', 12, true)}); step({key('inst', K11, CLASS + 4294967296)}); step(); stop()
  -- Three profiles; the middle one, then the first, then the last goes away.
  U(game(), entity(3, 13, {{CLASS, true, true}}))
  step({key('dead', 12, true)}); step({key('dead', 11, true)}); step({key('en', 13 * 256 + 1, true)}); step({key('dead', 13, true)}); step(); stop()
  -- The value is already back (or the game re-enabled it) when the mode stops: nothing to restore.
  U(game()); stop({key('en', K11, true)}); U(); stop({key('en', K12, true), key('dead', 12, true)})
end)
F.run('foreign', function()
  F.op('new'); U(game())
  step({set('fc', true)}); stop(); F.op('peek'); step({set('fc', false), set('av', false)}); step({set('fc', true)}); stop()
  step({set('fc', false), set('av', true)})
  -- The compat object decides in update; stop only asks backend.foreign_camera.
  step({set('cf', true)}); step({set('cf', false)}); step({set('cf', true), set('fc', true)}); step({set('fc', false)})
  step({set('cf', false), set('x_compat_foreign', 'compat gone')}); step()
  step({set('has_cf', false), set('fc', true)}); step({set('has_compat', false), set('has_cf', true)}); step({set('fc', false)})
  step({set('x_foreign_camera', 'camera gone')}); step(); stop({set('x_foreign_camera', 'camera gone')}); stop()
  step({set('has_fc', false)}); stop(); step({set('has_compat', true), set('cf', true)}); step({set('cf', nil)}); step({set('cf', 0)})
  step({set('cf', false), set('fc', nil), set('has_fc', true), set('has_cf', false)}); stop(); stop({set('fc', false)})
  -- active_view must be exactly true.
  for _, value in ipairs({true, 1, 'yes', false, true}) do step({set('av', value), set('has_cf', true)}) end
  step({set('av', nil)}); step({set('av', true), set('x_active_view', 'view gone')}); step()
end)
F.run('fallback', function()
  F.op('new'); U(game(), {set('has_av', false)})
  local function try(slot, value, back) step({set(slot, value)}); step({set(slot, back)}) end
  try('p_ok', false, true); try('p_safe', false, true); try('p_safe', nil, 1); try('p_kind', 4, 6); try('p_kind', nil, 6)
  try('c_enabled', false, true); try('c_enabled', nil, 1); try('c_pose', false, true); try('fc', true, false); try('cf', true, false)
  step({set('x_player_snapshot', 'Memory unavailable')}); step(); step({set('p_ok', false), set('c_enabled', false)}); stop()
  step({set('p_ok', true), set('c_enabled', true)}); F.op('shutdown'); step()
end)
F.run('setter', function()
  F.op('new')
  for mode = 1, 5 do
    U(game(), {set('set_mode', mode)}); step(); stop(); step({set('set_mode', nil)}); step({key('en', K11, true), set('set_mode', mode)})
    stop(); stop({set('set_mode', nil)}); stop({key('en', K11, true), key('en', K12, true)})
  end
  U(game(), {set('x_set', 'setter gone')}); step(); stop({set('x_set', 'setter gone')}); stop()
end)
F.run('bindings', function()
  F.op('new', {true}); U(game()); stop(); F.op('new')
  for missing = 1, 4 do U(game(), {set('no_api', missing)}); step({set('no_api', nil)}); stop({set('no_api', missing)}); stop({set('no_api', nil)}) end
  for _, name in ipairs(NAMES) do
    for _, value in ipairs({'text', false, true, 'nil', 2}) do
      if value == 'nil' then value = nil end
      U(game(), {set('b_' .. name, value)}); step({set('b_' .. name, 1)}); step({set('b_' .. name, value)})
      stop(); stop({set('b_' .. name, 1)}); stop()
    end
  end
  -- Absent and not a function together: "unavailable" wins whichever of the two it is.
  U(game(), {set('b_world', nil), set('b_set', 'x')}); U({set('b_world', 'x'), set('b_set', nil)}); U({set('b_get', 3), set('b_world', nil)})
  U({set('b_world', 1), set('b_set', 1)}); stop(); stop({set('b_get', 1)}); stop({set('b_get', 3)})
  U(); step(); stop()
end)
F.run('world', function()
  F.op('new'); U(game()); step({set('world', 6)}); stop()                     -- rows of the old world are dropped, not restored
  U(game()); stop({set('world', 6)}); U(game()); stop({set('world', nil)}); stop()
  U(game()); step({set('world', nil)}); step({set('world', false)}); step({set('world', 0)}); step({set('world', 5)}); stop()
  U(game(), {set('mgr', nil)}); step({set('mgr', false)}); step({set('mgr', 10)}); step({set('mgr', 9)}); stop()
  U(game(), {set('x_main_world', 'no world')}); step({set('x_manager', 'no manager')}); step(); stop({set('x_main_world', 'no world')}); stop()
end)
F.run('entities', function()
  F.op('new')
  for mode = 1, 3 do U(game(), {set('ent_mode', mode)}); step({set('ent_mode', nil)}); stop() end
  U(game(), {set('x_entities', 'no entities')}); step(); stop()
  U(game(), {set('nent', 0)}); step(); stop()
  U(game(), {set('nent', 32769)}); step(); stop(); U({set('nent', 32768)}); step(); step({set('nent', 32769)}); stop()
  -- A false entry ends the inventory there.
  U(game(), {key('ent', 2, 0)}); stop(); U(game(), {key('ent', 1, 0)}); stop()
  -- 96 entities per update: exactly one slice, one more, and a long inventory with the vignette at the end.
  for _, n in ipairs({95, 96, 97, 192, 193}) do
    U(game(), {set('nent', n)}, entity(n, 14, {{CLASS, true, true}})); step(); step(); step(); stop({key('ent', n, nil)})
  end
  U(game(), {set('nent', 300)}, entity(300, 14, {{CLASS, true, true}}))
  for _ = 1, 4 do step() end
  -- The list is the one taken when the inventory began; an entity that died since is passed over.
  T = T + 1; U({set('now', T), set('nent', 250)}, entity(200, 15, {{CLASS, true, true}}), entity(100, 16, {{CLASS, true, true}}))
  step({key('dead', 15, true), set('nent', 3)}); step({key('dead', 15, nil)}); step(); step(); F.op('shutdown')
  -- A raise in the middle of a slice loses that entity only.
  F.op('new'); U(game(), {set('x_alive', 'no entity', 3)}); step(); T = T + 1; U({set('now', T)}); stop()
end)
F.run('instances', function()
  F.op('new')
  local function many(n, class_at)
    local e = {key('ninst', 11, n)}
    for i = 1, n do e[#e + 1] = key('inst', K11 - 1 + i, i == class_at and CLASS or nil) end
    return join(e, {key('map', K11 - 1 + class_at, true), key('en', K11 - 1 + class_at, true)})
  end
  U(game(), many(64, 64)); step(); stop(); U(game(), many(65, 65)); step(); stop()
  U(game()); step(many(65, 1)); stop(); stop(many(64, 1)); stop(many(1, 1))
  for _, table_mode in ipairs({true, false}) do
    -- One table instead of several results; ids of every kind on one entity; the same id twice.
    local ids = {CLASS, CLASS + 4294967296, 'text', CLASS - 4294967296, CLASS + 9007199254740992, CLASS + .5, 1 / 0, -1 / 0, 0, -CLASS,
      CLASS + 4294967296, CLASS + 1, CLASS - 1, CLASS * 4294967296, 2 ^ 80}
    local list = {}
    for i, id in ipairs(ids) do list[i] = {id, true, true} end
    U(game(), {set('inst_table', table_mode)}, entity(1, 11, list)); step(); step({key('en', K11 + 3, true)}); stop()
    U(game(), {set('inst_table', table_mode), key('ninst', 11, 0), key('ninst', 12, 0)}); step({key('ninst', 12, 1)}); stop()
    U(game(), {set('inst_table', table_mode)}, entity(1, 11, {{CLASS, true, true}, {CLASS, true, false}})); step(); stop()
  end
  U(game(), {set('x_instances', 'no instances')}); step(); stop({set('x_instances', 'no instances')}); stop()
  U(game(), {set('x_get', 'no property')}); step(); stop({set('x_get', 'no property')}); stop()
end)
F.run('profiles', function()
  for _, n in ipairs({32, 33, 34}) do
    F.op('new')
    local e = {set('nent', n)}
    for i = 1, n do e = join(e, entity(i, 20 + i, {{CLASS + i * 4294967296, true, true}})) end
    U(game(), e); step(); step(); T = T + 1; U({set('now', T)}); step({key('en', 21 * 256 + 1, true), key('en', 52 * 256 + 1, true)})
    stop({key('dead', 30, true)}); stop()
  end
end)
F.run('clock', function()
  F.op('new')
  for _, value in ipairs({0 / 0, 'text', 'nil', false}) do
    if value == 'nil' then value = nil end
    U(game(), {set('now', value)}); U({set('now', 10)}); U({set('now', value)}); stop()
  end
  U(game(), {set('x_clock', 'Engine FPV clock sample invalid')}); U(); stop()
  -- The longest text the C keeps whole (255 bytes); the same text again is not logged again.
  local long = string.rep('long text ', 25) .. 'abcde'
  U(game(), {set('x_clock', long)}); U({set('x_clock', long)}); U({set('x_clock', long:sub(1, 254))}); U(); stop()
  -- Infinity passes; nothing is scanned or polled afterwards until a stop.
  U(game(), {set('now', 1 / 0)}); U({set('now', 1e300), key('en', K11, true)}); U({set('now', 1 / 0), key('en', K12, true)}); stop()
  U(game(), {set('now', -1 / 0)}); U({set('now', 0)}); U({set('now', -1)}); stop()
  -- Back in time: no poll before the old deadline. The deadlines are sums of doubles.
  U(game(), {set('now', 100.3)}); U({set('now', 100.39999), key('en', K11, true)}); U({set('now', 100.4)}); U({set('now', 100.4 + 1e-13)})
  U({set('now', 50), key('en', K11, true)}); U({set('now', 100.5)}); U({set('now', 101.29999), set('nent', 0)}); U({set('now', 101.3)})
  U({set('now', 0.3), set('nent', 3)}); stop(); U({set('now', 0.3)}); U({set('now', 0.4), key('en', K11, true)}); U({set('now', 0.1 + 0.3)})
  U({set('now', 1.3), set('nent', 0)}); U({set('now', 1e16)}); U({set('now', 1e16 + 2), key('en', K12, true)}); stop()
end)

-- Sweeps: the same situation again and again, with one raise or one change placed before each host call in turn.
local SITUATIONS = {
  first = {prep = function() end, call = 'update', before = function() return game() end},
  poll = {prep = function() U(game()) end, call = 'update', before = function() return {tm(1.2), key('en', K11, true), key('en', K12, true)} end},
  quiet = {prep = function() U(game()) end, call = 'update', before = function() return {tm(1.2)} end},
  restore = {prep = function() U(game()) end, call = 'stop', before = function() return {} end},
  away = {prep = function() U(game()) end, call = 'update', before = function() return {tm(.11), set('av', false)} end},
}
local CHANGES = {
  raise = function(at) return {set('x_any', 'raised in the host', at)} end,
  world = function(at) return {set('world', 6, at)} end,
  no_world = function(at) return {set('world', nil, at)} end,
  token = function(at) return {set('b_alive', 2, at)} end,
  token_set = function(at) return {set('b_set', 2, at)} end,
  token_world = function(at) return {set('b_world', 2, at)} end,
  no_binding = function(at) return {set('b_get', nil, at)} end,
  bad_binding = function(at) return {set('b_instances', 'x', at)} end,
  no_api = function(at) return {set('no_api', 4, at)} end,
  dead = function(at) return {key('dead', 11, true, at)} end,
  dead2 = function(at) return {key('dead', 12, true, at)} end,
  mapping = function(at) return {key('map', K12, 'foreign', at)} end,
  enabled = function(at) return {key('en', K11, true, at)} end,
  disabled = function(at) return {key('en', K12, false, at)} end,
  not_boolean = function(at) return {key('en', K11, 1, at)} end,
  gone = function(at) return {key('ninst', 12, 1, at)} end,
  too_many = function(at) return {key('ninst', 11, 65, at)} end,
  camera = function(at) return {set('fc', true, at)} end,
  compat = function(at) return {set('cf', true, at)} end,
  no_camera = function(at) return {set('has_fc', false, at), set('has_cf', false, at)} end,
  refuse = function(at) return {set('set_mode', 1, at)} end,
  uncertain = function(at) return {set('set_mode', 4, at)} end,
  ignore = function(at) return {set('set_mode', 3, at)} end,
}
local function sorted(t) local names = {}; for name in pairs(t) do names[#names + 1] = name end; table.sort(names); return names end
for _, situation in ipairs(sorted(SITUATIONS)) do
  local s = SITUATIONS[situation]
  for _, change in ipairs(sorted(CHANGES)) do
    F.run('sweep_' .. situation .. '_' .. change, function()
      F.op('new'); s.prep(); F.op(s.call, nil, s.before())
      local calls = F.last_calls
      for at = 1, calls + 1 do
        F.op('new'); s.prep(); F.op(s.call, nil, join(s.before(), CHANGES[change](at)))
        step(); stop()
      end
    end)
  end
end

-- Seeded walks: random world changes, some landing in the middle of an operation.
local function walk(seed, count)
  F.seed(seed)
  F.op('new'); U(game())
  local raises = {'x_clock', 'x_main_world', 'x_entities', 'x_manager', 'x_alive', 'x_instances', 'x_get', 'x_set', 'x_compat_foreign',
    'x_foreign_camera', 'x_active_view', 'x_player_snapshot'}
  local handles = {11, 12, 13, 14, 15, 16}
  local ids = {CLASS, CLASS, CLASS + 4294967296, CLASS + 3 * 4294967296, 7, 'text', CLASS - 4294967296, CLASS + .5}
  local closing = false
  local function change()
    local at = F.chance(.4) and F.rnd(F.last_calls + 3) or 0
    local k, e = F.rnd(34)
    if k == 1 then e = set('av', F.pick({true, true, true, true, false, 1}))
    elseif k == 2 then e = set('fc', F.pick({false, false, false, false, true, 'nil'}))
    elseif k == 3 then e = set('cf', F.pick({false, false, false, false, true}))
    elseif k == 4 then e = set('has_fc', F.chance(.85))
    elseif k == 5 then e = set('has_cf', F.chance(.7))
    elseif k == 6 then e = set('has_compat', F.chance(.8))
    elseif k == 7 then e = set('has_av', F.chance(.8))
    elseif k == 8 then e = set(F.pick({'p_ok', 'p_safe', 'c_enabled', 'c_pose'}), F.chance(.85))
    elseif k == 9 then e = set('p_kind', F.pick({6, 6, 6, 4, 5}))
    elseif k == 10 then e = set('world', F.pick({5, 5, 5, 5, 6, 'nil', false}))
    elseif k == 11 then e = set('mgr', F.pick({9, 9, 9, 10, 'nil'}))
    elseif k == 12 then e = set('b_' .. F.pick(NAMES), F.pick({1, 1, 1, 1, 2, 'nil', 'text', false}))
    elseif k == 13 then e = set('no_api', F.pick({'nil', 'nil', 'nil', 1, 2, 3, 4}))
    elseif k == 14 then e = set('nent', F.pick({0, 1, 3, 5, 6, 6, 8, 8, 100, 200}))
    elseif k == 15 or k == 16 then e = key('ent', F.rnd(8), F.pick({11, 12, 13, 14, 15, 16, 11, 12, 0}))
    elseif k == 17 then e = key('dead', F.pick(handles), F.chance(.3) or nil)
    elseif k == 18 then e = key('ninst', F.pick(handles), F.pick({0, 1, 1, 1, 2, 2, 3, 64, 65}))
    elseif k == 19 or k == 20 then e = key('inst', F.pick(handles) * 256 + F.rnd(3), F.pick(ids))
    elseif k == 21 or k == 22 then e = key('map', F.pick(handles) * 256 + F.rnd(3), F.pick({true, true, true, true, 'foreign', 'nil'}))
    elseif k >= 23 and k <= 26 then e = key('en', F.pick(handles) * 256 + F.rnd(3), F.pick({true, true, true, false, false, 'nil', 1}))
    elseif k == 27 then e = set('set_mode', F.pick({'nil', 'nil', 'nil', 'nil', 1, 2, 3, 4, 5}))
    elseif k == 28 then e = set('inst_table', F.chance(.5))
    elseif k == 29 then e = set('ent_mode', F.pick({'nil', 'nil', 'nil', 'nil', 1, 2, 3}))
    elseif k == 30 then e = set(F.pick(raises), F.pick({'Memory unavailable', 'Invalid handle'})); at = F.rnd(F.last_calls + 3)
    elseif k == 31 then e = set('now', F.pick({0 / 0, 1 / 0, 'text', T - 5, T - .05, 0})); at = 0
    else
      -- Everything back in order, keeping the time; more vignettes on the spare entities.
      local now = T
      local events = join(game(), {set('nent', 6)}, entity(4, 14, {{CLASS + 2 * 4294967296, true, true}}),
        entity(5, 15, {{CLASS, true, true}, {CLASS + 4294967296, true, false}}), entity(6, 16, {{7, true, true}}))
      T = now; events[#events + 1] = set('now', T)
      return events
    end
    if e.v == 'nil' then e.v = nil end
    e.at = at
    return {e}
  end
  for _ = 1, count do
    local events = {}
    if F.chance(.75) then events[1] = tm(F.pick({.03, .06, .11, .11, .11, .5, 1.01, 2.5})) end
    for _ = 1, F.pick({0, 0, 0, 1, 1, 1, 2, 3}) do events = join(events, change()) end
    if F.chance(.12) then events[#events + 1] = set('x_any', F.pick({'Memory unavailable', 'Invalid handle'}), F.rnd(F.last_calls + 2)) end
    local call = F.pick({'update', 'update', 'update', 'update', 'update', 'update', 'update', 'stop', 'stop', 'peek'})
    if F.chance(.01) then call = 'shutdown' end
    if closing and F.chance(.3) then call = 'new' end
    if call == 'shutdown' then closing = true elseif call == 'new' then closing = false end
    F.op(call, call == 'new' and {F.chance(.05)} or nil, events)
  end
end
for seed = 1, 8 do F.run('walk' .. seed, function() walk(seed, 700) end) end
F.finish()

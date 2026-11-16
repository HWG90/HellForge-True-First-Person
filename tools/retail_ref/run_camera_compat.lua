-- Drives the retail camera_compat.lua, unmodified, under LuaJIT (see effects_common.lua).
-- Usage: luajit run_camera_compat.lua <Goose Gadget Kit folder>
-- Fakes: the other mods' globals (Photo's camera-ownership table, Photo's legacy ActionCam bridge with its
-- set_input_blocked function, package.loaded), debug.getupvalue, the reader functions that sit in the
-- legacy bridge, the controller and the logger. The world:
--   g_own, g_api        the two globals: nil, false / true (a value that is no table) or a table number
--   t_held, t_avail, t_dis, t_set [table]   the fields of a table; t_set: nil, false, true or a setter number
--   rd [setter]         the reader the setter holds: nil, false / true (no function), a foreign reader
--                       number (below 1000) or 1000 + the serial of one of the module's own shims
--   up_pos, up_mode [setter]   debug.getupvalue: the index of 'input_reader' (1 when nil); mode 1 ends the
--                       upvalue list there instead, mode 2 raises there
--   set_install [setter]  nil: the setter stores what it is given; false: it stores nothing; a number: it
--                       stores that reader instead.  set_calls: it calls the given reader.  set_raise: it
--                       raises that text afterwards.  set_ret, set_issue: its two results
--   r_beh [reader]      what a foreign reader returns (false, true, nil); a text: it raises; a number k: it
--                       calls the reader setter k holds now and returns that result
--   pkg, pkg_loaded     package / package.loaded: nil, true (no table) or a number (a table);
--   actioncam           package.loaded['mods/actioncam/actioncam']
--   dbg_fn              debug.getupvalue is a function;  c_enabled: what controller:is_enabled() returns;
--   c_gone              the controller has lost its is_enabled function
-- Host calls (each ticks): photo_camera_ownership, photo_actioncam_compat, actioncam_loaded (the three
-- rawget(G, ...) of provider()), field (one index of a provider table), getupvalue, set, previous, is_enabled.
package.path = arg[0]:gsub('[^/]*$', '') .. '?.lua;' .. package.path
local F = require('effects_common')
F.slots({'g_own', 'g_api', 'pkg', 'pkg_loaded', 'actioncam', 'dbg_fn', 'c_enabled', 'c_gone',
  't_held', 't_avail', 't_dis', 't_set', 'rd', 'up_pos', 'up_mode', 'set_install', 'set_calls', 'set_raise', 'set_ret', 'set_issue', 'r_beh',
  'x_getupvalue', 'x_set', 'x_previous', 'x_is_enabled', 'x_any'})
local real_debug, real_rawget = debug, rawget
local OWN, API, ACTIONCAM = 'DBF_PHOTO_CAMERA_OWNERSHIP', 'DBF_PHOTO_ACTIONCAM_COMPAT', 'mods/actioncam/actioncam'

local G, fakedbg, controller, compat
local tables, table_id, setters, setter_id, readers, reader_id = {}, {}, {}, {}, {}, {}
local shims, shim_serial, count, depth = {}, {}, 0, 0
local sync, harness_error

local function tick(name, args)
  local ok, err = pcall(F.tick, name, args)
  sync()
  if not ok then error(err, 0) end
end
-- The serial of one of the module's shims: they are numbered in the order the module makes them.
local function serial_of(fn)
  local n = shim_serial[fn]
  if not n then count = count + 1; n = count; shim_serial[fn] = n; shims[n] = fn end
  return n
end
-- The text of a Lua value, as tests/camera_compat_test.c prints a TfpCameraCompatValue.
local function vt(v)
  if v == nil then return '~' elseif v == false then return 'F' elseif v == true then return 'T' end
  local t = type(v)
  if t == 'number' then return '<number>' elseif t == 'string' then return '<string>' end
  if t == 'table' then return 'tb' .. assert(table_id[v]) end
  assert(t == 'function')
  if setter_id[v] then return 'f' .. (256 + setter_id[v]) end
  if reader_id[v] then return 'f' .. (512 + reader_id[v]) end
  return 'H' .. serial_of(v)
end
local reader, setter
local function decode(v)
  if type(v) ~= 'number' then return v end
  if v < 1000 then return reader(v) end
  -- (Not an assert: retail calls the fakes under pcall.)
  if not shims[v - 1000] then harness_error = 'the world names a shim that does not exist' end
  return shims[v - 1000]
end
local function encode(fn)
  if fn == nil then return nil end
  if reader_id[fn] then return reader_id[fn] end
  return 1000 + serial_of(fn)
end
-- A foreign reader function.
function reader(rid)
  if readers[rid] then return readers[rid] end
  local function fn()
    tick('previous', 'f' .. (512 + rid))
    local beh = F.getk('r_beh', rid)
    if type(beh) == 'string' then error(beh, 0) end
    if type(beh) == 'number' then
      local current = decode(F.getk('rd', beh))
      if type(current) ~= 'function' or depth >= 6 then return false end
      depth = depth + 1
      local ok, value = pcall(current)
      depth = depth - 1
      if not ok then error(value, 0) end
      return value
    end
    return beh
  end
  readers[rid] = fn; reader_id[fn] = rid
  return fn
end
-- A provider's set_input_blocked.
function setter(sid)
  if setters[sid] then return setters[sid] end
  local function fn(given)
    tick('set', 'f' .. (256 + sid) .. ' ' .. vt(given))
    local install = F.getk('set_install', sid)
    if install == nil then F.poke('rd', sid, encode(given)) elseif install ~= false then F.poke('rd', sid, install) end
    if F.getk('set_calls', sid) and type(given) == 'function' then pcall(given) end
    local raise = F.getk('set_raise', sid)
    if raise ~= nil then error(raise, 0) end
    return F.getk('set_ret', sid), F.getk('set_issue', sid)
  end
  setters[sid] = fn; setter_id[fn] = sid
  return fn
end
-- A provider table: every index is one host call.
local function provider_table(tid)
  if tables[tid] then return tables[tid] end
  local t = setmetatable({}, {__index = function(_, key)
    tick('field', 'tb' .. tid .. ' ' .. key)
    if key == 'held' then return F.getk('t_held', tid) end
    if key == 'available' then return F.getk('t_avail', tid) end
    if key == 'disabled' then return F.getk('t_dis', tid) end
    assert(key == 'set_input_blocked', 'retail read an unexpected field: ' .. tostring(key))
    local v = F.getk('t_set', tid)
    if type(v) == 'number' then return setter(v) end
    return v
  end})
  tables[tid] = t; table_id[t] = tid
  return t
end
local function getupvalue(fn, index)
  tick('getupvalue', vt(fn) .. ' ' .. index)
  local sid = setter_id[fn]
  if not sid then error("bad argument #1 to 'getupvalue' (function expected)", 0) end
  local pos = F.getk('up_pos', sid) or 1
  if index < pos then return 'other' .. index, index end
  if index > pos then return nil end
  local mode = F.getk('up_mode', sid)
  if mode == 1 then return nil elseif mode == 2 then error('upvalue inspection refused', 0) end
  return 'input_reader', decode(F.getk('rd', sid))
end
local function is_enabled(self)
  assert(self == controller)
  tick('is_enabled')
  return F.get('c_enabled')
end
local function global(v)
  if type(v) == 'number' then return provider_table(v) end
  return v
end
-- What the module reads without a call follows the world.
function sync()
  if not G then return end
  G[OWN] = global(F.get('g_own')); G[API] = global(F.get('g_api'))
  local pkg = F.get('pkg')
  if type(pkg) == 'number' then
    local loaded = F.get('pkg_loaded')
    if type(loaded) == 'number' then loaded = {[ACTIONCAM] = F.get('actioncam')} end
    pkg = {loaded = loaded}
  end
  G.package = pkg
  fakedbg.getupvalue = F.get('dbg_fn') and getupvalue or nil
  controller.is_enabled = not F.get('c_gone') and is_enabled or nil
end
-- provider()'s three rawget(G, ...) are host calls.
function rawget(t, key)
  if t == G and G then
    if key == OWN then tick('photo_camera_ownership') elseif key == API then tick('photo_actioncam_compat')
    elseif key == 'package' then tick('actioncam_loaded') end
  end
  return real_rawget(t, key)
end

local M = F.load('camera_compat.lua')
local function upvalue(wanted)
  for _, fn in ipairs({compat.prepare, compat.result, compat.stop}) do
    for i = 1, 60 do
      local name, value = real_debug.getupvalue(fn, i)
      if not name then break end
      if name == wanted then return value end
    end
  end
end
local function state()
  local record, rec = upvalue('record'), '~'
  if record then
    rec = table.concat({'#' .. serial_of(record.shim), vt(record.provider), vt(record.setter), vt(record.previous), F.show(record.blocking),
      F.show(record.reading)}, ' ')
  end
  return table.concat({upvalue('phase'), F.show(upvalue('conflict')), F.show(upvalue('closing')), F.show(upvalue('message')), '|', rec}, ' ')
end
-- a[1]: where debug comes from (0 G.debug, 1 the global, 2 nowhere, 3 G.debug is no table);
-- a[2]: no controller; a[3]: no logger.
local function fresh(a)
  local mode = a[1] or 0
  shims, shim_serial, count = {}, {}, 0
  G, fakedbg, controller = {}, {}, {}
  if mode == 0 then G.debug = fakedbg elseif mode == 3 then G.debug = true end
  sync()
  -- The global `debug` retail falls back to, through the function's environment (the file is untouched).
  local global_debug = real_debug
  if mode == 1 then global_debug = fakedbg elseif mode == 2 then global_debug = nil end
  setfenv(M.new, setmetatable({}, {__index = function(_, name)
    if name == 'debug' then return global_debug end
    return _G[name]
  end}))
  local ok, value = pcall(M.new, not a[2] and controller or nil, not a[3] and F.log or nil, G)
  compat = ok and value or nil
  return ok, value
end
function F.dispatch(call, a)
  depth = 0
  if call == 'new' then
    local ok, value = fresh(a)
    if not ok then return 'new raised ' .. F.show(value) end
    return 'new | ' .. state()
  end
  sync()
  local out
  if call == 'foreign' or call == 'prepare' or call == 'stop' or call == 'shutdown' or call == 'update' then
    local r1, r2 = compat[call](compat)
    out = F.show(r1) .. ' ' .. F.show(r2)
  elseif call == 'is_clean' then out = F.show(compat:is_clean())
  elseif call == 'result' then
    local r = compat:result()
    out = table.concat({r.phase, F.show(r.installed), F.show(r.blocking), F.show(r.foreign), F.show(r.foreign_reason), F.show(r.cleanup_pending),
      F.show(r.error_message), F.show(r.closing)}, ' ')
  elseif call == 'status' then out = F.show(compat:status())
  elseif call == 'shim' then out = F.show(assert(shims[a[1]])())          -- a[1]: the serial
  else error(call) end
  return out .. ' | ' .. state()
end

local function set(slot, v, at) return {slot = slot, v = v, at = at} end
local function key(slot, k, v, at) return {slot = slot, key = k, v = v, at = at} end
local function join(...)
  local out = {}
  for _, list in ipairs({...}) do for _, e in ipairs(list) do out[#out + 1] = e end end
  return out
end
local function quiet_setter(sid, held_reader)
  return {key('rd', sid, held_reader), key('up_pos', sid, nil), key('up_mode', sid, nil), key('set_install', sid, nil), key('set_calls', sid, nil),
    key('set_raise', sid, nil), key('set_ret', sid, nil), key('set_issue', sid, nil)}
end
local function base() return {set('dbg_fn', true), set('c_enabled', true), set('c_gone', nil)} end
-- Photo's legacy bridge: table `tid` with setter `sid` holding `held_reader`.
local function legacy(tid, sid, held_reader)
  return join(base(), {set('g_own', nil), set('g_api', tid), key('t_held', tid, false), key('t_avail', tid, true), key('t_dis', tid, nil),
    key('t_set', tid, sid)}, quiet_setter(sid, held_reader))
end
-- Photo's camera-ownership table.
local function modern(tid)
  return join(base(), {set('g_own', tid), key('t_held', tid, false), key('t_avail', tid, true), key('t_dis', tid, nil)})
end
local function absent() return join(base(), {set('g_own', nil), set('g_api', nil), set('pkg', nil)}) end
local op = F.op
local function all()                                   -- every reading call
  op('foreign'); op('is_clean'); op('result'); op('status')
end

-- Hand-written. The life of the legacy reader chain (retail's own first tests).
F.run('legacy_life', function()
  op('new')
  op('stop', nil, join(legacy(1, 1, 1), {key('r_beh', 1, false)})); all()
  op('prepare'); all(); op('shim', {1})
  op('shim', {1}, {set('c_enabled', false)}); op('update'); all()       -- FPV off: the chain is restored
  op('update', nil, {set('c_enabled', true)}); op('update'); op('prepare'); op('shim', {2}); op('shim', {1})
  op('foreign', nil, {key('t_held', 1, true)}); op('shim', {2}); op('result'); op('status'); op('prepare'); op('update'); op('update'); op('shim', {2})
  op('update', nil, {key('t_held', 1, false)}); op('shim', {2}); op('result'); op('status')
  op('shutdown'); all(); op('prepare'); op('update'); op('stop'); op('shutdown'); op('shim', {2})
end)
F.run('legacy_no_previous', function()
  op('new'); op('prepare', nil, legacy(2, 3, nil)); op('shim', {1}); op('result'); op('stop'); all(); op('shim', {1})
  op('update'); op('update', nil, {key('t_avail', 2, nil)}); op('update', nil, {key('t_avail', 2, 7)}); op('stop')
end)
F.run('held_before_install', function()
  op('new'); op('foreign', nil, join(legacy(1, 1, 1), {key('r_beh', 1, false), key('t_held', 1, true)}))
  op('prepare'); op('update'); op('result'); op('is_clean'); op('update', nil, {key('t_held', 1, false)}); op('shim', {1})
  op('prepare', nil, {key('t_held', 1, true)}); op('foreign'); op('update'); op('stop'); op('stop')
  op('update', nil, {key('t_held', 1, 5)}); op('update', nil, {key('t_held', 1, nil)}); op('update', nil, {key('t_held', 1, 'yes')})
end)
-- What the older reader answers, and readers that call back into the chain.
F.run('previous_readers', function()
  op('new'); op('prepare', nil, join(legacy(1, 1, 1), {key('r_beh', 1, false)}))
  for _, beh in ipairs({true, 'previous reader failed', 0, false}) do
    op('shim', {1}, {key('r_beh', 1, beh)}); op('shim', {1}, {key('t_held', 1, true)}); op('shim', {1}, {key('t_held', 1, false)})
  end
  op('shim', {1}, {key('r_beh', 1, nil)})
  op('shim', {1}, {key('r_beh', 1, 1)})                                 -- the older reader calls the chain again
  op('shim', {1}, {key('r_beh', 1, 2), key('rd', 2, 2), key('r_beh', 2, 2)})   -- two foreign readers in a loop
  op('shim', {1}, {key('r_beh', 2, 'deep failure')}); op('shim', {1}, {key('r_beh', 2, true)}); op('shim', {1}, {key('r_beh', 2, false)})
  op('shim', {1}, {key('rd', 2, nil)}); op('shim', {1}, {set('x_previous', 'raised by the host')})
  op('shim', {1}, {key('r_beh', 1, false), set('c_enabled', nil)}); op('shim', {1}, {set('c_enabled', 3)})
  op('shim', {1}, {set('c_enabled', false), set('x_is_enabled', 'fixture controller error')}); op('shim', {1}); op('shim', {1}, {set('c_gone', true)})
  op('result'); op('update'); op('update', nil, {set('c_gone', nil)}); op('shim', {1})
  op('update', nil, {set('c_enabled', true)}); op('stop'); op('shim', {2}); op('shim', {1})
  -- An old shim comes back as the older reader of a new chain.
  op('prepare', nil, {key('rd', 1, 1001)}); op('shim', {3}); op('shim', {3}, {key('r_beh', 1, true)}); op('shim', {1}); op('stop'); op('result')
end)
-- A third party replaces the reader, the setter or the provider.
F.run('replaced', function()
  op('new'); op('update', nil, join(legacy(1, 1, 1), {key('r_beh', 1, false), key('r_beh', 2, 1)}))
  op('foreign', nil, {key('rd', 1, 2)}); op('result'); op('status'); op('update'); op('update'); op('prepare'); op('shim', {1}); op('foreign')
  op('stop'); all(); op('prepare'); op('shim', {2}); op('stop'); op('result')
  op('update'); op('foreign', nil, join(legacy(2, 2, nil))); op('update'); op('result'); op('stop'); op('result'); op('update'); op('shim', {4})
  op('foreign', nil, {key('t_set', 2, 3)}); op('prepare'); op('stop'); op('update', nil, {key('t_set', 2, 2)})
  op('update', nil, {set('g_own', 3), key('t_held', 3, false), key('t_avail', 3, true)}); op('result'); op('stop'); op('update'); op('result')
  op('update', nil, {set('g_own', nil)}); op('update', nil, {set('g_api', nil)}); op('stop'); op('update'); op('foreign')
end)
-- The setter refuses, raises, stores something else; cleanup stays pending until a readback proves it.
F.run('setter', function()
  local function try(events, cleanup)
    op('prepare', nil, join(legacy(1, 1, 1), events)); all(); op('update'); op('prepare')
    if cleanup then op('stop', nil, cleanup); op('result'); op('update'); op('foreign') end
    op('stop', nil, legacy(1, 1, 1)); op('result')
  end
  op('new')
  try({key('set_install', 1, false), key('set_ret', 1, false)})
  try({key('set_install', 1, false), key('set_ret', 1, false), key('set_issue', 1, 'Photo is busy')})
  try({key('set_install', 1, false), key('set_ret', 1, false), key('set_issue', 1, '')})
  try({key('set_install', 1, false), key('set_ret', 1, false), key('set_issue', 1, false)})
  try({key('set_install', 1, false)}); try({key('set_install', 1, false), key('set_ret', 1, true), key('set_issue', 1, 'ignored unless it fails')})
  try({key('set_ret', 1, false)}, {key('set_ret', 1, nil), key('set_install', 1, false)})
  try({key('set_ret', 1, false), key('set_issue', 1, 'accepted but says no')}, {key('set_install', 1, false), key('set_issue', 1, 'still busy')})
  try({key('set_ret', 1, true), key('set_issue', 1, 'fine')}); try({key('set_ret', 1, 0)})
  try({key('set_install', 1, false), key('set_raise', 1, 'fixture setter before change')})
  try({key('set_raise', 1, 'fixture setter after change')}, {key('set_raise', 1, 'fixture setter before change'), key('set_install', 1, false)})
  try({key('set_raise', 1, 'fixture setter after change')}, {key('set_raise', 1, 'restored, then failed')})
  try({set('x_set', 'raised by the host')}); try({key('set_install', 1, 2)}); try({key('set_install', 1, true)})
  try({key('set_calls', 1, true), key('r_beh', 1, false)}); try({key('set_calls', 1, true), key('r_beh', 1, 1)})
  op('prepare'); op('stop', nil, {set('x_set', 'raised by the host')}); op('stop', nil, {key('set_install', 1, 2)}); op('prepare')
  op('stop', nil, {key('set_install', 1, false)}); op('stop', nil, {key('set_install', 1, nil), set('x_getupvalue', 'no')}); op('result'); op('status')
  op('stop', nil, {key('set_install', 1, false), set('x_getupvalue', 'no', 3)}); op('stop', nil, {key('set_calls', 1, true)}); op('stop')
  op('prepare'); op('shutdown', nil, {key('set_raise', 1, 'late'), key('set_install', 1, false)}); all(); op('prepare'); op('update'); op('shutdown', nil, {key('set_raise', 1, nil), key('set_install', 1, nil)})
  all()
end)
-- What debug.getupvalue can say about the setter.
F.run('reader', function()
  local function try(events)
    op('foreign', nil, join(legacy(1, 1, 1), events)); op('prepare'); op('result'); op('update'); op('stop'); op('stop')
  end
  op('new')
  try({set('dbg_fn', nil)}); try({key('up_mode', 1, 1)}); try({key('up_mode', 1, 2)}); try({key('up_pos', 1, 2)}); try({key('up_pos', 1, 32)})
  try({key('up_pos', 1, 33)}); try({key('up_pos', 1, 5), key('up_mode', 1, 1)}); try({key('up_pos', 1, 4), key('up_mode', 1, 2)})
  try({key('rd', 1, true)}); try({key('rd', 1, false)}); try({key('rd', 1, 'text')}); try({set('x_getupvalue', 'raised by the host')})
  -- The same, once the chain is installed.
  for _, e in ipairs({set('dbg_fn', nil), key('up_mode', 1, 1), key('up_mode', 1, 2), key('up_pos', 1, 33), key('rd', 1, true), key('rd', 1, nil),
    set('x_getupvalue', 'raised by the host')}) do
    op('prepare', nil, legacy(1, 1, 1)); op('foreign', nil, {e}); op('result'); op('update'); op('stop'); op('result'); op('stop', nil, legacy(1, 1, 1))
    op('prepare'); op('stop', nil, {e}); op('result'); op('status'); op('update'); op('stop', nil, legacy(1, 1, 1)); op('stop', nil, legacy(1, 1, 1))
  end
end)
for mode = 1, 3 do
  F.run('debug_source' .. mode, function()
    op('new', {mode}); op('foreign', nil, legacy(1, 1, nil)); op('prepare'); all(); op('update'); op('stop'); op('update', nil, modern(2)); all()
    op('update', nil, absent()); all()
  end)
end
-- Photo's own camera lease is only observed.
F.run('modern', function()
  op('new'); op('foreign', nil, join(modern(1), {set('g_api', 2), set('pkg', 1), set('pkg_loaded', 1), set('actioncam', true), set('dbg_fn', nil)}))
  op('prepare'); all(); op('update'); op('update'); all()
  op('foreign', nil, {key('t_held', 1, true)}); op('prepare'); op('update'); op('result'); op('status'); op('update', nil, {key('t_held', 1, false)}); op('prepare')
  op('update', nil, {set('c_enabled', false)}); op('stop'); op('shutdown'); all()
  op('new'); op('update', nil, modern(1))
  for _, e in ipairs({key('t_held', 1, nil), key('t_held', 1, 1), key('t_held', 1, 'true'), key('t_avail', 1, nil), key('t_avail', 1, false), key('t_avail', 1, 1),
    key('t_dis', 1, true), set('g_own', true), set('g_own', false)}) do
    op('foreign', nil, {e}); op('prepare'); op('update'); op('result'); op('stop'); op('update', nil, modern(1)); op('result')
  end
  op('update', nil, {key('t_dis', 1, false)}); op('update', nil, {key('t_dis', 1, 1)}); op('update', nil, {key('t_dis', 1, 'true')})
end)
-- No Photo; ActionCam with and without its bridge; bridges that are not what they should be.
F.run('absent_and_malformed', function()
  op('new'); op('foreign', nil, absent()); op('prepare'); all(); op('update'); all(); op('stop')
  for _, events in ipairs({{set('pkg', true)}, {set('pkg', 1)}, {set('pkg_loaded', true)}, {set('pkg_loaded', 1)}, {set('actioncam', false)}, {set('actioncam', true)},
    {set('actioncam', 0)}, {set('actioncam', 'loaded')}}) do
    op('foreign', nil, events); op('prepare'); op('update'); op('result'); op('status'); op('stop')
  end
  op('update', nil, {set('actioncam', nil)}); op('result')
  for _, events in ipairs({{set('g_api', true)}, {set('g_api', false)}, {set('g_api', 1)}, {key('t_set', 1, true)}, {key('t_set', 1, 1)},
    {key('t_held', 1, 'false')}, {key('t_held', 1, false), key('t_dis', 1, true)}, {key('t_dis', 1, 1), key('t_avail', 1, false)},
    {key('t_avail', 1, 0)}}) do
    op('foreign', nil, events); op('prepare'); op('update'); op('result'); op('status'); op('stop')
  end
  op('update'); op('result'); op('stop')
end)
-- The controller, the logger and construction.
F.run('construction', function()
  op('new', {0, true}); op('new', {0, nil, true}, {set('c_gone', true)}); op('new', {0, nil, true}, join(legacy(1, 1, nil)))
  op('prepare'); op('update', nil, {key('t_set', 1, nil)}); op('stop'); op('stop', nil, legacy(1, 1, nil)); all()
  op('new'); op('update', nil, {set('c_enabled', false)}); op('update', nil, {set('c_enabled', nil)}); op('update', nil, {set('c_enabled', 0)})
  op('update', nil, {set('c_enabled', false), set('x_is_enabled', 'fixture controller error')}); op('update'); op('update', nil, {set('c_gone', true)})
  op('result'); op('status'); op('stop')
  -- The same reason is logged once; a new one again.
  op('prepare', nil, {key('t_held', 1, true)}); op('prepare'); op('update'); op('prepare', nil, {key('t_held', 1, false)}); op('prepare', nil, {key('t_held', 1, true)})
  op('prepare', nil, {key('t_dis', 1, true)}); op('prepare'); op('update'); op('foreign'); op('stop'); op('prepare'); op('stop'); op('prepare', nil, {key('t_dis', 1, nil)})
end)
-- Changes in the middle of a call.
F.run('midcall', function()
  -- The setter is gone between the two reads of prepare (lines 105 and 107): the record keeps what is
  -- there now, and a record whose setter is no function can never be cleaned up.
  for _, v in ipairs({false, true, 0, 2}) do
    if v == 0 then v = nil end
    op('new'); op('prepare', nil, join(legacy(1, 1, 1), {key('r_beh', 1, false), key('rd', 2, 2)})); op('stop')
    op('prepare', nil, {key('t_set', 1, v, 9)}); all(); op('update'); op('stop'); op('stop', nil, {key('t_set', 1, 1)}); op('result')
    op('update', nil, {key('t_set', 1, v)}); op('result'); op('stop'); op('shutdown'); op('result')
  end
  op('new'); op('stop', nil, legacy(1, 1, 1))
  for at = 1, 22 do
    op('prepare', nil, {key('t_held', 1, true, at)}); op('result'); op('stop', nil, {key('t_held', 1, false)})
    op('prepare', nil, {set('g_api', 2, at), key('t_set', 2, 1), key('t_held', 2, false)}); op('result'); op('stop', nil, {set('g_api', 1)}); op('stop')
    op('prepare', nil, {key('rd', 1, 2, at)}); op('result'); op('stop', nil, {key('rd', 1, 1)}); op('stop')
    op('prepare', nil, {set('dbg_fn', nil, at)}); op('result'); op('stop', nil, {set('dbg_fn', true)}); op('stop')
    op('prepare'); op('update', nil, {set('g_own', 3, at), key('t_held', 3, false), key('t_avail', 3, true)}); op('stop', nil, {set('g_own', nil)}); op('stop')
    op('prepare'); op('update', nil, {set('c_enabled', false, at)}); op('result', nil, {key('t_held', 1, true, at)}); op('stop', nil, {set('c_enabled', true), key('t_held', 1, false)})
    op('prepare'); op('stop', nil, {key('rd', 1, 2, at)}); op('stop', nil, {key('rd', 1, 1)}); op('stop')
    -- The provider's setter is something else by the time verify() compares it.
    for _, v in ipairs({0, true, 2}) do
      if v == 0 then v = nil end
      op('prepare'); op(at % 2 == 0 and 'foreign' or 'update', nil, {key('t_set', 1, v, at)}); op('result'); op('stop', nil, {key('t_set', 1, 1)}); op('stop')
    end
    op('prepare'); op('shim', {count}, {key('t_held', 1, true, at)}); op('shim', {count}, {key('rd', 1, nil, at), key('t_held', 1, false)}); op('stop', nil, legacy(1, 1, 1))
  end
end)

-- Seeded walks: random changes of the other mods, some landing in the middle of an operation.
local function walk(seed, steps)
  F.seed(seed)
  local texts = {'Photo is busy', 'fixture setter failure', ''}
  op('new', {F.pick({0, 0, 0, 0, 0, 1}), nil, F.chance(.1)})
  op('stop', nil, join(legacy(1, 1, 1), {key('r_beh', 1, false), key('r_beh', 2, false), key('r_beh', 3, true)}))
  local function change()
    local tid, sid, rid = F.rnd(3), F.rnd(3), F.rnd(3)
    local at = F.chance(.5) and F.rnd(F.last_calls + 3) or 0
    local n, e = F.rnd(34)
    if n == 1 then e = set('g_own', F.pick({nil, nil, nil, 3, 3, true, false}))
    elseif n == 2 then e = set('g_api', F.pick({1, 1, 1, 2, nil, true, false}))
    elseif n == 3 or n == 4 then e = key('t_held', tid, F.pick({false, false, false, true, true, nil, 1, 'held'}))
    elseif n == 5 then e = key('t_avail', tid, F.pick({true, true, true, nil, false, 1}))
    elseif n == 6 then e = key('t_dis', tid, F.pick({nil, nil, false, true, 1}))
    elseif n == 7 then e = key('t_set', tid, F.pick({1, 1, 2, 3, nil, true, false}))
    elseif n == 8 or n == 9 then e = key('rd', sid, F.pick({nil, 1, 2, 3, true, false, count > 0 and 1000 + F.rnd(count) or nil, count > 0 and 1000 + count or 1}))
    elseif n == 10 then e = key('up_pos', sid, F.pick({nil, 1, 2, 3, 32, 33}))
    elseif n == 11 then e = key('up_mode', sid, F.pick({nil, nil, 1, 2}))
    elseif n == 12 then e = key('set_install', sid, F.pick({nil, nil, false, rid, true}))
    elseif n == 13 then e = key('set_calls', sid, F.pick({nil, true}))
    elseif n == 14 then e = key('set_raise', sid, F.pick({nil, nil, texts[1], texts[2]}))
    elseif n == 15 then e = key('set_ret', sid, F.pick({nil, nil, false, false, true, 0}))
    elseif n == 16 then e = key('set_issue', sid, F.pick({nil, nil, false, texts[1], texts[2], texts[3]}))
    elseif n == 17 or n == 18 then e = key('r_beh', rid, F.pick({false, false, true, nil, 'previous reader failed', 1, 2, 3}))
    elseif n == 19 or n == 20 then e = set('c_enabled', F.pick({true, true, true, false, false, nil, 2}))
    elseif n == 21 then e = set('c_gone', F.pick({nil, nil, nil, true}))
    elseif n == 22 then e = set('dbg_fn', F.pick({nil, true, true, true}))
    elseif n == 23 then e = set('pkg', F.pick({nil, true, 1, 1}))
    elseif n == 24 then e = set('pkg_loaded', F.pick({nil, true, 1, 1}))
    elseif n == 25 then e = set('actioncam', F.pick({nil, false, true, 1}))
    elseif n == 26 then e = set(F.pick({'x_getupvalue', 'x_set', 'x_previous', 'x_is_enabled'}), F.pick({'raised by the host', 'second failure'})); at = F.rnd(F.last_calls + 3)
    elseif n <= 31 then return legacy(F.pick({1, 1, 2}), F.pick({1, 1, 2}), F.pick({nil, 1, 1, 2}))         -- everything back in order
    elseif n == 32 then return modern(3)
    elseif n == 33 then return absent()
    else return quiet_setter(sid, F.pick({nil, 1})) end
    e.at = at
    return {e}
  end
  local closed = false
  for _ = 1, steps do
    local events = {}
    for _ = 1, F.pick({0, 0, 0, 1, 1, 1, 2, 3}) do events = join(events, change()) end
    if F.chance(.12) then events[#events + 1] = set('x_any', F.pick({'raised by the host', 'second failure'}), F.rnd(F.last_calls + 2)) end
    local call = F.pick({'update', 'update', 'update', 'update', 'update', 'prepare', 'prepare', 'stop', 'stop', 'foreign', 'foreign', 'result', 'result',
      'status', 'is_clean', 'shim', 'shim', 'shim'})
    if call == 'shim' and count == 0 then call = 'update' end
    if F.chance(.004) then call = 'shutdown'; closed = true end
    -- A closed instance only ever stops; open a new one after a while.
    if closed and F.chance(.05) or F.chance(.006) then
      -- The shims of the instance left behind are gone with it.
      op('new', {F.pick({0, 0, 0, 1}), nil, F.chance(.1)}, {set('c_gone', nil), key('rd', 1, nil), key('rd', 2, nil), key('rd', 3, nil)}); closed = false
    elseif call == 'shim' then
      op('shim', {F.chance(.7) and count or F.rnd(count)}, events)
    else op(call, nil, events) end
  end
end
for seed = 1, 12 do F.run('walk' .. seed, function() walk(seed, 700) end) end
assert(not harness_error, harness_error)
F.finish()

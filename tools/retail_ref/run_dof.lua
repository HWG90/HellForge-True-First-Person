-- Drives the retail dof.lua, unmodified, under LuaJIT (see effects_common.lua).
-- Usage: luajit run_dof.lua <Goose Gadget Kit folder>
-- Fakes: the clock, the log, stingray.Application (three identities: the table, its getter, its
-- setter), the backend (renderer_dof_value, foreign_camera, profile.pose, _alignment_executable,
-- _read), the camera_compat objects given to update, deps.verify / active_view / fallback_ready,
-- the controller, and a scripted vignette module for the vignette_factory construction.
-- Fields that retail reads at call time (backend.foreign_camera, compat.foreign, ...) follow the
-- world through __index, so a change in the middle of a call is seen exactly where retail looks.
package.path = arg[0]:gsub('[^/]*$', '') .. '?.lua;' .. package.path
local F = require('effects_common')
F.slots({'mem', 'time', 'app', 'get', 'set', 'v_dof', 'v_vig', 'v_other', 'set_effect', 'set_ret',
  'rdv_has', 'rdv_mode', 'fc_has', 'fc_v', 'fc_why', 'c1_has', 'c1_v', 'c1_why', 'c2_has', 'c2_v', 'c2_why',
  'profile', 'exe', 'verify_ok', 'view', 'fallback', 'ctl_has', 'c_enabled', 'c_pose', 'c_running',
  'vg_ret', 'vg_why', 'vg_clean', 'vg_owned', 'vg_cp', 'vg_cmd', 'vg_status',
  'x_clock', 'x_render_setting', 'x_set_render_setting', 'x_foreign_camera', 'x_compat_foreign',
  'x_alignment_executable', 'x_read', 'x_verify', 'x_active_view', 'x_fallback_ready',
  'x_vignette_update', 'x_vignette_stop', 'x_vignette_shutdown', 'x_renderer_dof_value', 'x_any'})
local function g(name) return F.world[name] end
local VAL = {dof_enabled = 'v_dof', vignette_enabled = 'v_vig', other_enabled = 'v_other'}

-- stingray.Application: three table identities, three getters, three setters. Reading
-- sr.Application is the host call `application_binding`; the table's two fields are set from the
-- world at that moment (retail reads them right after, with no host call in between).
local apps, getters, setters, app_id, get_id, set_id = {}, {}, {}, {}, {}, {}
for i = 1, 3 do
  apps[i] = {}; app_id[apps[i]] = i
  getters[i] = function(key)
    F.tick('render_setting', i .. ' ' .. tostring(key))
    return g(VAL[key])
  end
  get_id[getters[i]] = i
  setters[i] = function(key, value)
    F.tick('set_render_setting', i .. ' ' .. tostring(key) .. ' ' .. tostring(value))
    local effect = g('set_effect') or 0
    if effect ~= 1 then F.poke(VAL[key], nil, value == 'true') end
    if effect == 2 then error('setter after queue', 0) end
    if effect == 3 then error() end                        -- the error value nil
    return g('set_ret')
  end
  set_id[setters[i]] = i
end
local sr = setmetatable({}, {__index = function(_, k)
  if k ~= 'Application' then return nil end
  F.tick('application_binding')
  local app = apps[g('app') or 0]
  if app then app.render_setting = getters[g('get') or 0]; app.set_render_setting = setters[g('set') or 0] end
  return app
end})
local function ident(map, v) return v == nil and '~' or tostring(map[v] or '?') end

local POSE = {}
local function foreign_camera(target)
  F.tick('foreign_camera', target == nil and '' or '?')
  return g('fc_v'), g('fc_why')
end
local function renderer_dof_value(_, getter, setter, key)
  F.tick('renderer_dof_value', ident(get_id, getter) .. ' ' .. ident(set_id, setter) .. ' ' .. tostring(key))
  local mode = g('rdv_mode') or 0
  if mode == 1 then return true elseif mode == 2 then return false elseif mode == 3 then return nil end
  return g(VAL[key])
end
local function alignment_executable(_, profile)
  F.tick('alignment_executable', profile == POSE and 'pose' or '?')
  return g('exe')
end
local function read(_, address, size)
  F.tick('read', F.num(address) .. ' ' .. F.num(size))
  local bytes = F.read(address, size)
  if not bytes then error('Memory unavailable', 0) end
  return bytes
end
local backend = setmetatable({}, {__index = function(_, k)
  if k == 'foreign_camera' then return g('fc_has') and foreign_camera or nil end
  if k == 'renderer_dof_value' then return g('rdv_has') and renderer_dof_value or nil end
  if k == 'profile' then
    local p = g('profile')
    if p == true then return {pose = POSE} elseif p == 1 then return {} end
    return nil
  end
  if k == '_alignment_executable' then return alignment_executable end
  if k == '_read' then return read end
end})

local compats, compat_fn = {}, {}
for i = 1, 2 do
  compat_fn[i] = function(target)
    F.tick('compat_foreign', i .. (target == compats[i] and '' or '?'))
    return g('c' .. i .. '_v'), g('c' .. i .. '_why')
  end
  compats[i] = setmetatable({}, {__index = function(_, k)
    if k == 'foreign' and g('c' .. i .. '_has') then return compat_fn[i] end
  end})
end
local function compat_name(c)
  if c == nil then return '~' elseif c == false then return 'X' end
  return c == compats[1] and '1' or c == compats[2] and '2' or '?'
end

local controller = setmetatable({}, {__index = function(_, k)
  if k == 'is_enabled' and g('ctl_has') then return function() F.tick('is_enabled'); return g('c_enabled') end end
  if k == 'result' then
    return function()
      F.tick('result')
      return {mode = g('c_pose') and 'pose_offset' or 'other', phase = g('c_running') and 'running' or 'waiting'}
    end
  end
end})
local CAMERA = {}

-- The vignette module of the vignette_factory construction: every answer is scripted.
local function vignette_factory(b, _, s, options)
  assert(b == backend and s == sr and options.setting == 'vignette_enabled', 'factory arguments')
  local v = {}
  local function answer(name, args)
    F.tick(name, args)
    return g('vg_ret'), g('vg_why')
  end
  function v:update(_, _, compat) return answer('vignette_update', compat_name(compat)) end
  function v:stop() return answer('vignette_stop') end
  function v:shutdown() return answer('vignette_shutdown') end
  function v:is_clean() F.tick('vignette_is_clean'); return g('vg_clean') == true end
  function v:result() F.tick('vignette_result'); return {owned = g('vg_owned'), cleanup_pending = g('vg_cp'), command_pending = g('vg_cmd')} end
  function v:status() F.tick('vignette_status'); return g('vg_status') or 'v' end
  return v
end

local Dof = F.load('dof.lua')
local lease, mode, dof_setting, vignette_setting

-- Every upvalue of the closures of one Setting, by name (reading only).
local function upvalues(object)
  local out, seen, queue = {}, {}, {}
  for _, f in pairs(object) do queue[#queue + 1] = f end
  while #queue > 0 do
    local f = table.remove(queue)
    if type(f) == 'function' and not seen[f] and debug.getinfo(f, 'S').source:sub(-8) == '/dof.lua' then
      seen[f] = true
      for i = 1, 200 do
        local name, value = debug.getupvalue(f, i)
        if not name then break end
        if not out[name] then out[name] = {value} end
        queue[#queue + 1] = value
      end
    end
  end
  return function(name) return assert(out[name], name)[1] end
end
local function binding(c)
  if not c then return '~' end
  return ident(app_id, c.app) .. '.' .. ident(get_id, c.get) .. '.' .. ident(set_id, c.set)
end
local function time(v) return v == nil and '~' or F.num(v) end
local function setting_state(object)
  if not object then return '-' end
  local u = upvalues(object)
  local r = u('record')
  return table.concat({u('phase'), F.show(u('message')), F.show(u('closing')), F.show(u('session')), F.show(u('preserved')), F.show(u('yielded')),
    time(u('next_attempt')), time(u('last_time')), F.num(u('reads')), F.num(u('writes')), F.num(u('restores')), F.num(u('foreign_changes')),
    F.num(u('cached_reads')), F.num(u('cache_fallbacks')), F.num(u('reassertions')), compat_name(u('compat')), binding(u('verified')),
    r and table.concat({binding(r), F.show(r.before), F.show(r.cancel), time(r.sent), r.stage or '~', time(r.next_poll), F.show(r.issue)}, ' ') or '~'}, ' ')
end
local function setting_result(r)
  return table.concat({r.phase, F.show(r.owned), F.show(r.cleanup_pending), F.show(r.command_pending), F.show(r.disabled), F.num(r.reads), F.num(r.writes),
    F.num(r.restores), F.num(r.foreign_changes), F.num(r.cached_reads), F.num(r.cache_fallbacks), F.num(r.reassertions), F.show(r.error_message),
    F.show(r.closing)}, ' ')
end

-- new: a[1] construction (0 suppress_vignette=false, 1 vignette_factory, 2 a second Setting),
-- a[2] flags (1 active_view, 2 fallback_ready, 4 verify, 8 no log), a[3] deps.setting.
local function has(flags, bit) return math.floor(flags / bit) % 2 == 1 end
local function fresh(a)
  mode = a[1]
  local flags = a[2] or 0
  local deps = {clock = function() F.tick('clock'); return g('time') end, setting = a[3]}
  if mode == 0 then deps.suppress_vignette = false elseif mode == 1 then deps.vignette_factory = vignette_factory end
  if has(flags, 1) then deps.active_view = function() F.tick('active_view'); return g('view') end end
  if has(flags, 2) then deps.fallback_ready = function() F.tick('fallback_ready'); return g('fallback') end end
  if has(flags, 4) then deps.verify = function(app) F.tick('verify', ident(app_id, app)); return g('verify_ok') end end
  lease = Dof.new(backend, not has(flags, 8) and F.log or nil, sr, deps)
  dof_setting, vignette_setting = lease, nil
  if mode ~= 0 then
    local u = upvalues({lease.update})
    dof_setting = u('dof')
    if mode == 2 then vignette_setting = u('vignette') end
  end
end
function F.dispatch(call, a)
  local out
  if call == 'new' then fresh(a); out = 'new'
  else
    local ok, r1, r2
    if call == 'update' then       -- a[1]: the camera_compat argument (1, 2, 3 = false, else nil); a[2]: no controller
      local compat = nil
      if a[1] == 1 or a[1] == 2 then compat = compats[a[1]] elseif a[1] == 3 then compat = false end
      ok, r1, r2 = pcall(lease.update, lease, not a[2] and controller or nil, CAMERA, compat)
    elseif call == 'stop' or call == 'shutdown' then ok, r1, r2 = pcall(lease[call], lease)
    else error(call) end
    out = ok and F.show(r1) .. ' ' .. F.show(r2) or 'raised ' .. F.show(r1)
  end
  local state = setting_state(dof_setting) .. ' | ' .. setting_state(vignette_setting)
  local r = lease:result()
  local result = setting_result(r)
  if mode == 1 then result = result .. ' V ' .. F.show(r.vignette.owned) .. ' ' .. F.show(r.vignette.cleanup_pending) .. ' ' .. F.show(r.vignette.command_pending)
  elseif mode == 2 then result = result .. ' V ' .. setting_result(r.vignette) end
  return table.concat({out, state, result, F.str(lease:status()), F.show(lease:is_clean()), F.show(lease:is_suppressed())}, ' | ')
end

local EXE = 0x140000000
local GET, SET = '\072\139\209\233\008\253\255\255', '\072\139\209\233\136\250\255\255'
local function set(slot, v, at) return {slot = slot, v = v, at = at} end
local function mem(at, bytes) return {slot = 'mem', key = at, v = bytes} end
local function join(...)
  local out = {}
  for _, list in ipairs({...}) do for _, e in ipairs(list) do out[#out + 1] = e end end
  return out
end
local function order()   -- everything but the clock and the setting values back in order
  return {set('app', 1), set('get', 1), set('set', 1), set('set_effect', 0), set('set_ret', nil), set('rdv_has', true), set('rdv_mode', 0),
    set('fc_has', true), set('fc_v', false), set('fc_why', nil), set('c1_has', true), set('c1_v', false), set('c1_why', nil),
    set('c2_has', true), set('c2_v', false), set('c2_why', nil), set('profile', true), set('exe', EXE),
    mem(EXE + 0x3d43d0, GET), mem(EXE + 0x3d3ff0, SET), set('verify_ok', true), set('view', true), set('fallback', false), set('ctl_has', true),
    set('c_enabled', true), set('c_pose', true), set('c_running', true), set('vg_ret', true), set('vg_why', nil), set('vg_clean', true),
    set('vg_owned', false), set('vg_cp', false), set('vg_cmd', nil), set('vg_status', 'idle; environment vignette disabled=false; profiles=0; ')}
end
local function game(t)
  return join({set('time', t), set('v_dof', true), set('v_vig', true), set('v_other', true)}, order())
end

-- Hand-written: the life of the lease, in sections that each start from a fresh instance and a
-- world in order. `now` is the scripted clock.
local now, cfg, v
local function flat(list, out)
  out = out or {}
  for _, e in ipairs(list or {}) do
    if e.slot then out[#out + 1] = e else flat(e, out) end
  end
  return out
end
local function up(dt, events, compat, no_controller)
  now = now + (dt or 0)
  return F.op('update', {compat == nil and 1 or compat, no_controller}, join({set('time', now)}, flat(events)))
end
local function stop(dt, events, call)
  now = now + (dt or 0)
  return F.op(call or 'stop', nil, join({set('time', now)}, flat(events)))
end
-- The value of the leased setting (with a second Setting: of both).
local function val(x, at)
  if cfg[1] == 2 and v ~= 'v_vig' then return {set(v, x, at), set('v_vig', x, at)} end
  return {set(v, x, at)}
end
local function begin(acquire)
  now = now + 1.25
  F.op('new', {cfg[1], cfg[2], cfg[3]}, game(now))
  if acquire ~= false then up(0); up(.5) end                       -- owned and acknowledged
end
local function away(x) return {set('view', not x), set('c_enabled', not x)} end
local function session(m, flags, key)
  cfg, v, now = {m, flags, key}, VAL[key or 'dof_enabled'], 100
  begin(false); stop(0)
  up(0); up(0); up(.25); up(.25); up(.5)                           -- acquire, quiet polls, cached monitor polls
  up(.5, {set('rdv_mode', 3)}); up(.5, {set('rdv_has', false)}); up(.5, {set('rdv_has', true), set('x_renderer_dof_value', 'observer raised')})
  up(.5, {set('rdv_mode', 0), val(true)})                          -- the game switched it back on: reassert
  up(.5, {val(true), set('rdv_mode', 2)}); up(.5, {set('rdv_mode', 1), val(false)}); up(.5, {set('rdv_mode', 0)})
  up(.1, away(true)); up(.1); up(.5); up(.5, away(false))           -- leave the view: restore
  up(.5); up(.5, {set('set_effect', 1)}); up(.2); up(.5); up(1); up(1); up(.6); up(.5, val(false))    -- delayed acknowledgement
  up(.1, {away(true), set('c_pose', false)}); up(.5); up(1); up(2); up(.5, val(true)); up(.5, {set('set_effect', 0)})
  up(.5, {away(false), set('c_pose', true)}); up(.5)
  stop(.1); stop(.1); stop(.5); up(.5); stop(0, {set('set_effect', 1)}); stop(.2); stop(.5); stop(2.5); stop(.5, {val(true), set('set_effect', 0)})
  up(.5, val(false)); up(.2); up(.5); up(.5, val(true)); up(.5)     -- already disabled, then enabled again
  up(.5, {val(false), away(true)}); up(.5, away(false))
  up(.5, val(false)); up(.5, {val(true), set('rdv_mode', 2)}); up(.5, {val(false), set('rdv_mode', 1)}); up(.5, {set('rdv_mode', 0), val(true)})
  stop(.5); up(.5); stop(.5); stop(.5)
  up(.5, val(false)); up(.5, {set('rdv_has', false)}); up(.5, val(true)); up(.5); stop(.5); stop(.5)
  -- Another camera.
  begin()
  up(.5, {set('c1_v', true), set('c1_why', 'Photo Mode owns the camera')}); up(.5); stop(.5); up(.5, {set('c1_why', nil)}); up(.1, {set('c1_why', '')})
  up(.5, {set('c1_v', false), set('c1_why', nil)}); up(.5); up(.5)
  up(.5, {set('c1_v', nil)}); up(.5, {set('c1_v', 7)}); up(.5, {set('c1_v', 'text value')}); up(.5, {set('x_compat_foreign', 'compat raised')})
  up(.5, {set('c1_v', false)}); up(.5)
  up(.5, {set('c1_v', true)}); up(.5, {set('c1_why', 'held')}); up(.5, {set('c1_why', 'held again')}); up(.5, {set('c1_why', false)})
  up(.5, {set('c1_v', false), set('c1_why', 'ignored')}); up(.5)
  up(.5, {set('c1_has', false)}); up(.5, {set('fc_v', true)}); up(.5, {set('fc_why', 'backend says so')}); up(.5, {set('fc_has', false)}); up(.5)
  up(.5, {set('fc_has', true), set('x_foreign_camera', 'backend raised')}); up(.5, {set('fc_v', false)}, 0); up(.5, nil, 3); up(.5, {set('fc_v', true)}, 0)
  up(.5, {set('c2_v', true), set('c2_why', 'second')}, 2); up(.5, nil, 0); up(.5, {set('fc_v', false)}, 1); up(.5, {set('c1_has', true)}, 0); up(.5)
  begin(false)                                                     -- held from the start, each kind of answer
  up(0, {set('c1_v', true)}); up(.5, {set('c1_why', 'Photo Mode owns the camera')}); up(.5, {set('c1_why', nil), set('c1_v', nil)}); stop(.5)
  up(.5, {set('x_compat_foreign', 'compat raised')}); up(.5, {set('c1_has', false), set('fc_v', 12.5)}); up(.5, {set('fc_has', false)}); stop(.5); stop(.5)
  begin(false); up(0, nil, 0); up(.5, {set('fc_v', true)}, 0); up(.5, {set('fc_v', false)}, 3); up(.5, nil, 2); up(.5, {set('c2_v', true)}, 0); stop(.5, {set('c2_v', false)}); stop(.5)
  for at = 1, 14 do                                                -- the camera takes over in the middle of a call
    begin(false); up(0, {set('c1_v', true, at), set('c1_why', 'mid', at)}); up(.5); up(.5, {set('c1_v', false), set('c1_why', nil)}); up(.5); stop(.5); stop(.5)
    begin(); up(.5, {val(true), set('c1_v', true, at)}); up(.5); up(.5, {set('c1_v', false)}); up(.5); stop(.5); stop(.5)
    begin(); stop(.1, {set('c1_v', true, at)}); stop(.5); stop(.5, {set('c1_v', false)}); stop(.5)
    begin(); up(.5, {away(true), set('c1_v', true, at)}); up(.5, {set('c1_v', false, at)}); up(.5); up(.5)
  end
  -- The setter.
  begin(false)
  up(0, {set('set_ret', false)}); up(.5); up(.5); up(.5, {set('set_ret', nil)}); up(.5); up(.5)
  stop(.5); up(.5, {set('set_ret', true)}); stop(.5, {set('set_ret', false)}); stop(.5); stop(.5, {set('set_ret', nil)}); stop(.5)
  up(.5, {set('x_set_render_setting', 'setter before queue')}); up(.5); up(.5); up(.5)
  stop(.5); stop(.5); up(.5, {set('set_effect', 2)}); up(.5); up(.5, {set('set_effect', 0)}); up(.5); up(.5); stop(.5); stop(.5)
  up(.5, {set('set_effect', 1), set('set_ret', false)}); up(.5); up(3); up(.5, val(false)); up(.5); up(.5, {set('set_effect', 0), set('set_ret', nil)}); up(.5)
  up(.5); stop(.5); stop(.5); up(.5, {set('set_effect', 3)}); up(.5); up(.5, {set('set_effect', 0)}); up(.5); stop(.5); stop(.5)
  begin(); stop(.1, {set('x_set_render_setting', 'setter before queue')}); stop(.5); stop(.5, val(true)); stop(.5)
  begin(); stop(.1, {set('set_effect', 2)}); stop(.5); stop(.5, {set('set_effect', 0)}); stop(.5)
  begin(); stop(.1, {set('set_effect', 1)}); stop(.5); stop(1.6); stop(.5); stop(.5, val(true)); stop(.5)
  begin(false); up(0, {set('set_effect', 1)}); up(.5); up(1.6); up(.5); stop(.5); up(.5, val(false)); up(.5); up(.5, {set('set_effect', 0)}); up(.5, val(true)); up(.5)
  begin(false); up(0, {set('set_effect', 1)}); up(.5); up(2); up(.01); up(.5, val(false)); up(.5); stop(.5, {set('set_effect', 0)}); stop(.5)
  -- The getter.
  begin(false)
  up(0, val(1)); up(.5); up(.5, val(nil)); up(.5, val(true)); up(.5, {val(3), set('rdv_has', false)}); up(.5); up(.5, val(false))
  up(.5, {set('x_render_setting', 'getter raised')}); up(.5, {set('rdv_has', true)}); up(.5, val(true)); stop(.5); stop(.5)
  up(.5, {set('x_render_setting', 'getter raised')}); up(.5); up(.5, val(1, 5)); up(.5, val(true)); up(.5); up(.5); stop(.5); stop(.5)
  for at = 5, 20 do
    begin(false); up(0, {set('x_render_setting', 'getter raised', at)}); up(.5); up(.5); stop(.5); stop(.5, val(true)); stop(.5)
    begin(false); up(0, val(false, at)); up(.5, val(true, at)); up(.5); up(.5, val(false, at - 4)); up(.5); stop(.5); stop(.5, val(true)); stop(.5)
    begin(); up(.5, {set('x_render_setting', 'getter raised', at - 4)}); up(.5); stop(.5, {set('x_render_setting', 'getter raised', at - 4)}); stop(.5); stop(.5)
  end
  -- The binding.
  begin(false)
  up(0, {set('app', nil)}); up(.1); up(.5); up(.5, {set('app', 1), set('get', nil)}); up(.5, {set('get', 1), set('set', nil)}); up(.5, {set('set', 1)}); up(.5)
  for _, slot in ipairs({'app', 'get', 'set'}) do
    begin(); up(.5, {set(slot, 2)}); up(.5); stop(.2); stop(.5); up(.5, {set(slot, nil)}); stop(.5); up(.5, {set(slot, 1)}); up(.5); stop(.5); stop(.5); up(.5)
    stop(.5, {set(slot, 2)}); stop(.5, {set(slot, 1)}); stop(.5); up(.5); up(.5)
    begin(false); up(0, val(false)); up(.5, {set(slot, 2)}); up(.5, val(true)); up(.5); stop(.5); stop(.5); up(.5, {set(slot, 1)}); up(.5); stop(.5); stop(.5)
    begin(false); up(0, {set(slot, 3)}); up(.5); up(.5, {set(slot, 1)}); stop(.5, {set(slot, 3)}); stop(.5)
  end
  for at = 1, 16 do                                                -- the binding changes in the middle of a call
    for _, slot in ipairs({'get', 'app'}) do
      local other = slot == 'get' and 2 or nil
      begin(false); up(0, {set(slot, other, at)}); up(.5); up(.5, {set(slot, 1)}); up(.5); stop(.5); stop(-1); stop(.5); stop(.5, {set(slot, other)}); stop(.5)
      stop(.5, {val(true), set(slot, 1)}); stop(.5); stop(-1); stop(.5, val(true)); stop(.5)
      begin(); up(.5, {set(slot, other, at)}); up(.5); up(.5, {set(slot, 1)}); up(.5); stop(.5); stop(.5)
      begin(); stop(.5, {set(slot, other, at)}); stop(.5, {set(slot, 1)}); stop(.5)
      begin(); up(.5, {val(true), set(slot, other, at)}); up(.5, {set(slot, 1)}); up(.5); stop(.5); stop(.5)
    end
  end
  -- The clock.
  begin()
  up(.5); up(0, {set('time', 0 / 0)}); up(0, {set('time', 1 / 0)}); up(0, {set('time', -1 / 0)}); up(0, {set('x_clock', 'Engine FPV clock sample invalid')})
  stop(0, {set('time', 0 / 0)}); stop(0, {set('x_clock', 'Engine FPV clock moved backwards')})
  up(0, {set('x_clock', string.rep('L', 250))})        -- a text near the 255-byte cap, whole in the log line
  up(-50); up(.1); up(.5); stop(-20); stop(.1); stop(.5)
  up(.5, {set('app', nil)}); up(.1); up(-5); up(.5, {set('app', 1)}); up(-5, val(false)); up(-1); up(.5, val(true)); up(.3); stop(.5); stop(.5)
  begin(false); up(0, {set('time', 1 / 0)}); up(0, {set('x_clock', 'Engine FPV clock sample invalid')}); stop(0, {set('time', 0 / 0)}); up(0); up(.25); up(-.125); up(.25); up(.25)
  begin(false); up(0, {set('set_effect', 1)}); up(.25); up(-3); up(.25); up(.25); up(2.25); up(-1); up(.5, val(false)); stop(.5); stop(-2, {set('set_effect', 0)}); stop(.5)
  -- The contract check.
  begin(false)
  up(0, {set('verify_ok', false), set('profile', nil)}); up(.5, {set('profile', 1)})
  up(.5, {set('profile', true), set('x_verify', 'verify raised'), set('x_alignment_executable', 'Unsupported executable build')}); up(.5, {mem(EXE + 0x3d43d0, 'X')})
  up(.5, {mem(EXE + 0x3d43d0, GET), mem(EXE + 0x3d3ff0 + 7, '\0')}); up(.5, {mem(EXE + 0x3d3ff0, 4)})
  up(.5, {mem(EXE + 0x3d3ff0, SET), set('x_read', 'Invalid read'), set('verify_ok', 1)}); up(.5, {set('exe', EXE + 4096)}); up(.2); up(.3, {set('exe', EXE)})
  up(.5, {set('verify_ok', true)}); up(.5); stop(.5); stop(.5); up(.5, {set('app', 3), set('get', 3), set('set', 3)}); up(.5, {set('app', 2)}); up(.5); stop(.5); stop(.5)
  up(.5, {set('app', 1), set('get', 1), set('set', 1)}); up(.5); stop(.5); stop(.5)
  up(.5, {set('app', 2), set('verify_ok', false), mem(EXE + 0x3d43d0 + 3, '\0')}); up(.5); up(.5, {set('app', 1)}); stop(.5); stop(.5)
  for at = 4, 9 do
    begin(false); up(0, {set('x_read', 'Invalid read', at)}); up(.5); up(.5); stop(.5)
    begin(false); up(0, {mem(EXE + 0x3d3ff0 + 2, '\0'), set('verify_ok', false, at)}); up(.5); up(.5, {mem(EXE + 0x3d3ff0, SET), set('verify_ok', true)}); stop(.5)
  end
  for i = 0, 7 do
    begin(false); up(0, {mem(EXE + 0x3d43d0 + i, '\0')}); up(.5, {mem(EXE + 0x3d43d0, GET), mem(EXE + 0x3d3ff0 + i, '\1')}); up(.5); stop(.5)
  end
  -- The view.
  begin()
  up(.5, away(true)); up(.5, {set('view', 1), set('c_enabled', true), set('c_pose', false)})
  up(.5, {set('view', true), set('c_pose', true), set('c_running', false)}); up(.5, {set('fallback', true)}); up(.5, {set('fallback', 1)}); up(.5, {set('fallback', true)})
  up(.5, {set('x_active_view', 'view raised'), set('x_fallback_ready', 'fallback raised')}); up(.5, {set('ctl_has', false)}); up(.5, {set('ctl_has', true)}); up(.5, nil, 1, true)
  up(.5); up(.5, {set('c_running', true), set('fallback', false)}); up(.5, {set('view', false, 5), set('c_enabled', false, 5)}); up(.5, away(false)); stop(.5); stop(.5)
  begin(false); up(0, {set('c_running', false), set('view', false)}); up(.5, {set('fallback', true), set('view', true)}); up(.5, {set('c_pose', false), set('view', false)}); up(.5)
  up(.5, {set('c_pose', true), set('view', true)}, 1, true); up(.5); up(.5, {set('c_enabled', false), set('view', false)}); up(.5)
  for at = 1, 12 do
    begin(false); up(0, {set('view', false, at), set('c_running', false, at)}); up(.5, {set('view', true), set('c_running', true)}); up(.5); stop(.5); stop(.5)
    begin(false); up(0, {set('x_active_view', 'view raised', at), set('x_fallback_ready', 'fallback raised', at), set('c_running', false, at)}); up(.5, {set('c_running', true)})
    stop(.5); stop(.5)
    begin(); up(.5, {set('x_active_view', 'view raised', at), set('x_fallback_ready', 'fallback raised', at), set('c_running', false, at)}); up(.5, {set('c_running', true)}); up(.5)
  end
  -- The vignette module.
  begin()
  for _, ret in ipairs({false, 'nil', true}) do
    if ret == 'nil' then ret = nil end
    up(.5, {set('vg_ret', ret), set('vg_why', 'No vignette components found'), set('vg_owned', true), set('vg_cp', true), set('vg_cmd', true), set('vg_clean', false)})
    stop(.5); up(.5, {set('vg_why', nil)}); stop(.5); stop(.5); stop(.5, {set('vg_owned', nil), set('vg_cp', nil), set('vg_cmd', false), set('vg_status', 'other')})
    up(.5, {set('c1_v', true), set('c1_why', nil)}); stop(.5, {set('c1_v', false)}); up(.5, {set('vg_why', 'second text')}); stop(.5, {set('set_effect', 1)}); stop(.5)
    stop(.5, {val(true), set('set_effect', 0), set('vg_clean', true)}); stop(.5, {set('vg_owned', false), set('vg_cp', false), set('vg_cmd', nil)}); up(.5)
  end
  up(.5, {set('x_vignette_update', 'vignette raised')}); stop(.5, {set('x_vignette_stop', 'vignette raised')}); stop(.5, {set('vg_clean', true), set('vg_cp', false), set('vg_owned', false)})
  -- Yielding to an external change while the restoration is owed.
  begin()
  up(.5); up(.5, {away(true), set('set_effect', 1)}); up(.5, val(true)); up(.5, {set('set_effect', 0)}); up(.5)
  up(.5, away(false)); up(.5); up(.5, {set('c1_v', true), val(true)}); up(.5, {set('c1_v', false)}); up(.5); up(.5)
  up(.5, {set('set_ret', false)}); up(.5, val(true)); up(.5, {val(true), set('set_ret', nil)}); up(.5); up(.5); up(.5, {set('x_any', 'everything raises')}); up(.5); up(.5)
  begin(); up(.5, {val(true), set('set_ret', false)}); up(.5, val(true)); up(.5); up(.5, {set('set_ret', nil)}); up(.5); stop(.5); stop(.5)
  begin(); up(.5, {val(true), set('rdv_mode', 2)}); up(.5, {set('rdv_mode', 1)}); up(.5, {set('rdv_mode', 3)}); up(.5, {val(false), set('rdv_mode', 0)}); up(.5); stop(.5); stop(.5)
  begin(false); up(0, val(false)); up(.5, away(true)); up(.5, away(false)); up(.5, val(true)); up(.5, val(false)); up(.5, val(true)); up(.2); up(.5); stop(.5); stop(.5)
  -- A preserved (already false) setting: the cached observer, then the getter before the write.
  begin(false); up(0, val(false)); up(.5, {set('rdv_mode', 1), val(1)}); up(.5, {val(true), set('rdv_mode', 0), set('x_render_setting', 'getter raised')})
  up(.5, {set('rdv_mode', 1), val(false)}); up(.5, {set('rdv_mode', 0), val(true)}); stop(.5); stop(.5)
  -- A new binding ends the session's memory (preserved, yielded) even when nothing else happens.
  begin(false); up(0, val(false)); up(.1, {set('app', 2)}); up(.5); up(.5, {set('app', 1)}); stop(.5)
  begin(false); up(0, val(false)); up(.5, {set('app', 2), set('verify_ok', false), mem(EXE + 0x3d43d0, 'X')}); up(.5); stop(.5)
  begin(); up(.5, val(true)); up(.5); up(.5, {set('get', 2)}); up(.5); stop(.5); stop(.5)
  -- Exactly two seconds without an acknowledgement is not yet "pending".
  now = math.floor(now) + 1
  begin(false); up(0, {set('set_effect', 1)}); up(2); up(.5); stop(.5, {set('set_effect', 0)}); stop(.5)
  -- The end.
  begin(); stop(.5, {set('x_vignette_shutdown', 'vignette raised')}, 'shutdown'); up(.5); up(.5); stop(.5); stop(.5, nil, 'shutdown'); up(.5, val(true)); up(.5)
  begin(); stop(.5, {set('set_effect', 1)}, 'shutdown'); up(.5); up(.5, val(true)); up(.5); stop(.5)
  begin(false); stop(0, nil, 'shutdown'); up(.5); up(.5, {set('c1_v', true)}); stop(.5)
end
local configurations = {
  {'shipped', 1, 3}, {'setting_only', 0, 3}, {'two_settings', 2, 3}, {'controller_view', 2, 2}, {'controller_view_no_fallback', 0, 0},
  {'verify_factory', 1, 5}, {'verify_no_log', 2, 8 + 6}, {'vignette_key_two', 2, 4, 'vignette_enabled'}, {'other_key', 0, 3, 'other_enabled'},
}
for _, c in ipairs(configurations) do F.run('session_' .. c[1], function() session(c[2], c[3], c[4]) end) end

-- Seeded walks: random world changes, some landing in the middle of an operation, and raises.
local RAISES = {'x_clock', 'x_render_setting', 'x_set_render_setting', 'x_foreign_camera', 'x_compat_foreign', 'x_alignment_executable', 'x_read',
  'x_verify', 'x_active_view', 'x_fallback_ready', 'x_vignette_update', 'x_vignette_stop', 'x_vignette_shutdown', 'x_renderer_dof_value'}
-- The binding a pending reservation was made through (so that a walk does not stay locked out of
-- its own restoration), else the first one.
local function owned_binding(at)
  local r = upvalues(dof_setting)('record') or vignette_setting and upvalues(vignette_setting)('record')
  if not r then return {set('app', 1, at), set('get', 1, at), set('set', 1, at)} end
  return {set('app', app_id[r.app], at), set('get', get_id[r.get], at), set('set', set_id[r.set], at)}
end
local function walk(seed, count, calm)
  F.seed(seed)
  local m, flags = F.rnd(3) - 1, F.pick({3, 3, 1, 2, 0, 7, 5, 6, 11})
  local key = F.pick({nil, nil, nil, 'vignette_enabled', 'other_enabled'})
  local values = {'v_dof', 'v_vig', 'v_other'}
  F.op('new', {m, flags, key})
  local t = 50 + F.rnd(100) / 8
  F.op('stop', nil, game(t))
  local function change()
    local at = F.chance(.4) and F.rnd(F.last_calls + 3) or 0
    local k, e = F.rnd(calm and 30 or 44)
    if k == 1 then e = set(F.pick(values), F.chance(.7))
    elseif k == 2 then e = set(F.pick(values), F.chance(.5))
    elseif k == 3 then e = set('view', F.chance(.7))
    elseif k == 4 then e = set('c_enabled', F.chance(.8))
    elseif k == 5 then e = set('c_pose', F.chance(.8))
    elseif k == 6 then e = set('c_running', F.chance(.6))
    elseif k == 7 then e = set('fallback', F.chance(.6))
    elseif k == 8 then e = set('c1_v', F.chance(.25))
    elseif k == 9 then e = set('fc_v', F.chance(.25))
    elseif k == 10 then e = set('set_effect', F.pick({0, 0, 0, 1, 1, 2, 3}))
    elseif k == 11 then e = set('set_ret', F.pick({'nil', 'nil', true, false}))
    elseif k == 12 then e = set('rdv_has', F.chance(.7))
    elseif k == 13 then e = set('rdv_mode', F.pick({0, 0, 0, 1, 2, 3}))
    elseif k == 14 then e = set('c1_why', F.pick({'nil', 'Photo Mode owns the camera', 'ActionCam', ''}))
    elseif k == 15 then e = set('view', true)
    elseif k == 16 then e = set('c1_v', false)
    elseif k == 17 then e = set(F.pick(values), true)
    elseif k == 18 then e = set('set_effect', 0)
    elseif k == 19 then e = set('vg_ret', F.pick({true, true, false, 'nil'}))
    elseif k == 20 then e = set('vg_why', F.pick({'nil', 'No vignette components found', 'Another camera owns the view'}))
    elseif k == 21 then e = set(F.pick({'vg_owned', 'vg_cp', 'vg_cmd'}), F.pick({true, false, 'nil'}))
    elseif k == 22 then e = set('vg_clean', F.chance(.7))
    elseif k == 23 then e = set('fc_v', false)
    elseif k == 24 then e = set('set_ret', 'nil')
    elseif k >= 25 and k <= 30 then return {}
    elseif k == 31 then e = set(F.pick({'app', 'get', 'set'}), F.pick({1, 1, 1, 2, 3, 'nil'}))
    elseif k == 32 then return owned_binding(at)
    elseif k == 33 then e = set(F.pick({'fc_has', 'c1_has', 'c2_has', 'ctl_has'}), F.chance(.7))
    elseif k == 34 then e = set(F.pick({'c2_v', 'c1_v', 'fc_v'}), F.pick({true, false, false, 'nil', 3, 'text'}))
    elseif k == 35 then e = set(F.pick({'c2_why', 'fc_why'}), F.pick({'nil', 'second camera', false}))
    elseif k == 36 then e = set('profile', F.pick({true, true, 1, 'nil'}))
    elseif k == 37 then e = set('verify_ok', F.pick({true, true, false, 1}))
    elseif k == 38 then e = mem(EXE + F.pick({0x3d43d0, 0x3d3ff0}) + F.rnd(8) - 1, F.pick({'\0', 'H', 2}))
    elseif k == 39 then e = set('exe', F.pick({EXE, EXE, EXE + 4096}))
    elseif k == 40 then e = set(F.pick(values), F.pick({1, 'nil', 'text'}))
    elseif k == 41 then e = set(F.pick(RAISES), F.pick({'host raised', 'Memory unavailable'})); at = F.rnd(F.last_calls + 3)
    elseif k == 42 then e = set('time', F.pick({0 / 0, 1 / 0, -1 / 0, t - 3, t - .001}))
    elseif k == 43 then return order()
    else return join(order(), {set('v_dof', true), set('v_vig', true), set('v_other', true)}) end
    if e.v == 'nil' then e.v = nil end
    e.at = at
    return {e}
  end
  for _ = 1, count do
    t = t + F.pick({0, 0, .05, .125, .2, .3, .5, .5, .5, .7, 1.1, 2.3})
    if F.chance(.01) then t = t - F.pick({.3, 5, 500}) end
    local events = {set('time', t)}
    for _ = 1, F.pick({0, 0, 1, 1, 1, 2, 3}) do events = join(events, change()) end
    if not calm and F.chance(.12) then
      local at = F.rnd(F.last_calls + 2)
      events[#events + 1] = set('x_any', F.pick({'host raised', 'Invalid read'}), at)
      if F.chance(.6) then events[#events + 1] = set('x_any', nil, at + 1) end
    end
    local call = F.pick({'update', 'update', 'update', 'update', 'update', 'update', 'stop', 'stop', F.chance(.01) and 'shutdown' or 'update'})
    local compat = F.pick({1, 1, 1, 1, 1, 1, 0, 0, 2, 3})
    F.op(call, call == 'update' and {compat, F.chance(.03) or nil} or nil, events)
  end
end
for seed = 1, 12 do F.run('walk' .. seed, function() walk(seed, 600) end) end
for seed = 21, 26 do F.run('calm' .. seed, function() walk(seed, 600, true) end) end
F.finish()

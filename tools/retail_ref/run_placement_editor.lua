-- Retail src/first_person/placement_editor.lua, unmodified, under LuaJIT against scripted fakes
-- (protocol: effects_common.lua). tests/placement_editor_test.c replays the same world in C.
--
-- A placement is two slots: <name> (true: a table, anything else: that value) and <name>_f keyed 1..6
-- (x y z yaw pitch roll; a missing key is a nil field). The mouse sample is `sample` / `sample_f`
-- keyed 1..5 (click, x, y, wheel, dt).
package.path = (arg[0]:match('^(.*)/[^/]*$') or '.') .. '/?.lua;' .. package.path
local F = require('effects_common')
local COVER = os.getenv('TFP_COVER')

local CALLS = {'clock', 'c_is_clean', 'c_is_enabled', 'c_result', 'c_set_placement', 'c_get_placement', 'c_start', 's_select_mode',
  's_can_start', 's_target', 's_get', 's_get_action', 's_save', 's_reset_all', 's_baseline', 's_action_supported', 'lut_status',
  'w_mouse_focus', 'w_show_cursor', 'a_close', 'a_is_open', 'a_input_status', 'a_resolution', 'a_create_view', 'i_acquire', 'i_release',
  'i_set_focus', 'i_sample', 'i_down', 'i_focused', 'v_draw', 'v_release', 'v_refresh_font'}
local slots = {'clock', 'is_clean', 'is_enabled', 'result', 'set_ok', 'set_why', 'get_place', 'get_place_f', 'get_why', 'start_ok', 'start_why',
  'has_select', 'select_ok', 'select_why', 'has_can', 'can_ok', 'can_why', 'has_target', 'target_key', 'target_owner', 'store_get',
  'store_get_f', 'store_get_why', 'act', 'act_f', 'save_ok', 'save_why', 'has_reset_all', 'reset_ok', 'reset_why', 'base', 'base_f', 'basefn',
  'has_supported', 'supported', 'title', 'rotation', 'single_save', 'save_label', 'all_label', 'footer', 'actions', 'lut', 'lut_status',
  'win', 'mouse_focus', 'show_cursor', 'api', 'api_ok', 'input_id', 'close_ok', 'close_why', 'is_open', 'status', 'res_w', 'res_h', 'view',
  'view_why', 'acquire', 'acquire_why', 'release', 'release_why', 'focus_ok', 'focus_why', 'sample', 'sample_f', 'sample_why', 'down',
  'focused', 'draw_ok', 'draw_why', 'vrelease_ok', 'vrelease_why', 'no_log', 'x_any'}
for _, name in ipairs(CALLS) do slots[#slots + 1] = 'x_' .. name end
F.slots(slots)

local function place(slot)
  if F.get(slot) ~= true then return F.get(slot) end
  return {x = F.getk(slot .. '_f', 1), y = F.getk(slot .. '_f', 2), z = F.getk(slot .. '_f', 3), yaw = F.getk(slot .. '_f', 4),
    pitch = F.getk(slot .. '_f', 5), roll = F.getk(slot .. '_f', 6)}
end
local function field(v) if v == nil then return '~' end return F.num(v) end
local function place_text(p)
  return table.concat({field(p.x), field(p.y), field(p.z), field(p.yaw), field(p.pitch), field(p.roll)}, ',')
end
local function has(text, flag) return type(text) == 'string' and text:find(flag, 1, true) ~= nil end
-- input_status / capture.status: nil slot -> nil; a text of flags -> a table.
local function status_table(text)
  if type(text) ~= 'string' then return text end
  return {owner = has(text, 'g') and 'goose_first_person_editor' or has(text, 'o') and 'someone' or nil, active = has(text, 'a') or nil,
    pending_restore = has(text, 'p') or nil}
end

local controller = {}
function controller:is_clean() F.tick('c_is_clean'); return F.get('is_clean') end
function controller:is_enabled() F.tick('c_is_enabled'); return F.get('is_enabled') end
function controller:result()
  F.tick('c_result')
  local phase = F.get('result')
  if phase == nil then return nil end
  return {phase = phase}
end
function controller:set_placement(p) F.tick('c_set_placement', place_text(p)); return F.get('set_ok'), F.get('set_why') end
function controller:get_placement() F.tick('c_get_placement'); return place('get_place'), F.get('get_why') end
function controller:start(a, b) F.tick('c_start', F.show(a) .. ',' .. F.show(b)); return F.get('start_ok'), F.get('start_why') end

local function parse_actions()
  local text = F.get('actions')
  if type(text) ~= 'string' then return nil end
  local list = {}
  for id, label in text:gmatch('([^:,]+):([^,]+)') do list[#list + 1] = {id, label} end
  return list
end
local methods = {
  select_mode = function(kind) F.tick('s_select_mode', F.show(kind)); return F.get('select_ok'), F.get('select_why') end,
  can_start = function() F.tick('s_can_start'); return F.get('can_ok'), F.get('can_why') end,
  target = function() F.tick('s_target'); return F.get('target_key'), F.get('target_owner') end,
  get = function() F.tick('s_get'); return place('store_get'), F.get('store_get_why') end,
  get_action = function(key, action) F.tick('s_get_action', F.show(key) .. ',' .. F.show(action)); return place('act') end,
  save = function(p, key, owner, all, action)
    F.tick('s_save', place_text(p) .. ',' .. F.show(key) .. ',' .. F.show(owner) .. ',' .. F.show(all) .. ',' .. F.show(action))
    return F.get('save_ok'), F.get('save_why')
  end,
  reset_all = function() F.tick('s_reset_all'); return F.get('reset_ok'), F.get('reset_why') end,
  baseline = function(key, action)
    F.tick('s_baseline', F.show(key) .. ',' .. F.show(action))
    if F.get('basefn') ~= true then return F.get('basefn') end
    return {x = F.getk('base_f', 1), y = F.getk('base_f', 2), z = F.getk('base_f', 3), yaw = F.getk('base_f', 4), pitch = F.getk('base_f', 5),
      roll = F.getk('base_f', 6)}
  end,
  action_supported = function(action) F.tick('s_action_supported', F.show(action)); return F.get('supported') end,
}
local gates = {select_mode = 'has_select', can_start = 'has_can', target = 'has_target', reset_all = 'has_reset_all', action_supported = 'has_supported'}
local store = setmetatable({}, {__index = function(_, name)
  if name == 'actions' then return parse_actions() end
  if name == 'title' or name == 'save_label' or name == 'all_label' or name == 'footer' or name == 'single_save' then return F.get(name) end
  if name == 'rotation' then return F.get('rotation') end
  if name == 'baseline' then
    if F.get('base') == 'fn' then return methods.baseline end
    return place('base')
  end
  if gates[name] then return F.get(gates[name]) and methods[name] or nil end
  return methods[name]
end})

-- Integration.editor_api(): one table per identity, so retail's identity comparisons see what the world says.
local inputs, providers = {}, {}
local function input_of(id)
  if id == nil then return nil end
  if not inputs[id] then
    inputs[id] = {api = 2, isolated_look = true,
      acquire = function() F.tick('i_acquire'); return F.get('acquire'), F.get('acquire_why') end,
      release = function() F.tick('i_release'); return F.get('release'), F.get('release_why') end,
      set_focus = function(on) F.tick('i_set_focus', F.show(on)); return F.get('focus_ok'), F.get('focus_why') end,
      menu_sample = function()
        F.tick('i_sample')
        if F.get('sample') ~= true then return F.get('sample'), F.get('sample_why') end
        return {click = F.getk('sample_f', 1), x = F.getk('sample_f', 2), y = F.getk('sample_f', 3), wheel = F.getk('sample_f', 4),
          dt = F.getk('sample_f', 5)}, F.get('sample_why')
      end,
      down = function(code) F.tick('i_down', F.show(code)); return F.getk('down', code) end,
      focused = function() F.tick('i_focused'); return F.get('focused') end}
  end
  return inputs[id]
end
local view_methods = {
  draw = function(commands)
    local out = {}
    for _, c in ipairs(commands) do
      if c.type == 'rect' then out[#out + 1] = table.concat({'r', F.num(c.x), F.num(c.y), F.num(c.w), F.num(c.h), c.c, c.a, c.layer}, ' ')
      else out[#out + 1] = table.concat({'t', F.str(c.text), F.num(c.x), F.num(c.y), F.num(c.size), c.c, c.a, c.layer}, ' ') end
    end
    F.tick('v_draw', table.concat(out, ';'))
    return F.get('draw_ok'), F.get('draw_why')
  end,
  release = function() F.tick('v_release'); return F.get('vrelease_ok'), F.get('vrelease_why') end,
  refresh_font = function() F.tick('v_refresh_font') end}
local function provider_of(id)
  if id == nil then return nil end
  if not providers[id] then
    providers[id] = setmetatable({
      close = function() F.tick('a_close'); return F.get('close_ok'), F.get('close_why') end,
      is_open = function() F.tick('a_is_open'); return F.get('is_open') end,
      input_status = function() F.tick('a_input_status'); return status_table(F.get('status')) end,
      photo_resolution = function() F.tick('a_resolution'); return F.get('res_w'), F.get('res_h') end,
      create_photo_view = function()
        F.tick('a_create_view')
        local flags = F.get('view')
        if type(flags) ~= 'string' then return flags, F.get('view_why') end
        return {draw = has(flags, 'd') and view_methods.draw or nil, release = has(flags, 'r') and view_methods.release or nil,
          refresh_font = has(flags, 'f') and view_methods.refresh_font or nil}, F.get('view_why')
      end}, {__index = function(_, name)
        if name == 'camera_input' and F.get('api_ok') then return input_of(F.get('input_id')) end
      end})
  end
  return providers[id]
end
local Integration = {editor_api = function() return provider_of(F.get('api')) end}
local Style = {editor_legacy_tokens = {colors = {panel = 'panel', field = 'field', text = 'text', accent = 'accent', text_dim = 'text_dim'},
  opacity = {solid = 'solid', panel = 'panel'}}}

-- package.loaded['dbf.epic_lut.frontend.v1'] and stingray.Window, read from the world at every access.
package.loaded['dbf.epic_lut.frontend.v1'] = nil
local epic_capture = setmetatable({}, {__index = function(_, name)
  local flags = F.get('lut')
  if name == 'active' then return has(flags, 'a') or nil end
  if name == 'status' and has(flags, 's') then return function() F.tick('lut_status'); return status_table(F.get('lut_status')) end end
end})
local epic = setmetatable({}, {__index = function(_, name)
  local flags = F.get('lut')
  if name == 'closed' then return has(flags, 'c') or nil end
  if name == 'menu' then return has(flags, 'm') and {visible = true} or has(flags, 'M') and {} or nil end
  if name == 'was_visible' then return has(flags, 'w') or nil end
  if name == 'capture' then return (has(flags, 'a') or has(flags, 's') or has(flags, 'k')) and epic_capture or nil end
end})
setmetatable(package.loaded, {__index = function(_, name)
  if name == 'dbf.epic_lut.frontend.v1' and type(F.get('lut')) == 'string' then return epic end
end})
local window = setmetatable({}, {__index = function(_, name)
  local flags = F.get('win')
  if name == 'mouse_focus' and has(flags, 'f') then return function() F.tick('w_mouse_focus'); return F.get('mouse_focus') end end
  if name == 'show_cursor' and has(flags, 'c') then return function() F.tick('w_show_cursor'); return F.get('show_cursor') end end
end})
stingray = setmetatable({}, {__index = function(_, name) if name == 'Window' and type(F.get('win')) == 'string' then return window end end})

local source_lines
if COVER then
  jit.off()
  source_lines = {}
  debug.sethook(function(_, line)
    local info = debug.getinfo(2, 'S')
    if info.source == '@src/first_person/placement_editor.lua' then source_lines[line] = true end
  end, 'l')
end
local Editor = F.load('placement_editor.lua', {['src/first_person.lua'] = Integration, ['src/first_person/ui_style.lua'] = Style})
local editor
local function monotonic() F.tick('clock'); return F.get('clock') end
local function fresh()
  editor = Editor.new(controller, store, not F.get('no_log') and F.log or nil, monotonic)
end

function F.dispatch(call, args)
  local results
  if call == 'new' then fresh(); results = {true}
  elseif call == 'request_open' then results = {pcall(editor.request_open, editor, args[1])}
  elseif call == 'update' then results = {pcall(editor.update, editor)}
  elseif call == 'cancel' then results = {pcall(editor.cancel, editor, args[1])}
  elseif call == 'finish' then results = {pcall(editor.finish, editor, args[1])}
  elseif call == 'shutdown' then results = {pcall(editor.shutdown, editor)}
  else error('unknown call ' .. call) end
  local out
  if results[1] then out = F.show(results[2]) .. ' ' .. F.show(results[3]) else out = 'ERR ' .. F.show(results[2]) end
  local rotation = editor:preview_rotation()
  return table.concat({out, editor.phase, F.show(editor.kind), F.show(editor:status()), F.show(editor:is_active()), F.show(editor:is_clean()),
    F.show(editor:preview_action()), rotation and (F.num(rotation.yaw) .. ',' .. F.num(rotation.pitch) .. ',' .. F.num(rotation.roll)) or '~'}, ' ')
end

-- ---- scenarios ----
local function set(events, slot, v, at) events[#events + 1] = {slot = slot, v = v, at = at} end
local function setk(events, slot, key, v, at) events[#events + 1] = {slot = slot, key = key, v = v, at = at} end
local function put_place(events, slot, p, at)
  set(events, slot, true, at)
  for i = 1, 6 do setk(events, slot .. '_f', i, p[i], at) end
end
local function put_sample(events, click, x, y, wheel, dt, at)
  set(events, 'sample', true, at)
  setk(events, 'sample_f', 1, click, at); setk(events, 'sample_f', 2, x, at); setk(events, 'sample_f', 3, y, at)
  setk(events, 'sample_f', 4, wheel, at); setk(events, 'sample_f', 5, dt, at)
end
local KEYS = {2, 4, 13, 16, 27, 33, 34, 37, 38, 39, 40}
-- A world in which everything works: the retail weapon store, first person running, MCM closed.
local function good(mode)
  local e = {}
  set(e, 'clock', 10); set(e, 'is_clean', true); set(e, 'is_enabled', true); set(e, 'result', 'running')
  set(e, 'set_ok', true); set(e, 'set_why', nil); put_place(e, 'get_place', {.1, .01, -.06, 0, 0, 0}); set(e, 'get_why', nil)
  set(e, 'start_ok', true); set(e, 'start_why', nil)
  set(e, 'has_select', mode ~= 'bare'); set(e, 'select_ok', true); set(e, 'select_why', nil)
  set(e, 'has_can', mode ~= 'bare'); set(e, 'can_ok', true); set(e, 'can_why', nil)
  set(e, 'has_target', mode ~= 'bare'); set(e, 'target_key', '5663fbf6d81eee27'); set(e, 'target_owner', 7)
  put_place(e, 'store_get', {.12, 0, -.1, 0, 0, 0}); set(e, 'store_get_why', nil)
  put_place(e, 'act', {.09, .02, -.03, 1, 2, -4}); set(e, 'save_ok', true); set(e, 'save_why', nil)
  set(e, 'has_reset_all', mode ~= 'bare'); set(e, 'reset_ok', true); set(e, 'reset_why', nil)
  put_place(e, 'base', {.15, 0, -.12, 0, 0, 0}); set(e, 'base', mode == 'bare' and nil or 'fn'); set(e, 'basefn', true)
  set(e, 'has_supported', mode ~= 'bare'); set(e, 'supported', true)
  set(e, 'title', mode == 'camera' and 'HEAD CAMERA' or mode == 'weapon' and 'WEAPON PLACEMENT' or nil)
  set(e, 'rotation', mode == 'camera' and false or mode == 'weapon' and true or nil)
  set(e, 'single_save', mode == 'camera' or nil); set(e, 'save_label', mode == 'camera' and 'Save camera' or nil)
  set(e, 'all_label', mode == 'camera' and 'Save camera' or nil)
  set(e, 'footer', mode == 'camera' and 'Camera only; weapon placements are unchanged' or nil)
  set(e, 'actions', mode == 'camera' and 'camera:Head camera' or mode == 'bare' and nil or 'idle:Idle,aim:Aim,ads:ADS,sprint:Sprint,prone:Prone')
  set(e, 'lut', nil); set(e, 'lut_status', nil); set(e, 'win', 'fc'); set(e, 'mouse_focus', true); set(e, 'show_cursor', false)
  set(e, 'api', 1); set(e, 'api_ok', true); set(e, 'input_id', 1); set(e, 'close_ok', true); set(e, 'close_why', nil)
  set(e, 'is_open', false); set(e, 'status', ''); set(e, 'res_w', 3840); set(e, 'res_h', 2160); set(e, 'view', 'drf'); set(e, 'view_why', nil)
  set(e, 'acquire', 'token'); set(e, 'acquire_why', nil); set(e, 'release', true); set(e, 'release_why', nil)
  set(e, 'focus_ok', true); set(e, 'focus_why', nil); put_sample(e, false, 1900, 900, 0, 1 / 100); set(e, 'sample_why', nil)
  for _, key in ipairs(KEYS) do setk(e, 'down', key, false) end
  set(e, 'focused', true); set(e, 'draw_ok', nil); set(e, 'draw_why', nil); set(e, 'vrelease_ok', nil); set(e, 'vrelease_why', nil)
  return e
end
local function open(kind, mode)
  F.op('request_open', {kind}, good(mode))
  F.op('update'); F.op('update'); F.op('update')
end
-- The panel as retail lays it out at 3840x2160 (scale 1.5): button centres, for clicks.
local SCALE = 1.5
local PX, PY, PH = 24 * SCALE, 2160 - 280 * SCALE, 256 * SCALE
local X0, TOP = PX + 16 * SCALE, PY + PH - 28 * SCALE
local function small(i) return X0 + (i - 1) * 40 * SCALE + 16 * SCALE, TOP - 36 * SCALE + 13 * SCALE end
local function big(index, count, pw)
  local bw = count == 4 and 112 * SCALE or (pw - 32 * SCALE - (count - 1) * 10 * SCALE) / count
  return X0 + (index - 1) * (bw + 10 * SCALE) + bw / 2, PY + 12 * SCALE + 14 * SCALE
end
local function click_at(x, y, more)
  local e = more or {}
  put_sample(e, true, x, y, 0, 1 / 100)
  F.op('update', nil, e)
  local up = {}
  put_sample(up, false, x, y, 0, 1 / 100)
  F.op('update', nil, up)
end

local function click_with(more, x, y) click_at(x, y, more) end

-- Directed: each mode opened, every button, every drag, the keys, save and cancel.
for _, mode in ipairs({'weapon', 'camera', 'bare', 'plain'}) do
  F.run('directed ' .. mode, function()
    F.op('new', nil, good(mode))
    F.op('update'); F.op('cancel'); F.op('finish'); F.op('shutdown')
    open(mode == 'camera' and 'camera' or nil, mode)
    local count = mode == 'camera' and 4 or mode == 'bare' and 4 or 5
    local pw = math.min(3840 - 48 * SCALE, 530 * SCALE)
    click_at(small(2)); click_at(small(2)); click_at(small(2)); click_at(small(1))
    -- drags: left (move), right (yaw/pitch), middle (roll), wheel, arrows, fine
    local e = {}; put_sample(e, true, 2000, 800, 0, .01); F.op('update', nil, e)
    e = {}; put_sample(e, true, 2100, 700, 120, .01); F.op('update', nil, e)
    e = {}; put_sample(e, true, 2150, 650, -240, .5); setk(e, 'down', 16, true); F.op('update', nil, e)
    e = {}; put_sample(e, false, 2150, 650, 0, .01); setk(e, 'down', 16, false); F.op('update', nil, e)
    e = {}; setk(e, 'down', 2, true); F.op('update', nil, e)
    e = {}; put_sample(e, false, 2400, 400, 0, .01); F.op('update', nil, e)
    e = {}; setk(e, 'down', 2, false); setk(e, 'down', 4, true); F.op('update', nil, e)
    e = {}; put_sample(e, false, 1000, 400, 0, .01); F.op('update', nil, e)
    e = {}; setk(e, 'down', 4, false); F.op('update', nil, e)
    for _, key in ipairs({37, 39, 38, 40, 33, 34}) do
      e = {}; setk(e, 'down', key, true); F.op('update', nil, e)
      e = {}; setk(e, 'down', key, false); F.op('update', nil, e)
    end
    e = {}; put_sample(e, false, 1 / 0, 0 / 0, 1 / 0, -1); F.op('update', nil, e)
    e = {}; put_sample(e, true, 'x', nil, 'w', 'd'); F.op('update', nil, e)
    e = {}; put_sample(e, false, 1900, 900, 0, .01); F.op('update', nil, e)
    -- reset, reset all, save
    click_at(big(count == 5 and 4 or mode == 'camera' and 3 or 4, count, pw))
    if count == 5 then click_at(big(5, count, pw)) end
    e = {}; set(e, 'base', nil); click_with(e, big(count == 5 and 4 or mode == 'camera' and 3 or 4, count, pw))
    F.op('finish', {true})
    open(mode == 'camera' and 'camera' or nil, mode)
    click_at(big(1, count, pw))
    open(mode == 'camera' and 'camera' or nil, mode)
    if mode ~= 'camera' then click_at(big(2, count, pw)); open(nil, mode) end
    click_at(big(mode == 'camera' and 2 or 3, count, pw))
    open(mode == 'camera' and 'camera' or nil, mode)
    e = {}; setk(e, 'down', 13, true); F.op('update', nil, e)
    e = {}; setk(e, 'down', 13, false); F.op('update', nil, e)
    open(mode == 'camera' and 'camera' or nil, mode)
    e = {}; setk(e, 'down', 27, true); F.op('update', nil, e)
    e = {}; setk(e, 'down', 27, false); F.op('update', nil, e)
    -- the ads reset without rotation fields, then a rotation in the same frame (retail raises)
    if mode == 'weapon' then
      open(nil, mode)
      click_at(small(2)); click_at(small(2))
      e = {}; setk(e, 'down', 2, true); put_sample(e, false, 2400, 400, 0, .01); F.op('update', nil, e)
      e = {}; set(e, 'base', nil); local bx, by = big(4, count, pw); put_sample(e, true, bx, by, 0, .01)
      F.op('update', nil, e)
      e = {}; setk(e, 'down', 2, false); put_sample(e, false, 1900, 900, 0, .01); F.op('update', nil, e)
      F.op('cancel', {'because'})
    end
    F.op('shutdown')
  end)
end

-- Every refusal and failure, one at a time, at each stage.
local FAULTS = {
  {'clock', nil}, {'clock', 1 / 0}, {'clock', 'late'}, {'is_clean', false}, {'is_enabled', false}, {'is_enabled', nil}, {'result', nil},
  {'result', 'idle'}, {'result', 'waiting'}, {'result', 'suspended'}, {'result', 'reacquiring'}, {'result', 'suspend_pending'},
  {'set_ok', false}, {'set_ok', nil}, {'set_why', 'refused'}, {'get_place', nil}, {'get_place', false}, {'get_why', 'no placement'},
  {'start_ok', false}, {'start_why', 'no weapon'}, {'select_ok', false}, {'select_ok', nil}, {'select_why', 'mode refused'},
  {'can_ok', false}, {'can_why', 'pending'}, {'target_key', nil}, {'target_key', false}, {'target_key', 'other'}, {'target_key', 12},
  {'target_owner', 8}, {'target_owner', nil}, {'store_get', nil}, {'store_get_why', 'none'}, {'act', nil}, {'act', false},
  {'save_ok', false}, {'save_ok', nil}, {'save_why', 'disk'}, {'save_why', 5}, {'reset_ok', false}, {'reset_why', 'cannot'},
  {'base', nil}, {'base', true}, {'basefn', nil}, {'basefn', false}, {'supported', false}, {'supported', nil}, {'title', 'OTHER'}, {'rotation', false}, {'single_save', true},
  {'has_reset_all', false}, {'has_supported', false}, {'has_target', false}, {'has_can', false}, {'has_select', false},
  {'save_label', 'Keep'}, {'all_label', 'Keep all'}, {'footer', 'Foot'}, {'lut', ''}, {'lut', 'c'}, {'lut', 'cm'}, {'lut', 'm'}, {'lut', 'M'},
  {'lut', 'w'}, {'lut', 'a'}, {'lut', 's'}, {'lut', 'k'}, {'lut_status', 'o'}, {'lut_status', 'p'}, {'lut_status', ''}, {'win', nil},
  {'win', ''}, {'win', 'f'}, {'win', 'c'}, {'mouse_focus', false}, {'mouse_focus', nil}, {'show_cursor', true}, {'show_cursor', nil},
  {'api', nil}, {'api', 2}, {'api_ok', false}, {'input_id', 2}, {'input_id', nil}, {'close_ok', false}, {'close_ok', nil}, {'close_why', 'busy'},
  {'is_open', true}, {'is_open', nil}, {'status', nil}, {'status', 'o'}, {'status', 'a'}, {'status', 'p'}, {'status', 'g'}, {'res_w', nil},
  {'res_w', 0}, {'res_h', 1 / 0}, {'res_h', 600}, {'res_h', 3000}, {'res_w', 300}, {'view', nil}, {'view', ''}, {'view', 'd'},
  {'view', 'r'}, {'view', 'dr'}, {'view', true}, {'view_why', 'no view'}, {'acquire', nil}, {'acquire', false}, {'acquire_why', 'taken'},
  {'release', false}, {'release', nil}, {'release_why', 'stuck'}, {'focus_ok', false}, {'focus_why', 'no cursor'}, {'sample', nil},
  {'sample', false}, {'sample_why', 'gone'}, {'focused', false}, {'focused', nil}, {'draw_ok', false}, {'draw_ok', true},
  {'draw_why', 'canvas'}, {'vrelease_ok', false}, {'vrelease_why', 'held'}, {'no_log', true},
}
for _, name in ipairs(CALLS) do FAULTS[#FAULTS + 1] = {'x_' .. name, 'boom in ' .. name} end
local PLACES = {{.6, 0, 0, 0, 0, 0}, {.1, 0, 0, 91, 0, 0}, {.1, 0 / 0, 0, 0, 0, 0}, {.1, 0, nil, 0, 0, 0}, {.1, .2, .3}, {.5, -.5, .5, 90, -90, 90},
  {0, 0, 0, 0, 0, 95}, {.1, .1, .1, 0, 1 / 0, 0}}
F.run('faults', function()
  local n = 0
  for stage = 0, 5 do
    for _, fault in ipairs(FAULTS) do
      n = n + 1
      local mode = (n % 7 == 0) and 'camera' or (n % 11 == 0) and 'bare' or (n % 13 == 0) and 'plain' or 'weapon'
      F.op('new', nil, good(mode))
      local kind = mode == 'camera' and 'camera' or nil
      local e = {}; set(e, fault[1], fault[2])
      if stage == 0 then F.op('request_open', {kind}, e) else F.op('request_open', {kind}) end
      for step = 1, 4 do
        if step == stage then F.op('update', nil, e) else F.op('update') end
      end
      if stage == 5 then
        local c = {}; put_sample(c, true, 2000, 800, 0, .01); F.op('update', nil, c)
        F.op('finish', {n % 2 == 0}, e)
      end
      F.op('update'); F.op('cancel'); F.op('update', nil, good(mode)); F.op('shutdown'); F.op('update')
    end
  end
  for _, slot in ipairs({'get_place', 'store_get', 'act', 'base'}) do
    for _, p in ipairs(PLACES) do
      for stage = 2, 4 do
        F.op('new', nil, good('weapon'))
        F.op('request_open'); F.op('update')
        local e = {}; put_place(e, slot, p); if slot == 'base' then set(e, 'base', n % 2 == 0 and 'fn' or true); n = n + 1 end
        if slot == 'store_get' then set(e, 'is_enabled', false) end
        if stage == 2 then F.op('update', nil, e) else F.op('update') end
        if stage >= 3 then F.op('update'); click_with(stage == 3 and e or nil, small(2)); click_with(e, big(4, 5, 795)) end
        F.op('update'); F.op('finish'); F.op('shutdown', nil, good('weapon'))
      end
    end
  end
end)

-- Random walks: any operation, any world change, some of them in the middle of an operation.
local function wander(seed, steps)
  F.run('walk ' .. seed, function()
    F.seed(seed)
    local mode = F.pick({'weapon', 'weapon', 'camera', 'bare', 'plain'})
    F.op('new', nil, good(mode))
    for _ = 1, steps do
      local e = {}
      if F.chance(.04) then mode = F.pick({'weapon', 'weapon', 'camera', 'bare', 'plain'}); e = good(mode) end
      if F.chance(.10) then
        local fault = F.pick(FAULTS)
        set(e, fault[1], fault[2], F.chance(.5) and F.rnd(12) or 0)
      end
      if F.chance(.06) then put_place(e, F.pick({'get_place', 'store_get', 'act', 'base'}), F.pick(PLACES)) end
      if F.chance(.5) then
        local x = F.pick({60, 100, 150, 220, 330, 500, 700, 1900, 2400, 3000})
        local y = F.pick({1700, 1760, 1790, 1850, 1950, 2000, 2100, 900, 400})
        put_sample(e, F.chance(.4), x + F.rnd(40), y + F.rnd(40), F.pick({0, 0, 0, 120, -120, 360}), F.pick({.01, .016, .2, 0}))
      end
      if F.chance(.25) then setk(e, 'down', F.pick(KEYS), F.chance(.5)) end
      local roll = F.rnd(100)
      if roll <= 70 then F.op('update', nil, e)
      elseif roll <= 82 then F.op('request_open', {F.pick({nil, 'weapon', 'camera'})}, e)
      elseif roll <= 88 then F.op('cancel', {F.pick({nil, 'Because'})}, e)
      elseif roll <= 94 then F.op('finish', {F.pick({nil, true})}, e)
      else F.op('shutdown', nil, e) end
    end
  end)
end
for seed = 1, 40 do wander(seed, 200) end

if COVER then
  debug.sethook()
  local handle = assert(io.open(F.kit .. '/src/first_person/placement_editor.lua'))
  local missed, total, number = {}, 0, 0
  for line in handle:lines() do
    number = number + 1
    local code = line:gsub('%-%-.*$', ''):gsub('%s', '')
    if code ~= '' and code ~= 'end' and code ~= 'else' and code ~= '}' and code ~= 'end)' then
      total = total + 1
      if not source_lines[number] then missed[#missed + 1] = number end
    end
  end
  io.stderr:write(string.format('placement_editor.lua: %d of %d code lines reached; not reached: %s\n', total - #missed, total, table.concat(missed, ' ')))
end
F.finish()

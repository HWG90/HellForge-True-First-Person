-- Replays scripted sessions against the retail runtime.lua, unmodified, under LuaJIT.
-- Usage: luajit run_runtime.lua <Goose Gadget Kit folder> <script file> [coverage file]
--
-- Every module runtime.lua requires is a scripted fake built from tools/retail_ref/runtime_spec.py:
-- each call into a fake is written to the trace with its arguments and answers what the "knobs"
-- say; the script sets knobs before each operation. tests/tfp_runtime_test.c drives the C over the
-- same knobs with fakes generated from the same spec.
-- One line per operation:  answer | trace | state
--   answer  '=' and the values the called function returned, '!' and the raised value, or 'absent'
--   trace   every call into a fake, in order: name(arguments); '{name:answer}' is a function of
--           runtime.lua that the fake called back before it answered
--   state   runtime.lua's own variables after the operation, '=' when they are as after the session's operation before
-- Values: ~ nil, T, F, a number (decimal when whole and below 2^31, else x and the sixteen hex
-- digits of the double, nan for any NaN), "text", R<n> a table handed out by a fake.
local ffi = require('ffi')
local kit, script_path, coverage_path = arg[1], arg[2], arg[3]
local CHUNK = 'src/first_person/runtime.lua'

-- ------------------------------------------------------------------ values
local REFS, shadow = {}, {}
local trace
local function t(text) trace[#trace + 1] = text end
local val
local ref_meta = {
  __index = function(self, key) local row = shadow[rawget(self, '__ref')]; return row and row[key] end,
  __newindex = function(self, key, value)
    local id = rawget(self, '__ref')
    if key == 'capture' then
      FAKE_SET('capture_gate_token_set_capture', self, value)
    else
      shadow[id] = shadow[id] or {}; shadow[id][key] = value
    end
  end}
function R(id)
  local found = REFS[id]
  if not found then found = setmetatable({__ref = id}, ref_meta); REFS[id] = found end
  return found
end
local SPEC = dofile(script_path)
local TABLES, K = SPEC.tables, {}
local knob_of = {}
for i, name in ipairs(SPEC.knobs) do knob_of[name] = i end
local function kn(name) return K[assert(knob_of[name], name)] end

local box = ffi.new('double[1]')
local words = ffi.cast('uint32_t *', box)
local function num(x)
  if x ~= x then return 'nan' end
  if x == math.floor(x) and x > -2147483648 and x < 2147483648 and not (x == 0 and 1 / x < 0) then return string.format('%d', x) end
  box[0] = x
  return string.format('x%08x%08x', words[1], words[0])
end
jit.off(num)
local function quote(text)
  return '"' .. text:gsub('.', function(c)
    local b = c:byte()
    if b < 32 or b > 126 or c == '"' or c == '\\' then return string.format('\\x%02x', b) end
  end) .. '"'
end
function val(x)
  local kind = type(x)
  if x == nil then return '~' end
  if x == true then return 'T' elseif x == false then return 'F' end
  if kind == 'number' then return num(x) end
  if kind == 'string' then return quote(x) end
  if kind == 'table' then
    local ref = x.__ref
    if ref then return 'R' .. num(ref) end
    if x.__obj then return 'T' end
    return '{?}'
  end
  return '<' .. kind .. '>'
end
local function tabref(x)
  if x == nil then return '~' end
  if type(x) ~= 'table' then return val(x) end
  return 'R' .. num(x.__ref or 0)
end

-- ------------------------------------------------------------------ knob tables
local lease_cache, helpers = {}, {}
local fake_call
local function plain(kind, base)
  if K[base] ~= 1 then return nil end
  local out = {__ref = K[base + 1]}
  for i, field in ipairs(TABLES[kind]) do out[field] = K[base + 1 + i] end
  return out
end
local capture_meta = {__newindex = function(self, key, value)
  if key == 'aim' then FAKE_SET('fird_capture_set_aim', self, value.projection) else rawset(self, key, value) end
end}
local function build(kind, base)
  local flat = plain(kind, base)
  if not flat then return nil end
  if kind == 'LEASE' then
    local lease = lease_cache[flat.__ref] or {}
    lease_cache[flat.__ref] = lease
    for key in pairs(lease) do lease[key] = nil end
    lease.__ref = flat.__ref
    if flat.origin then
      lease.origin = {identity = flat.origin_identity, player_identity = flat.origin_player_identity, body = flat.origin_body,
        world = flat.origin_world}
    end
    if flat.alignment then
      local helper
      if flat.helper then
        helper = helpers[flat.__ref] or {}
        helpers[flat.__ref] = helper
        helper.depth_stats = flat.depth_stats and function(_, handle) return fake_call(SPEC.method.helper_depth_stats, lease, handle) end or nil
        helper.input_stats = flat.input_stats and function(_, handle) return fake_call(SPEC.method.helper_input_stats, lease, handle) end or nil
      end
      lease.alignment = {ended = flat.ended, kind = flat.kind, helper = helper, handle = flat.handle}
    end
    return lease
  elseif kind == 'CAPTURE' then
    local out = {__ref = flat.__ref, weapon_identity = flat.weapon_identity, identity = flat.identity, weapon_key = flat.weapon_key,
      source_fov = flat.source_fov, reference = flat.reference, pivot = flat.pivot}
    if flat.projection then
      out.projection = {__ref = flat.__ref, position = {flat.px, flat.py, flat.pz}, rotation = {flat.rx, flat.ry, flat.rz, flat.rw}}
    end
    return setmetatable(out, capture_meta)
  end
  return flat
end
local function lease_of(slot)
  if not slot or slot == 0 then return nil end
  return build('LEASE', knob_of['lease' .. num(slot) .. '.present'])
end
local policy_meta = {}
local function policies(base)
  if K[base] ~= 1 then return nil end
  local rows, order = setmetatable({}, policy_meta), {}
  for slot = 0, K[base + 2] - 1 do
    local name = K[base + 3 + slot * 3]
    rows[name] = {core = K[base + 4 + slot * 3], phase = K[base + 5 + slot * 3]}
    order[#order + 1] = name
  end
  policy_meta[rows] = order
  return {__ref = K[base + 1], policies = rows}
end
setmetatable(policy_meta, {__mode = 'k'})

-- pairs in one defined order. runtime.lua walks three tables with string keys where the order
-- shows (the stop order of recover_new_owner, the policies of the recovery ledger, the restore of
-- reset_all); LuaJIT's own order there follows the string hashes. The C walks them in this order.
local RANK = {fov = 1, blur = 2, camera = 3, head = 4, movement = 5, near = 6, dof = 7, native_aim = 8, aim_markers = 9, arm_pieces = 10,
  grip_hand = 11, weapon_placements = 1, action_placements = 2, placement = 3}
local raw_pairs = pairs
function pairs(tbl)
  local order = policy_meta[tbl]
  if not order then
    order = {}
    for key in raw_pairs(tbl) do
      if type(key) ~= 'string' then return raw_pairs(tbl) end
      order[#order + 1] = key
    end
    table.sort(order, function(a, b)
      local x, y = RANK[a] or 1000, RANK[b] or 1000
      if x ~= y then return x < y end
      return a < b
    end)
  end
  local at = 0
  return function()
    at = at + 1
    local key = order[at]
    if key ~= nil then return key, rawget(tbl, key) end
  end
end

-- ------------------------------------------------------------------ the generic fake
local CAP, STORE = {}, nil
local calls, faults, callbacks, run_entry, seen
local ready_context_now
local PRINT = {
  v = val, n = val, b = val, t = val, tab = tabref,
  tabx = function(x)
    if x == nil then return '~' end
    return tabref(x) .. '[' .. val(x.zoom_identity) .. ',' .. val(x.controller_active) .. ']'
  end,
  ctx = function(x) if x == nil then return '~' end return 'c' .. val(x.tick) end,
}
local function vals(kind, value)
  if value == nil then return '~' end
  local out = {}
  for i, field in ipairs(TABLES[kind]) do out[i] = val(value[field]) end
  return '[' .. table.concat(out, ',') .. ']'
end
function fake_call(m, ...)
  local args, parts, at = {...}, {}, 0
  for _, spec in ipairs(m.ins) do
    at = at + 1
    if spec[1] == 'vals' then parts[#parts + 1] = vals(spec[2], args[at])
    elseif spec[1] ~= '_' then parts[#parts + 1] = PRINT[spec[1]](args[at]) end
  end
  calls = calls + 1
  local n = calls
  t(m.host .. '(' .. table.concat(parts, ',') .. ')')
  if m.effect == 'frame_on' then K[knob_of['backend_frame_active']] = kn('frame.during')
  elseif m.effect == 'frame_off' then K[knob_of['backend_frame_active']] = false
  elseif m.effect == 'revision' then K[knob_of['backend_action_placement_revision']] = args[1]
  elseif m.effect == 'ref_set' then
    local id = rawget(args[1], '__ref')
    shadow[id] = shadow[id] or {}; shadow[id].capture = args[2]
  end
  seen[m] = (seen[m] or 0) + 1
  local occurrence = seen[m]
  for _, cb in ipairs(callbacks) do
    if cb[1] == m.index and cb[2] == occurrence then t('{' .. SPEC.entries[cb[3]].name .. ':' .. run_entry(cb[3], cb[4]) .. '}') end
  end
  local fault = faults[n]
  if fault == 1 then error('boom', 0) end
  local out = {}
  for i, o in ipairs(m.outs) do
    local value
    if fault == 2 then
      if o[1] == 'v' then if i == 1 then value = false elseif i == 2 then value = 'no' end end
    elseif fault ~= 3 then
      if o[1] == 'v' then value = K[o[2]]
      elseif o[1] == 'tab' then value = build(o[3], o[2])
      else value = policies(o[2]) end
    end
    out[i] = value
  end
  if m.effect == 'read' and type(out[1]) == 'string' and #out[1] > args[2] then out[1] = out[1]:sub(1, args[2]) end
  if m.effect == 'clock' and type(K[m.outs[1][2]]) == 'number' then
    K[m.outs[1][2]] = K[m.outs[1][2]] + kn('clock.step')
  end
  return unpack(out, 1, #m.outs)
end
function FAKE_SET(host, a, b) fake_call(SPEC.method[host], a, b) end

-- ------------------------------------------------------------------ objects and modules
local by_object, getters = {}, {}
for _, m in ipairs(SPEC.methods) do
  by_object[m.obj] = by_object[m.obj] or {}
  by_object[m.obj][m.lua] = m
end
for _, g in ipairs(SPEC.getters) do
  getters[g.obj] = getters[g.obj] or {}
  getters[g.obj][g.field] = g
end
local function nothing() end
local function read_getter(g)
  if g.field == 'camera_ready' then return K[g.knob] and nothing or nil end
  if g.out == 'v' then return K[g.knob] end
  if g.out == 'lease' then return lease_of(K[g.knob]) end
  return build(g.kind, g.knob)
end
local OBJECT_SETTERS = {backend = {pin_first_person = 'backend_set_pin_first_person', camera_update_token = 'backend_set_camera_update_token',
  action_placement_revision = 'backend_set_action_placement_revision', fird_camera_offset = 'backend_set_fird_camera_offset'}}
local BACKEND_CAPTURES = {presentation_transition = 1, presentation_action = 1, presentation_resume = 1, foreign_camera = 1,
  camera_before_entry = 1, action_placement = 1, action_mapping = 1}
local closures = {}
local function method_closure(m)
  local found = closures[m]
  if not found then
    if m.static then found = function(...) return fake_call(m, ...) end
    else found = function(_, ...) return fake_call(m, ...) end end
    closures[m] = found
  end
  return found
end
local function has(m) return not m.optional or K[m.has] ~= 0 end
local metas = {}
local function object(name)
  local meta = metas[name]
  if not meta then
    local methods, fields, setters = by_object[name] or {}, getters[name] or {}, OBJECT_SETTERS[name] or {}
    meta = {
      __index = function(_, key)
        if key == '__obj' then return true end
        local m = methods[key]
        if m and not m.static then if has(m) then return method_closure(m) end return nil end
        local g = fields[key]
        if g then return read_getter(g) end
      end,
      __newindex = function(_, key, value)
        if setters[key] then fake_call(SPEC.method[setters[key]], value)
        elseif name == 'backend' and BACKEND_CAPTURES[key] then CAP[key] = value end
      end}
    metas[name] = meta
  end
  return setmetatable({}, meta)
end

-- What each constructor is handed that the script may call later.
local CAPH = {}
function CAPH.backend_new(log) CAP.log = log end
function CAPH.controller_new(_, _, clock) CAP.clock = clock end
function CAPH.head_new(_, _, _, o)
  CAP.fallback_ready, CAP.pinned_ready, CAP.full_body = o.fallback_ready, o.pinned_ready, o.full_body
  CAP.hide_torso, CAP.show_torso, CAP.seated_head = o.hide_torso, o.show_torso, o.seated_head
  assert(o.model_shadows.__obj)
end
function CAPH.movement_new(_, _, _, o) assert(o.fallback_ready == CAP.fallback_ready) end
function CAPH.dof_new(_, _, _, o)
  CAP.active_view, CAP.dof_fallback_ready = o.active_view, o.fallback_ready
  assert(o.clock == CAP.clock and type(o.vignette_factory) == 'function')
end
function CAPH.near_new(_, _, _, clock, options) CAP.near_options = options; assert(clock == CAP.clock) end
function CAPH.native_aim_new(_, enabled, foreign) CAP.native_aim_enabled = enabled; assert(foreign == CAP.foreign_camera) end
function CAPH.aim_markers_source(_, _, o)
  CAP.markers_fird, CAP.markers_source_global_setting = o.fird, o.global_setting
  assert(o.foreign == CAP.foreign_camera and o.fallback_ready == CAP.fallback_ready and o.pinned_ready == CAP.pinned_ready)
end
function CAPH.aim_markers_new(_, _, o)
  CAP.markers_settings, CAP.markers_save, CAP.markers_global_setting, CAP.markers_save_global = o.settings, o.save, o.global_setting, o.save_global
end
function CAPH.head_positioner_new(_, _, enabled, get, set) CAP.hp_fird_enabled, CAP.hp_get, CAP.hp_set = enabled, get, set end
function CAPH.editor_new(pc, store)
  STORE = store
  for _, name in ipairs({'is_enabled', 'is_clean', 'get_placement', 'set_placement', 'result', 'placement_target', 'start'}) do
    local method = pc[name]
    CAP['pc_' .. name] = function(...) return method(pc, ...) end
  end
end
function CAPH.motion_new(_, _, _, _, _, options) CAP.motion_options = options end
function CAPH.menu_new(_, _, _, o)
  CAP.start_true_first_person, CAP.toggle_pose, CAP.stop_pose, CAP.start_native = o.start_pose, o.toggle_pose, o.stop, o.start_native
  CAP.start_free_aim, CAP.view_enabled, CAP.view_clean = o.start_free_aim, o.is_enabled, o.is_clean
  CAP.presentation_settings, CAP.current_weapon, CAP.save_presentation = o.presentation_settings, o.current_weapon, o.save_presentation
  CAP.aiming_markers, CAP.toggle_aiming_markers = o.aiming_markers, o.toggle_aiming_markers
  CAP.aim_markers_setting, CAP.set_aim_markers_setting, CAP.dev_status = o.aim_markers_setting, o.set_aim_markers_setting, o.dev_status
  assert(o.start_true_first_person == o.start_pose and o.start_fird == nil)
end
function CAPH.held_fire_new(_, o) CAP.held_fire_authorized = o.authorized end
function CAPH.ready_pose_new(o)
  CAP.ready_source_factory, CAP.ready_filter_valid, CAP.ready_log = o.source_factory, o.filter_valid, o.log
  assert(o.max_reapplications == 0 and o.retry_ticks == 30)
end
function CAPH.ready_source_new(_, gate) CAP.native_ready_gate = gate end
function CAPH.fird_native_ads_new(_, _, o) CAP.fird_prepare = o.prepare; assert(o.foreign == CAP.foreign_camera) end
function CAPH.fird_new(_, _, _, _, _, _, _, _, _, o) CAP.fird_aim_mode, CAP.fird_editor = o.aim_mode, o.editor end
function CAPH.fird_render_new(_, render, _, o) CAP.render_fird_pose, CAP.fird_render_active = render, o.active end
function CAPH.fird_warm_new(_, _, _, tick) CAP.fird_tick = tick end
function CAPH.fird_entry_new(o)
  CAP.fe_running, CAP.fe_target, CAP.fe_stop, CAP.fe_cached, CAP.fe_initialize = o.running, o.target, o.stop, o.cached, o.initialize
  CAP.fe_begin_fp, CAP.fe_sample, CAP.fe_capture, CAP.fe_enter = o.begin_fp, o.sample, o.capture, o.enter
end

local NEW_OBJECT = {capture_gate_new = 'capture_gate'}
local function constructor(m)
  return function(...)
    local hook = CAPH[m.host]
    if hook then hook(...) end
    if m.host == 'fird_new' then
      local o = select(10, ...)
      local opts = {}
      for _, field in ipairs(TABLES.FIRDOPT) do opts[field] = o[field] end
      return fake_call(m, opts) or object('fird')
    end
    local value
    if m.host == 'transition_new' then local o = ...; fake_call(m, o.duration, o.max_gap)
    elseif m.host == 'diagnostics_new' then local o = ...; fake_call(m, o.capacity)
    else value = fake_call(m, ...) end
    if #m.outs == 0 then return object(m.obj) end
    if (m.obj == 'arm_pieces' or m.obj == 'grip_hand') and value then return object(m.obj) end
    return value
  end
end
local module_cache = {}
local function module(name, extra)
  local found = module_cache[name]
  if found then return found end
  local methods, fields = by_object[name] or {}, getters[extra or name] or {}
  local made = {}
  found = setmetatable({}, {__index = function(_, key)
    if key == '__obj' then return true end
    local m = methods[key]
    if m and m.static then
      if not has(m) then return nil end
      if not made[key] then made[key] = (key == 'new' or key == 'source') and constructor(m) or method_closure(m) end
      return made[key]
    end
    local g = fields[key]
    if g then return read_getter(g) end
    if name == 'actions' and key == 'observed_actions' then
      return setmetatable({}, {__index = function(_, action)
        if action == kn('actions.observed_name') then return K[knob_of['actions_observed']] end
      end})
    end
  end})
  module_cache[name] = found
  return found
end
local PATHS = {backend = 'backend', controller = 'controller', menu = 'menu', placement_editor = 'editor', fov = 'fov', blur = 'blur',
  dof = 'dof', camera = 'camera', motion_runtime = 'motion', camera_compat = 'compat', head_visibility = 'head',
  first_person_movement = 'movement', near_clip = 'near', recovery = 'recovery', diagnostics = 'diagnostics', transition = 'transition',
  model_shadow_source = 'model_shadows', arm_pieces = 'arm_pieces', grip_hand = 'grip_hand', native_ads_option = 'native_aim',
  aim_markers = 'aim_markers', head_placement = 'head_positioner', action_placement = 'actions', ready_pose = 'ready_pose',
  ready_pose_source = 'ready_source', ready_pose_timing = 'ready_timing', held_fire = 'held_fire', fird_aim = 'fird_aim',
  fird_native_ads = 'fird_native_ads', fird_fov = 'fird_fov', fird_render = 'fird_render', fird_capture_ready = 'capture_gate',
  fird_warm = 'fird_warm', fird_auto = 'fird_entry'}
local PLAIN = {fird_camera_pose = 1, skinned_placement = 1, support_hand = 1, fird_source_pose = 1, fird_retarget = 1,
  fird_weapon_children = 1}
local plain_modules = {}
local config_proxy = setmetatable({}, {__index = function(_, key)
  if key == '__ref' then return rawget(K[knob_of['config_table']], '__ref') end
  if key == 'action_state_mapping' then
    local value = kn('config.action_state_mapping')
    if type(value) == 'table' then
      local mapped = kn('config.mapped_action')
      rawset(value, 'first', {one = 'unused', two = mapped}); rawset(value, 'second', 7)
    end
    return value
  end
  return K[knob_of['config.' .. key]]
end})
local function require_fake(path)
  if path == 'src/first_person.lua' then return {host = function(parent) return parent end} end
  if path == 'src/loadout_catalogue.lua' then return {gear = {}} end
  local name = path:match('^src/first_person/([%w_]+)%.lua$')
  if name == 'compat51' then return {kind = type} end
  if name == 'action_placement' then
    -- The one module only on_enable requires: a new generation of closures starts here.
    for key in pairs(CAP) do CAP[key] = nil end
    STORE = nil
  end
  if name == 'profile' then return {camera = module('profile')} end
  if name == 'vignette' then return {new = function() error('the vignette factory is not called by runtime.lua') end} end
  if name == 'fird_person' then return module('fird', 'fird_module') end
  if PLAIN[name] then plain_modules[name] = plain_modules[name] or {__obj = true}; return plain_modules[name] end
  local target = assert(PATHS[name], 'unexpected module ' .. tostring(path))
  if (target == 'arm_pieces' or target == 'grip_hand') and not has(by_object[target].new) then error('module missing', 0) end
  return module(target)
end
mod = setmetatable({}, {__index = function(_, key)
  if key == 'require' then return require_fake end
  if key == 'log' then return method_closure(SPEC.method.mod_log) end
  if key == 'set' then if has(SPEC.method.mod_set) then return method_closure(SPEC.method.mod_set) end return nil end
  if key == 'config' then if K[knob_of['config_table']] ~= nil then return config_proxy end return nil end
end})
local application = setmetatable({}, {__index = function(_, key)
  if key == 'time_since_launch' and has(SPEC.method.clock_now) then return method_closure(SPEC.method.clock_now) end
end})
local mouse = setmetatable({}, {__index = function(_, key)
  if key == 'button_index' and has(SPEC.method.mouse_button_index) then return method_closure(SPEC.method.mouse_button_index) end
  if key == 'button' and has(SPEC.method.mouse_button) then return method_closure(SPEC.method.mouse_button) end
end})
stingray = {Application = application, Mouse = mouse}

-- ------------------------------------------------------------------ the retail file
local hits = {}
if coverage_path then
  jit.off()
  debug.sethook(function(_, line)
    if debug.getinfo(2, 'S').source == '@' .. CHUNK then hits[line] = (hits[line] or 0) + 1 end
  end, 'l')
end
local handle = assert(io.open(kit .. '/' .. CHUNK, 'rb'))
local source = handle:read('*a'); handle:close()
local load_chunk = assert(loadstring(source, '@' .. CHUNK))
local entry_module

local function locate(fn, key, seen)
  seen = seen or {}
  if seen[fn] then return end
  seen[fn] = true
  local nested = {}
  for i = 1, 200 do
    local name, value = debug.getupvalue(fn, i)
    if not name then break end
    if name == key then return fn, i end
    if type(value) == 'function' and debug.getinfo(value, 'S').what == 'Lua' then nested[#nested + 1] = value end
  end
  for _, value in ipairs(nested) do
    local owner, index = locate(value, key, seen)
    if owner then return owner, index end
  end
end
local slots = {}
local function upvalue(key)
  local slot = slots[key]
  if not slot then
    local owner, index = locate(entry_module.on_update, key)
    if not owner then owner, index = locate(entry_module.on_enable, key) end
    if not owner then owner, index = locate(entry_module.on_disable, key) end
    slot = {assert(owner, key), index}
    slots[key] = slot
  end
  local _, value = debug.getupvalue(slot[1], slot[2])
  return value
end
local function local_of(fn, key)
  if not fn then return nil, false end
  local owner, index = locate(fn, key)
  if not owner then return nil, false end
  local _, value = debug.getupvalue(owner, index)
  return value, true
end

-- ------------------------------------------------------------------ the functions runtime.lua hands out
local API = {start_pose = 'start', seamless_version = 1, presentation_state = 1, diagnostics = 1, recovery_status = 1,
  clear_diagnostics = 1, start_fird = 1, probe_fird = 1, observe_camera_parent = 1, profile_camera = 1, camera_profile = 1,
  camera_kick_stats = 1, camera_parent_stats = 1, camera_handoff = 1, camera_handoff_stats = 1, camera_cut_hold = 1, stop_free_aim = 1,
  free_aim_stats = 1, edit_placement = 1, placement = 1, depth_stats = 1, input_stats = 1, status = 1, preflight = 1}
local STORE_FN = {store_get = 'get', store_can_start = 'can_start', store_action_supported = 'action_supported',
  store_get_action = 'get_action', store_target = 'target', store_save = 'save', store_select_mode = 'select_mode',
  store_reset_all = 'reset_all'}
local MODULE_FN = {on_enable = 1, on_update = 1, on_disable = 1, on_cleanup_poll = 1}
local function flat(...)
  return {n = select('#', ...), ...}
end
local CUSTOM = {}
function CUSTOM.seamless_version() local api = upvalue('api'); return api and function() return api.seamless_version end end
function CUSTOM.store_fields()
  if not STORE then return nil end
  return function()
    local actions = STORE.actions
    if type(actions) == 'table' and not actions.__ref then actions = actions[1][1] .. '/' .. actions[1][2] .. '/' .. num(#actions) end
    local baseline = STORE.baseline
    if type(baseline) == 'function' then baseline = 'function' elseif type(baseline) == 'table' then baseline = 'table' end
    return actions, STORE.title, STORE.rotation, baseline, STORE.single_save, STORE.save_label, STORE.all_label, STORE.footer
  end
end
function CUSTOM.store_baseline()
  if not STORE then return nil end
  return function(key, action)
    local baseline = STORE.baseline
    if type(baseline) == 'function' then return baseline(key, action) end
    if type(baseline) == 'table' then return nil, nil, baseline.x, baseline.y, baseline.z end
  end
end
function CUSTOM.near_options()
  local f = CAP.near_options
  return f and function()
    local r = f()
    if type(r) == 'table' and not r.__ref then return nil, r.hip, r.aim end
    return r
  end
end
function CUSTOM.motion_options()
  local f = CAP.motion_options
  return f and function()
    local r = f()
    if type(r) == 'table' and not r.__ref then return nil, r.enabled, r.scale, r.camera_bob_strength, r.camera_sway_strength end
    return r
  end
end
function CUSTOM.current_weapon()
  local f = CAP.current_weapon
  return f and function()
    local r = f()
    if r == nil then return nil end
    return true, r.key, r.identity, r.label, r.family
  end
end
function CUSTOM.status()
  local api = upvalue('api')
  return api and function()
    local text, r = api.status()
    local fird = r.fird or {}
    return text, r, r.fird ~= nil, fird.phase, fird.enabled, fird.writes, fird.weapon, fird.reference_needed, fird.scene_fov,
      fird.render_frames, fird.pending, r.fird_renderer, r.enabled, r.editor_active, r.editor_status, r.fov, r.arm_pieces, r.grip_hand,
      r.blur, r.camera, r.motion, r.compat, r.head, r.movement, r.dof, r.native_aim, r.near, r.aim_markers, r.ready_pose, r.held_fire
  end
end
local function find(name)
  if MODULE_FN[name] then return entry_module[name] end
  if CUSTOM[name] then return CUSTOM[name]() end
  if API[name] then
    local api = upvalue('api')
    return api and api[API[name] == 1 and name or API[name]]
  end
  if STORE_FN[name] then return STORE and STORE[STORE_FN[name]] end
  return CAP[name]
end
function run_entry(index, args)
  local e = SPEC.entries[index]
  local f = find(e.name)
  if not f then return 'absent' end
  local list = {}
  for i, kind in ipairs(e.args) do
    local a = args[i]
    if kind == 'ctx' then
      if a == 1 then a = upvalue('ready_context')
      elseif a == 2 then local now = upvalue('ready_context'); a = {phase = 'qualified_update', tick = now and now.tick or 0}
      else a = nil end
    elseif kind ~= 'v' then
      local group = kind:sub(5)
      a = a and build(SPEC.arg_groups[group], knob_of[group .. '.present']) or nil
    end
    list[i] = a
  end
  local got = flat(pcall(f, unpack(list, 1, #e.args)))
  if not got[1] then return '!' .. val(got[2]) end
  local out = {}
  for i = 1, e.nret do out[i] = val(got[i + 1]) end
  return '=' .. table.concat(out, ',')
end

-- ------------------------------------------------------------------ the state
local OTHER = {}
local function state()
  local out, objects = {}, {}
  for i, name in ipairs(SPEC.objects) do objects[i] = upvalue(name) ~= nil and '1' or '0' end
  out[#out + 1] = table.concat(objects)
  local function show(name, value) out[#out + 1] = name .. '=' .. value end
  for _, name in ipairs({'stopping', 'idle_clean', 'handoff_ready', 'recovery_enabled', 'fp_dof_player', 'fird_tick', 'ready_tick',
    'fird_capture_phase', 'diagnostics_fault', 'diagnostics_look_at', 'aim_button', 'aim_button_held', 'aim_mode_button', 'aim_mode_held',
    'seated_view_held'}) do
    show(name, val(upvalue(name)))
  end
  local look = upvalue('diagnostics_look')
  show('look', type(look) == 'table' and tabref(look) or val(look))
  local owner = upvalue('recovery_owner')
  show('owner', owner and val(owner.identity) .. ',' .. val(owner.body) .. ',' .. val(owner.world) or '~')
  local draining = upvalue('owner_draining')
  show('draining', draining and val(draining.identity) .. ',' .. val(draining.body) .. ',' .. val(draining.world) .. ',' ..
    val(draining.frame) .. ',' .. val(draining.cleaned_at) or '~')
  local function scoped(name, fn, key, printer)
    local value, found = local_of(fn, key)
    show(name, found and (printer or val)(value) or '?')
  end
  scoped('previous', CAP.clock, 'previous')
  scoped('edit_mode', STORE and STORE.select_mode, 'edit_mode')
  scoped('probe_deadline', CAP.stop_pose, 'probe_deadline')
  scoped('candidate', CAP.fe_sample, 'candidate', tabref)
  scoped('candidate_time', CAP.fe_sample, 'candidate_time')
  scoped('hip_capture', CAP.fe_sample, 'hip_capture', tabref)
  local global = rawget(_G, 'NativeADSCameraTest')
  show('global', global == nil and '0' or global == OTHER and '2' or '1')
  return table.concat(out, ' ')
end

-- ------------------------------------------------------------------ replay
local out = {}
local G = knob_of['global_get']
for _, session in ipairs(SPEC.sessions) do
  for i = 1, #SPEC.knobs do K[i] = SPEC.defaults[i] end
  for key in pairs(shadow) do shadow[key] = nil end
  for key in pairs(CAP) do CAP[key] = nil end
  STORE = nil
  slots = {}
  rawset(_G, 'NativeADSCameraTest', nil)
  entry_module = load_chunk()
  local last_state
  for _, op in ipairs(session) do
    local sets = op.s
    for i = 1, op.n, 2 do K[sets[i]] = sets[i + 1] end
    if K[G] == 0 then rawset(_G, 'NativeADSCameraTest', nil) elseif K[G] == 2 then rawset(_G, 'NativeADSCameraTest', OTHER) end
    trace, calls, faults, callbacks, seen = {}, 0, {}, op.c or {}, {}
    if op.f then for i = 1, #op.f, 2 do faults[op.f[i]] = op.f[i + 1] end end
    local answer = run_entry(op.e, op.a or {})
    local global = rawget(_G, 'NativeADSCameraTest')
    K[G] = global == nil and 0 or global == OTHER and 2 or 1
    local now = state()
    out[#out + 1] = answer .. '|' .. table.concat(trace, ' ') .. '|' .. (now == last_state and '=' or now) .. '|' .. num(calls)
    last_state = now
  end
end
io.write(table.concat(out, '\n'), '\n')
if coverage_path then
  local file = assert(io.open(coverage_path, 'w'))
  for line, count in pairs(hits) do file:write(line, ' ', count, '\n') end
  file:close()
end

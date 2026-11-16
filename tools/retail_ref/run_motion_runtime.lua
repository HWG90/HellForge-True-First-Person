-- Replays a script against the retail motion_runtime.lua, unmodified, under LuaJIT, with the
-- unmodified retail motion.lua, locomotion.lua (to_parent) and action_placement.lua beneath it.
-- Usage: luajit run_motion_runtime.lua <Goose Gadget Kit folder> <script file> [coverage file]
--
-- Everything the module does not own is a scripted fake: controller, backend, placement editor,
-- locomotion sampler, options, clock, log. The fakes answer what the "knobs" say; the script sets
-- knobs before each operation. tests/motion_runtime_test.c builds the same fakes over the same
-- knobs. One line per operation: what the call answered | the ordered trace of every call into a
-- fake with its arguments, log lines among them | the state table (self:result()), or '=' when it
-- is the same as after the previous operation.
-- Numbers: a whole number below 2^31 in decimal, anything else as 'x' and the sixteen hex digits
-- of the double, which names it exactly; any NaN is 'nan'.
local ffi = require('ffi')
local kit = arg[1]
local script = dofile(arg[2])
local coverage_path = arg[3]
local SOURCE = kit .. '/src/first_person/motion_runtime.lua'
mod = {require = function(path)
  if path:match('compat51') then return {kind = type, number = tonumber, bit = require('bit')} end
  return dofile(kit .. '/' .. path)
end}
local hits = {}
if coverage_path then
  jit.off()
  debug.sethook(function(_, line)
    local source = debug.getinfo(2, 'S').source
    if source:sub(-19) == '/motion_runtime.lua' then hits[line] = (hits[line] or 0) + 1 end
  end, 'l')
end
local Runtime = dofile(SOURCE)
local Motion = dofile(kit .. '/src/first_person/motion.lua')
local Locomotion = dofile(kit .. '/src/first_person/locomotion.lua')

local names, strings = script.names, script.strings
local EVIDENCE = script.evidence
local string_index = {}
for i, text in ipairs(strings) do string_index[text] = i end
local ACTIONS = {'idle', 'aim', 'ads', 'sprint', 'reload', 'crouch', 'prone', 'slide', 'dive'}
local SETTINGS = {'bob_strength', 'bob_speed', 'sway_strength', 'sway_speed', 'sprint_strength', 'camera_bob_strength',
  'camera_bob_speed', 'camera_sway_strength', 'camera_sway_speed'}
local PLACE = {'x', 'y', 'z', 'yaw', 'pitch', 'roll'}
local AXES = {'dx', 'dy', 'dz', 'qx', 'qy', 'qz', 'qw'}
local MODES = {[0] = 'pose_offset', 'native'}
local PHASES = {[0] = 'running', 'waiting', 'idle', 'suspended', 'cleanup_pending'}
local STATE = {'phase', 'applied', 'coalesced', 'softened', 'read_failures', 'write_refusals', 'time_held', 'dx', 'dy', 'dz', 'qx', 'qy',
  'qz', 'qw', 'reason', 'sprint_weight', 'bob_weight', 'sprint_target', 'sprint_held', 'shoulder_weight', 'ads_handoff', 'capture_ready',
  'capture_native_ads', 'ads', 'action', 'shoulder_aim', 'sprint_raw', 'l1', 'l3', 'l14', 'sprint_evidence', 'speed', 'gait_phase',
  'reset', 'rebased'}
local known = {}
for _, key in ipairs(STATE) do known[key] = true end
local LEASES = 6

local box = ffi.new('double[1]')
local words = ffi.cast('uint32_t *', box)
local function num(x)
  if x ~= x then return 'nan' end
  if x == math.floor(x) and x > -2147483648 and x < 2147483648 and not (x == 0 and 1 / x < 0) then return string.format('%d', x) end
  box[0] = x
  return string.format('x%08x%08x', words[1], words[0])
end
-- LuaJIT's assert puts '<chunk>:<line>: ' before its text. The chunk is wherever the kit is
-- installed; it is cut down to the kit-relative name, the line stays.
local function chunk(text) return (text:gsub('[^:]*/src/first_person/', 'src/first_person/')) end
-- Reading the double's words is type punning, which compiled traces may reorder: num stays interpreted.
jit.off(num)
local function val(x)
  if x == nil then return '~' end
  if x == true then return 'T' elseif x == false then return 'F' end
  if type(x) == 'number' then return num(x) end
  if x == EVIDENCE then return 'E' end
  return chunk(tostring(x))
end
local function S(index) if index ~= 0 then return strings[index] end end

local w, leases, lease_index, trace, clock_calls, runtime, last_state
local function t(text) trace[#trace + 1] = text end
local function L(lease) return lease == nil and 'L~' or 'L' .. tostring(lease_index[lease] or '?') end
local function bit_set(value, index) return math.floor(value / 2 ^ index) % 2 == 1 end
local function place_text(p)
  if p == nil then return '~' end
  local out = {}
  for i, key in ipairs(PLACE) do out[i] = val(p[key]) end
  return table.concat(out, ' ')
end
local function placement(prefix)
  local p = {}
  for i, key in ipairs(PLACE) do if bit_set(w[prefix .. 'present'], i - 1) then p[key] = w[prefix .. key] end end
  return p
end
local function flag(code) if code == 1 then return false elseif code == 2 then return true elseif code == 3 then return 'x' end end
local function number(kind, value) if kind == 1 then return value elseif kind == 2 then return 'x' end end
local function options_table(prefix, into)
  local o = into or {}
  for key in pairs(o) do o[key] = nil end
  o.enabled = flag(w[prefix .. 'enabled'])
  o.scale = number(w[prefix .. 'scale_kind'], w[prefix .. 'scale'])
  for i, key in ipairs(SETTINGS) do o[key] = number(w[prefix .. 'k' .. i], w[prefix .. 'v' .. i]) end
  o.editor_frozen = flag(w[prefix .. 'frozen'])
  o.shoulder_aim_enabled = flag(w[prefix .. 'shoulder'])
  return o
end

local function strip(text) return (tostring(text):gsub('^.-%.lua:%d+: ', '')) end
-- ------------------------------------------------------------------ the fakes
local controller, backend, editor, sampler, static_options = {}, {}, {}, {}, {}
function controller:result()
  t('result')
  if w.c_result == 1 then error('controller result raised', 0) end
  if w.c_result == 2 then return nil end
  return {mode = MODES[w.c_mode], phase = PHASES[w.c_phase]}
end
function controller:is_enabled()
  t('enabled')
  if w.c_enabled == 2 then error('controller enabled raised', 0) end
  if w.c_enabled == 3 then return 1 end
  if w.c_enabled == 4 then return nil end
  return w.c_enabled == 1
end
function controller:get_placement()
  t('get')
  if w.c_get == 2 then error('controller placement raised', 0) end
  if w.c_get == 1 then return nil end
  return placement('c_')
end
function controller:set_placement(value)
  t('set(' .. place_text(value) .. ')')
  if w.c_set == 3 then error(S(w.c_set_why) or 'set placement raised', 0) end
  if w.c_set == 2 then return false, S(w.c_set_why) end
  if w.c_set == 4 then return 1 end
  if w.c_set == 0 and value ~= nil then
    local present = 0
    for i, key in ipairs(PLACE) do
      if value[key] ~= nil then w['c_' .. key] = value[key]; present = present + 2 ^ (i - 1) end
    end
    w.c_present = present
  end
  return true
end
function backend:set_pose_motion(lease, value)
  local row = {}
  for i, axis in ipairs(AXES) do row[i] = val(value[axis]) end
  t('pose(' .. L(lease) .. ' ' .. table.concat(row, ' ') .. ' ' .. (value.authored_ads == true and 'A' or '-') .. ')')
  if w.b_pose == 3 then error(S(w.b_pose_why) or 'pose raised', 0) end
  if w.b_pose == 2 then return false, S(w.b_pose_why) end
  if w.b_pose == 4 then return 1 end
  return true
end
function backend:set_ads_placement(lease, ads)
  t('ads(' .. L(lease) .. ' ' .. place_text(ads) .. ')')
  if w.b_ads_set == 3 then error(S(w.b_ads_why) or 'ads placement raised', 0) end
  if w.b_ads_set == 2 then return false, S(w.b_ads_why) end
  if w.b_ads_set == 4 then return 1 end
  return true
end
function backend:player_snapshot()
  t('hint')
  if w.b_hint == 1 then error('snapshot raised', 0) end
  if w.b_hint == 2 then return nil end
  if w.b_hint == 3 then return {aiming = true, shoulder_aim = 1, controller_active = w.b_hint_active ~= 0} end
  if w.b_hint == 4 then return {aiming = true, shoulder_aim = w.b_hint_shoulder ~= 0} end
  return {aiming = false, shoulder_aim = w.b_hint_shoulder ~= 0, controller_active = w.b_hint_active ~= 0}
end
local function action_placement(key, action)
  t('ap(' .. val(key) .. ',' .. tostring(action) .. ')')
  local prefix = 'ap_' .. action .. '_'
  if w[prefix .. 'mode'] == 2 then error('action placement raised', 0) end
  if w[prefix .. 'mode'] == 1 then return nil end
  return placement(prefix)
end
local MAPPINGS = {
  {['4D1C334D294DFA97'] = {['75:71'] = 'sprint', ['5:6'] = 'reload', ['nil:nil'] = 'prone', ['1:2'] = 'unknown'}},
  {['0000000000000000'] = {['5:6'] = 'crouch'}, ['4D1C334D294DFA97'] = {['7:nil'] = 'dive', ['nil:9'] = 'slide'}},
}
local function action_mapping()
  t('mapping')
  if w.b_mapping_mode == 2 then error('mapping raised', 0) end
  if w.b_mapping_mode == 0 then return nil end
  return MAPPINGS[w.b_mapping_set]
end
local function presentation_transition()
  t('pt')
  if w.b_pt == 2 then error('transition raised', 0) end
  if w.b_pt == 3 then return nil end
  return {epoch = w.b_pt_epoch, progress = w.b_pt_progress, ads = w.b_pt_ads ~= 0}
end
local function presentation_action(action, timestamp)
  t('pa(' .. tostring(action) .. ',' .. val(timestamp) .. ')')
  if w.b_pa == 2 then error('presentation action raised', 0) end
  return true
end
local function presentation_resume(identity, player, timestamp)
  t('pr(' .. tostring(string_index[identity]) .. ',' .. tostring(string_index[player]) .. ',' .. val(timestamp) .. ')')
  if w.b_pr == 2 then error('presentation resume raised', 0) end
  return true
end
function editor.is_active()
  t('active')
  if w.e_active == 3 then error(S(w.e_why) or 'editor raised', 0) end
  if w.e_active == 2 then return nil, S(w.e_why) end
  if w.e_active == 4 then return 1 end
  return w.e_active == 1
end
local function preview_rotation()
  t('preview')
  if w.e_preview_mode == 2 then error('preview raised', 0) end
  if w.e_preview_mode == 1 then return nil end
  local p = {}
  for i, key in ipairs({'yaw', 'pitch', 'roll'}) do if bit_set(w.e_present, i - 1) then p[key] = w['e_' .. key] end end
  return p
end
function sampler:sample(lease)
  t('sample(' .. L(lease) .. ')')
  if w.s_mode == 3 then error(S(w.s_why) or 'sample raised', 0) end
  if w.s_mode == 1 then return nil, S(w.s_why) end
  if w.s_mode == 2 then return nil end
  local flags, camera, parent = {}, {}, {}
  for i = 1, 24 do flags[i] = string.char(w['s_f' .. i]) end
  for i = 1, 16 do camera[i] = w['s_cam' .. i]; parent[i] = w['s_par' .. i] end
  local action = w.s_has_action ~= 0
  return {owner = S(w.s_owner), x = w.s_x, y = w.s_y, z = w.s_z, yaw = w.s_yaw, pitch = w.s_pitch, ads = w.s_ads ~= 0,
    shoulder_aim = w.s_shoulder ~= 0, stance_flags = table.concat(flags), action_resource = action and S(w.s_resource) or nil,
    action_verified = action and w.s_verified ~= 0 or nil, sprint = w.s_sprint ~= 0, reload = w.s_reload ~= 0,
    l1 = action and bit_set(w.s_has_l, 0) and w.s_l1 or nil, l3 = action and bit_set(w.s_has_l, 1) and w.s_l3 or nil,
    l14 = action and bit_set(w.s_has_l, 2) and w.s_l14 or nil, sprint_evidence = EVIDENCE, camera_basis = camera, parent_basis = parent}
end
-- dependencies.to_parent, when the session asks for the seam (retail's own fixture uses it too).
local function to_parent(value, camera, parent)
  local row = {}
  for i, key in ipairs({'dx', 'dy', 'dz', 'pitch', 'yaw', 'roll'}) do row[i] = val(value[key]) end
  t('tp(' .. table.concat(row, ' ') .. ')')
  if w.tp_mode == 1 then error(S(w.tp_why) or 'to_parent raised', 0) end
  if w.tp_mode == 0 then
    -- The real conversion as this fake's own answer: its refusal is raised as the bare text.
    local ok, result = pcall(Locomotion.to_parent, value, camera, parent)
    if not ok then error(strip(result), 0) end
    return result
  end
  local out = {}
  for _, axis in ipairs(AXES) do
    local n = w['tp_' .. axis]
    if n == n then out[axis] = n end -- a NaN knob is a field that is missing
  end
  return out
end
local function clock()
  t('clock')
  clock_calls = clock_calls + 1
  local mode = w['clock_mode' .. math.min(clock_calls, 4)]
  local now = w.time
  w.time = w.time + w.clock_step
  if mode == 1 then error('clock raised', 0) end
  if mode == 2 then return 'x' end
  if mode == 3 then return 0 / 0 elseif mode == 4 then return math.huge elseif mode == 5 then return -math.huge end
  return now
end
local function options() t('options')
  if w.o_mode == 1 then error('options raised', 0) end
  if w.o_mode == 2 then return 'x' end
  return options_table('o_')
end
local function log(text)
  local head, reason = tostring(text):match('^(FPV motion: )(.*)$')
  t('log(' .. (head and head .. chunk(reason) or tostring(text)) .. ')')
end

-- A lease identity knob: 0 is nil, n > 0 the n-th text, n < 0 the number -n (which is no text).
local function identity(index) if index < 0 then return -index end return S(index) end
local function refresh()
  for i = 1, LEASES do
    local lease, p = leases[i], 'l' .. i .. '_'
    lease.done = w[p .. 'done'] ~= 0 or nil
    lease.cleanup_only = w[p .. 'cleanup'] ~= 0 or nil
    lease.pose_blend_entry = w[p .. 'blend'] ~= 0 or nil
    if w[p .. 'origin'] ~= 0 then
      lease.origin = lease.kept_origin
      lease.origin.identity = identity(w[p .. 'identity'])
      lease.origin.player_identity = identity(w[p .. 'player'])
    else lease.origin = nil end
  end
  controller.lease = leases[w.c_lease]
  backend.alignment_lease = leases[w.b_alignment]
  backend.placement_weapon_key = S(w.b_key)
  backend.action_placement_revision = w.b_revision_nil == 0 and w.b_revision or nil
  backend.action_placement = w.b_has_ap ~= 0 and action_placement or nil
  backend.action_mapping = w.b_has_mapping ~= 0 and action_mapping or nil
  backend.presentation_transition = w.b_pt ~= 0 and presentation_transition or nil
  backend.presentation_action = w.b_pa ~= 0 and presentation_action or nil
  backend.presentation_resume = w.b_pr ~= 0 and presentation_resume or nil
  editor.preview_rotation = w.e_has_preview ~= 0 and preview_rotation or nil
  options_table('o_', static_options)
end
local function state_text()
  if not runtime then return '-' end
  local state, row = runtime:result(), {}
  for key in pairs(state) do assert(known[key], 'unknown state key ' .. tostring(key)) end
  for i, key in ipairs(STATE) do row[i] = val(state[key]) end
  return table.concat(row, ' ')
end

local raised = {}
local out = {}
for _, op in ipairs(script.ops) do
  local kind = op[1]
  local answer
  if kind == 'begin' then
    w = {}
    for _, name in ipairs(names) do w[name] = 0 end
    leases, lease_index = {}, {}
    for i = 1, LEASES do leases[i] = {request = 100 + i, kept_origin = {}}; lease_index[leases[i]] = i end
    runtime, last_state = nil, nil
  end
  for i = 2, #op, 2 do w[names[op[i]]] = op[i + 1] end
  trace, clock_calls = {}, 0
  if kind == 'begin' then answer = 'B'
  elseif kind == 'new' then
    refresh()
    runtime = nil
    local dependencies, failed = {sampler = sampler}, nil
    if w.ctor_tp ~= 0 then dependencies.to_parent = to_parent end
    if w.ctor_model ~= 0 then
      local ok, made = pcall(Motion.new, w.ctor_model == 2 and options_table('m_') or nil)
      if ok then dependencies.motion = made else failed = strip(made) end
    end
    if failed then answer = 'N F ' .. failed
    else
      local given
      if w.ctor_options == 1 then given = static_options elseif w.ctor_options == 2 then given = options end
      local c, b, k = controller, backend, clock
      if w.ctor_bad == 1 then c = 'x' elseif w.ctor_bad == 2 then b = nil elseif w.ctor_bad == 3 then k = nil end
      local ok, made = pcall(Runtime.new, c, b, w.ctor_editor ~= 0 and editor or nil, w.ctor_log ~= 0 and log or nil, k, given, dependencies)
      if ok then runtime = made; answer = 'N T' else answer = 'N F ' .. strip(made) end
    end
  elseif kind == 'update' or kind == 'capture' or kind == 'reset' then
    refresh()
    local ok, a, b
    if kind == 'update' then ok, a, b = pcall(runtime.update, runtime)
    elseif kind == 'capture' then
      local native
      if w.cap_native == 1 then native = true elseif w.cap_native == 2 then native = false end
      ok, a, b = pcall(runtime.capture_neutral, runtime, native)
    else ok, a, b = pcall(runtime.reset, runtime) end
    if ok then
      assert(a == true or a == false, 'answer is not a boolean')
      assert(b == nil or type(b) == 'string', 'reason is not a text')
      answer = (a and 'T ' or 'F ') .. val(b)
    else
      answer = 'R'
      raised[strip(a)] = (raised[strip(a)] or 0) + 1
    end
  else error('unknown operation ' .. tostring(kind)) end
  local state = state_text()
  out[#out + 1] = answer .. '|' .. table.concat(trace, ';') .. '|' .. (state == last_state and '=' or state)
  last_state = state
end
io.write(table.concat(out, '\n'), '\n')
for text, count in pairs(raised) do io.stderr:write(string.format('raised %d: %s\n', count, text)) end
if coverage_path then
  debug.sethook()
  local file = assert(io.open(coverage_path, 'w'))
  for line, count in pairs(hits) do file:write(line, ' ', count, '\n') end
  file:close()
end

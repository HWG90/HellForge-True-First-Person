-- Replays scripts against the retail backend.lua (and the action_state.lua it loads),
-- unmodified, on its shipped (non-fixture) path: `Backend.new(log)` with no `io` table, the
-- way runtime.lua:665 builds it. What that path takes from Windows through the FFI
-- (GetModuleHandleA, ReadProcessMemory, VirtualQuery, casts to function pointers and to
-- byte / dword pointers) is a scripted Lua table here, so every read, write and engine call
-- of the real code path is seen. Prints, per step, the ordered trace of calls, writes and
-- log lines, the answer, and the backend's and every lease's state.
-- Usage: luajit run_backend_ops.lua <kit folder> <script file> [coverage file]
jit.off()
local kit, script_path, coverage_path = arg[1], arg[2], arg[3]
local bit = require('bit')
local LOW, HIGH = 65536, 140737488355328

-- ---- formatting -------------------------------------------------------------------------
local function N(x)
  if x == nil then return '-' end
  if x == true then return 't' end
  if x == false then return 'f' end
  if type(x) ~= 'number' then return '?' .. type(x) end
  if x ~= x then return 'nan' end
  if x == math.floor(x) and math.abs(x) < 1e17 then return string.format('%.0f', x) end
  return string.format('%.17g', x)
end
local function hex(bytes)
  if bytes == nil then return '-' end
  return (bytes:gsub('.', function(c) return string.format('%02x', c:byte()) end))
end
local address_name
-- Lua 5.1's assert puts "<chunk>:<line>: " in front of its message (so does the game's Lua 5.1).
-- The chunk name is the mod loader's business; the C gives the message alone. A pointer refusal
-- names an address, which differs between the builds: spelled by name here.
local function clean(text)
  text = tostring(text):gsub('B:%d+: ', ''):gsub('A:%d+: ', '')
  text = text:gsub('at0x(%x+)', function(h) return 'at' .. address_name(tonumber(h, 16)) end)
  return text
end
local function Q(text)
  if text == nil then return '-' end
  return '"' .. clean(text) .. '"'
end

-- ---- the fake process -------------------------------------------------------------------
local run, mem, state, trace, ports, hooks_present
local function T(line) trace[#trace + 1] = line end
local function apply_patch(index)
  for _, row in ipairs(run.patches[index + 1]) do
    local at, data = row[1], row[2]
    if at == 1 or at == 2 then
      local value = 0
      for i = 8, 1, -1 do value = value * 256 + tonumber(data:sub(i * 2 - 1, i * 2), 16) end
      if at == 1 then state.game = value else state.exe = value end
    elseif data == false then
      for i = 0, row[3] - 1 do mem[at + i] = nil end
    else
      for i = 1, #data, 2 do mem[at + (i - 1) / 2] = tonumber(data:sub(i, i + 1), 16) end
    end
  end
end
local armed = {}
-- A read that covers an armed address counts down; at zero the scene changes, after that read.
local function sprung(at, size)
  for i = #armed, 1, -1 do
    local t = armed[i]
    if at < t.at + t.size and t.at < at + size then
      t.count = t.count - 1
      if t.count <= 0 then
        table.remove(armed, i)
        apply_patch(t.patch)
      end
    end
  end
end
local function answer(name)
  local p = ports[name]
  if not p then return nil end
  if #p.fifo > 0 then return table.remove(p.fifo, 1) end
  return p.def
end
local function took(a)
  if a and a.patch >= 0 then apply_patch(a.patch) end
  if a and a.code == 99 then error(a.text or 'raised', 0) end
  return a
end
function address_name(at)
  if type(at) ~= 'number' then return N(at) end
  local name = run.names.game[at - state.game]
  if name then return 'game:' .. name end
  name = run.names.exe[at - state.exe]
  if name then return 'exe:' .. name end
  return N(at)
end

local SIGNATURES = {
  ['uint8_t (*)(void *,uint32_t)'] = 'allowed',
  ['void (*)(void *,uint32_t,uint32_t)'] = 'enter',
  ['void (*)(void *)'] = 'exit',
  ['void (*)(void *,uint32_t,uint8_t,uint8_t)'] = 'presentation',
  ['int (*)(uint32_t,uint32_t)'] = 'body_exists',
  ['void (*)(uint32_t,uint32_t)'] = 'body_trigger',
  ['void (*)(void *,uint32_t)'] = 'detach',
}
local function writer(at, width)
  return setmetatable({}, {__newindex = function(_, index, value)
    local where = at + index * width
    T('write ' .. N(where) .. ' ' .. width .. ' ' .. N(value))
    for i = 0, width - 1 do mem[where + i] = math.floor(value / 256 ^ i) % 256 end
  end})
end
local kernel = {
  NativeADS_Process = function() return 'process' end,
  NativeADS_Module = function(name)
    if name == 'game.dll' then return state.game end
    if name == 'helldivers2.exe' then return state.exe end
    return 0
  end,
  NativeADS_Read = function(_, at, buffer, size, count)
    for i = 0, size - 1 do
      local b = mem[at + i]
      if not b then count[0] = i; sprung(at, size); return 0 end
      buffer[i] = b
    end
    count[0] = size
    sprung(at, size)
    return 1
  end,
  NativeADS_Query = function(at, r)
    local q = state.region
    T('query ' .. address_name(at))
    r.state, r.kind, r.protect = q.state, q.kind, q.protect
    r.allocation = q.allocation == 1 and state.game or q.allocation == 2 and state.exe or 0
    r.base, r.size = at, 4096
    return q.answered and 48 or 0
  end,
}
local ffi = {os = 'Windows'}
function ffi.abi() return true end
function ffi.typeof() error('undeclared type', 0) end
function ffi.cdef() end
function ffi.sizeof() return 48 end
function ffi.load(name)
  if name == 'kernel32' then return kernel end
  if state.wine then return {wine_get_version = function() return 'wine' end} end
  return setmetatable({}, {__index = function() error('missing declaration', 0) end})
end
function ffi.new(kind)
  if kind == 'size_t[1]' then return {[0] = 0} end
  return {}
end
function ffi.string(buffer, size)
  local out = {}
  for i = 0, size - 1 do out[#out + 1] = string.char(buffer[i]) end
  return table.concat(out)
end
function ffi.cast(kind, value)
  if kind == 'const void *' or kind == 'void *' or kind == 'uintptr_t' then return value end
  if kind == 'uint8_t *' then return writer(value, 1) end
  if kind == 'uint32_t *' then return writer(value, 4) end
  local name = assert(SIGNATURES[kind], 'unknown cast ' .. tostring(kind))
  return function(...)
    local parts = {'call', name, address_name(value)}
    for _, v in ipairs({...}) do parts[#parts + 1] = N(v) end
    T(table.concat(parts, ' '))
    local a = took(answer(name))
    if name == 'allowed' or name == 'body_exists' then return a and a.code or 1 end
  end
end
local function_addresses = {}
local PORT51 = {kind = type, number = tonumber, bit = bit, ffi = ffi,
  jit_util = {funcinfo = function(fn) return {addr = function_addresses[fn]} end}}

-- ---- scripted helpers and hooks ---------------------------------------------------------
local natives, handles = 0, 0
local helpers = {}
local function config_text(c)
  return table.concat({address_name(c.game), address_name(c.exe), N(c.context), N(c.request - c.context), N(c.avatar_owner),
    N(c.avatar_index), N(c.avatar), N(c.weapon), N(c.body), N(c.viewmodel), N(c.duration_ms), N(c.weapon_descriptor),
    N(c.weapon_unit), N(c.blend_ms), N(c.steady_entry), N(c.offset_x), N(c.offset_y), N(c.offset_z)}, ' ')
end
local function stats_table(a, numeric)
  local function flag(code)
    if numeric then return code end
    if code == 0 then return false elseif code == 1 then return true end
    return code - 1
  end
  local s = {armed = flag(a.nums[1] or 1), expired = flag(a.nums[2] or 0), target_weight = a.nums[4] or 1, blend = a.nums[5] or 1}
  local e = a.nums[3]
  if e == nil then s.error = 0 elseif e == e then s.error = e end
  return s
end
local function make_helper(tag)
  local h = {tag = tag}
  local function port(name, text)
    T('helper ' .. tag .. '.' .. name .. (text and (' ' .. text) or ''))
    return answer(tag .. '.' .. name)
  end
  local function plain(a)
    took(a)
    if not a or a.code == 1 then return true, a and a.text or nil end
    if a.code == 0 then return false, a.text end
    return nil, a.text
  end
  function h.prepare(_, config)
    local a = took(port('prepare', config_text(config)))
    if not a or a.code == 1 then
      natives = natives + 1
      return true, {owner = h, id = natives}
    end
    if a.code == 0 then return false, a.text end
    return nil, a.text
  end
  function h.begin(_, config, native)
    local a = port('begin', config_text(config) .. ' n' .. N(native and native.id))
    local function own()
      handles = handles + 1
      h.owned = {owner = h, disarmed = false, id = handles}
      return h.owned
    end
    if a and a.patch >= 0 then apply_patch(a.patch) end
    if not a or a.code == 1 then return true, own() end
    if a.code == 2 then own(); return false, a.text end
    if a.code == 98 then own(); error(a.text or 'raised', 0) end
    if a.code == 99 then error(a.text or 'raised', 0) end
    if a.code == 0 then return false, a.text end
    return nil, a.text
  end
  function h.owned_handle()
    local a = took(port('owned_handle'))
    if not a then return h.owned end
    if a.code == 3 then return {owner = h, disarmed = false, id = 0} end
    return nil
  end
  function h.finish(_, handle)
    local a = took(port('finish', 'h' .. N(handle and handle.id)))
    if a and a.code == 0 then return false, a.text end
    if a and a.code == -1 then return nil, a.text end
    if handle then handle.disarmed = true end
    if h.owned == handle then h.owned = nil end
    if a and a.code == 4 then return true, a.text end
    if a and a.code == 1 then return true, stats_table(a, true) end
    return true
  end
  function h.stats(_, handle)
    local a = took(port('stats', 'h' .. N(handle and handle.id)))
    if not a then return stats_table({nums = {}}) end
    if a.code == 1 then return stats_table(a) end
    if a.code == 0 then return nil, a.text end
    return false, a.text
  end
  function h.set_placement(_, handle, x, y, z)
    return plain(port('set_placement', 'h' .. N(handle and handle.id) .. ' ' .. N(x) .. ' ' .. N(y) .. ' ' .. N(z)))
  end
  function h.set_zoom_policy(_, handle, active)
    return plain(port('set_zoom_policy', 'h' .. N(handle and handle.id) .. ' ' .. N(active)))
  end
  function h.set_ads_placement(_, handle, x, y, z)
    return plain(port('set_ads_placement', 'h' .. N(handle and handle.id) .. ' ' .. N(x) .. ' ' .. N(y) .. ' ' .. N(z)))
  end
  function h.transition(_, handle, token)
    return plain(port('transition', 'h' .. N(handle and handle.id) .. ' ' .. N(token.epoch) .. ' ' .. N(token.progress) .. ' ' .. N(token.ads)))
  end
  function h.set_motion(_, handle, dx, dy, dz, qx, qy, qz, qw, authored)
    return plain(port('set_motion', table.concat({'h' .. N(handle and handle.id), N(dx), N(dy), N(dz), N(qx), N(qy), N(qz), N(qw), N(authored)}, ' ')))
  end
  h.methods = {owned_handle = h.owned_handle, transition = h.transition}
  return h
end

-- ---- the retail modules -----------------------------------------------------------------
local function load_retail(path, chunk)
  local file = assert(io.open(kit .. '/' .. path, 'rb'))
  local source = file:read('*a')
  file:close()
  return assert(loadstring(source, '=' .. chunk))
end
local Profile, ActionState
mod = {require = function(path)
  if path:match('compat51') then return PORT51 end
  if path:match('profile%.lua') then return Profile end
  if path:match('action_state%.lua') then return ActionState end
  local tag = path:match('pose%.lua') and 'P' or path:match('alignment%.lua') and 'A'
  assert(tag, 'unexpected module ' .. path)
  return {new = function()
    T('load ' .. tag)
    took(answer('load_' .. tag))
    return helpers[tag]
  end}
end}
local module_loader = mod.require
Profile = load_retail('src/first_person/profile.lua', 'P')()
ActionState = load_retail('src/first_person/action_state.lua', 'A')()
local Backend = load_retail('src/first_person/backend.lua', 'B')()
local shipped_camera = Profile.camera

local covered = {B = {}, A = {}}
if coverage_path then
  -- Lines that carry code are taken from every function as it is first called (activelines);
  -- a function never called shows up in neither list and is named by the coverage report.
  local seen = {}
  debug.sethook(function(event, line)
    local info = debug.getinfo(2, event == 'call' and 'SL' or 'S')
    local set = covered[info.source:sub(2)]
    if not set or info.source:sub(1, 1) ~= '=' then return end
    if event == 'call' then
      local key = info.source .. info.linedefined
      if not seen[key] then
        seen[key] = true
        for active in pairs(info.activelines) do set[active] = set[active] or 0 end
      end
    else set[line] = (set[line] or 0) + 1 end
  end, 'cl')
end

-- ---- slots and printing -----------------------------------------------------------------
local backend, snaps, leases, tokens, actions
local function slot_of(list, value)
  if value == nil then return '-' end
  for i = 0, 62 do if list[i] == value then return tostring(i) end end
  return '?'
end
local function same_list(field, s)
  local out = {}
  for i = 0, 15 do
    local other = snaps[i]
    if other and other[field] == s[field] then out[#out + 1] = i end
  end
  return table.concat(out, ',')
end
local function action_text(a)
  if a == nil then return '-' end
  return table.concat({N(a.body), N(a.body_object), N(a.graph), N(a.instance), N(a.layer_count), a.resource, N(a.resource_match),
    N(a.l1), N(a.l3), N(a.l10), N(a.l11), N(a.l14), 'u' .. table.concat(a.unknown_layers, ','), N(a.sprint_hypothesis)}, ' ')
end
local function snapshot_text(s)
  local parts = {'k=' .. N(s.camera_kind), 'h=' .. N(s.head), 't=' .. N(s.tail), 'rec=' .. N(s.record - s.context),
    'ra=' .. N(s.request_avatar), 'rw=' .. N(s.request_weapon), 'ctx=' .. N(s.context), 'cam=' .. N(s.camera), 'world=' .. N(s.world),
    'ctl=' .. N(s.controller), 'cb=' .. hex(s.controller_bytes), 'ca=' .. N(s.controller_active), 'bl=' .. N(s.blocked),
    'av=' .. N(s.avatar), 'ad=' .. hex(s.avatar_descriptor), 'rl=' .. N(s.request_local), 'own=' .. N(s.avatar_owner),
    'ai=' .. N(s.avatar_index), 'body=' .. N(s.body), 'fa=' .. N(s.flags_address - s.avatar_owner - s.avatar_index * run.stride - run.flags),
    'fl=' .. hex(s.flags), 'vm=' .. N(s.viewmodel), 'aim=' .. N(s.aiming), 'sh=' .. N(s.shoulder_aim), 'seat=' .. N(s.seated),
    'rel=' .. N(s.reloading), 'safe=' .. N(s.safe), 'wt=' .. N(s.weapon_transition), 'hct=' .. N(s.head_camera_transition),
    'pin=' .. N(s.pin_camera), 'cu=' .. N(s.camera_unit), 'fc=' .. N(s.foreign_camera), 'why=' .. Q(s.reason),
    'w=' .. N(s.weapon), 'wu=' .. N(s.weapon_unit), 'wcd=' .. N(s.weapon_component_descriptor), 'wd=' .. hex(s.weapon_descriptor),
    'wr=' .. N(s.weapon_ready), 'es=' .. N(s.engine_safe), 'npe=' .. N(s.native_pose_eligible),
    'act=' .. action_text(s.action_state), 'ae=' .. Q(s.action_state_error),
    'id=' .. same_list('identity', s), 'pid=' .. same_list('player_identity', s)}
  return table.concat(parts, ' ')
end
local function stats_text(s)
  if s == nil then return '-' end
  return N(s.armed) .. ',' .. N(s.expired) .. ',' .. N(s.error) .. ',' .. N(s.target_weight) .. ',' .. N(s.blend)
end
local function placement_text(p)
  if p == nil then return '-' end
  return table.concat({N(p.x), N(p.y), N(p.z), N(p.yaw), N(p.pitch), N(p.roll)}, ',')
end
local shown = {}
local function state_text()
  local b = backend
  local contract = b.renderer_dof_contract
  local out = {table.concat({'B e=' .. N(b.frame_epoch), 'a=' .. N(b.frame_active), 'al=' .. slot_of(leases, b.alignment_lease),
    'wl=' .. slot_of(leases, b.warm_lease), 'pp=' .. placement_text(b.pose_placement), 'key=' .. N(b.placement_weapon_key and 1) ..
    (b.placement_weapon_key or ''), 'keeps=' .. N(b.aim_view_keeps), 'det=' .. N(b.seated_detaches),
    'ppe=' .. N(b.presentation_pose_epoch), 'ppl=' .. slot_of(leases, b.presentation_pose_lease),
    'pub=' .. N(b.pose_phase_publication ~= nil), 'coi=' .. N(b.camera_only_identity and b.camera_only_identity.hammer),
    'rdc=' .. N(contract ~= nil) .. N(contract and contract.indices ~= nil) ..
      N(contract and contract.indices and contract.indices[0x06f1d0d3]) .. N(contract and contract.indices and contract.indices[0x72e8f18c]),
    'ph=' .. N(helpers.P.owned and helpers.P.owned.id), 'ah=' .. N(helpers.A.owned and helpers.A.owned.id),
    'pose=' .. N(b.pose ~= nil), 'align=' .. N(b.alignment ~= nil)}, ' ')}
  for i = 0, 62 do
    local l = leases[i]
    if l then
      local a = l.alignment
      local text = table.concat({'L' .. i, 'o=' .. N(l.origin.camera_kind) .. ',' .. N(l.origin.tail),
        'req=' .. N(l.request and l.request - l.origin.context), 'boot=' .. N(l.bootstrap), 'own=' .. N(l.owner ~= nil),
        'done=' .. N(l.done), 'cu=' .. N(l.cleanup_only), 'err=' .. Q(l.error), 'pbe=' .. N(l.pose_blend_entry),
        'al=' .. (a and (a.kind .. ',' .. N(a.ended) .. ',h' .. N(a.handle and a.handle.id) .. ',n' .. N(a.prepared and a.prepared.id) ..
          ',' .. a.helper.tag) or '-'),
        'eac=' .. N(l.entry_aim_changed), 'ecc=' .. N(l.entry_controller_changed), 'bac=' .. N(l.body_event_aim_changed),
        'bcc=' .. N(l.body_event_controller_changed), 'as=' .. stats_text(l.alignment_stats), 'ps=' .. stats_text(l.pose_stats),
        'pe=' .. N(l.pose_error), 'por=' .. N(l.pose_owner_retired), 'prr=' .. Q(l.pose_retire_reason), 'op=' .. N(l.orphan_polls),
        'bpa=' .. N(l.body_pose_attempted), 'bpr=' .. N(l.body_pose_released)}, ' ')
      if shown[i] ~= text then
        shown[i] = text
        out[#out + 1] = text
      end
    end
  end
  local used = {}
  for i = 0, 62 do
    local t = tokens[i]
    if t then
      local record = (backend.pose_tokens and backend.pose_tokens[t]) or (backend.alignment_tokens and backend.alignment_tokens[t])
        or (backend.body_tokens and backend.body_tokens[t])
      used[#used + 1] = i .. '=' .. N(record and record.used)
    end
  end
  local text = 'T ' .. table.concat(used, ' ')
  if shown.tokens ~= text then
    shown.tokens = text
    out[#out + 1] = text
  end
  return table.concat(out, '\n')
end
local function result(...)
  local n = select('#', ...)
  local first, second = ...
  local text = '= ' .. N(first)
  if n > 1 and second ~= nil and type(second) ~= 'table' then
    text = text .. ' ' .. (type(second) == 'string' and Q(second) or N(second))
  end
  T(text)
  return ...
end
-- Runs a method; a Lua error that leaves it is an answer of its own ("R").
local function guarded(fn, ...)
  local packed = {pcall(fn, ...)}
  if not packed[1] then
    T('= R ' .. Q(packed[2]))
    return nil, nil, true
  end
  return result(unpack(packed, 2, table.maxn(packed)))
end
local function placement_arg(a, at)
  local mask = a[at]
  if mask < 0 then return nil end
  local p = {}
  for i, name in ipairs({'x', 'y', 'z', 'yaw', 'pitch', 'roll'}) do
    if bit.band(mask, bit.lshift(1, i - 1)) ~= 0 then p[name] = a[at + i] end
  end
  return p
end
local REASONS = {[0] = nil, 'initial', 'weapon', 'ads_enter', 'ads_leave', 'zoom', 'shoulder', 'action', 'seat', 'camera', 'reacquire'}
local function phase_token(a, at)
  if a[at] < 0 then return nil end
  local function identity(slot, field)
    if slot == -1 then return '' end
    if slot == -2 then return 'another' end
    return snaps[slot] and snaps[slot][field] or ''
  end
  local function number(v) if v == v then return v end end
  local function flag(v) if v == 0 then return false elseif v == 1 then return true end end
  return {epoch = number(a[at + 1]), progress = number(a[at + 2]), ads = flag(a[at + 3]), identity = identity(a[at + 4], 'identity'),
    player_identity = identity(a[at + 5], 'player_identity'), started = number(a[at + 6]), duration = number(a[at + 7]),
    continuous = flag(a[at + 8]), reason = REASONS[a[at + 9]], action = a[at + 10] >= 0 and a[at + 10] or nil}
end
local function lease_arg(index) if index >= 0 then return leases[index] end end
local function snap_arg(index) if index >= 0 then return snaps[index] end end
local function token_arg(index) if index >= 0 then return tokens[index] end end
local function keep(list, index, ok, value)
  if ok == true and type(value) == 'table' and index < 63 then list[index] = value end
end
local function config_result(c, why, raised)
  if raised then return end
  if not c then return end
  trace[#trace] = '= ' .. table.concat({address_name(c.game), address_name(c.exe), N(c.context), N(c.camera), N(c.avatar_owner),
    N(c.request and c.request - c.context), N(c.avatar_index), N(c.avatar), N(c.body), N(c.weapon), N(c.pin_view), N(c.pin_look),
    N(c.presentation_enabled), N(c.kick_enabled), N(c.camera_unit), N(c.camera_kind), N(c.forward), N(c.up), N(c.right),
    N(c.smoothing_ms), N(c.blend_ms), N(c.max_gap_ms)}, ' ')
end
local function context_text(c)
  return table.concat({address_name(c.exe), N(c.unit), N(c.index), N(c.generation), N(c.object), N(c.registry), N(c.units),
    N(c.generations), N(c.owner), N(c.avatar_index), N(c.weapon_descriptor), hex(c.weapon_bytes), N(c.weapon_unit),
    N(c.weapon_index), N(c.weapon_generation), N(c.weapon_object)}, ' ')
end

local getters = {}
local OPS = {}
function OPS.new(a)
  state.wine = a[1] == 1
  helpers = {P = make_helper('P'), A = make_helper('A')}
  backend = Backend.new(function(text) T('log ' .. clean(text)) end, nil, a[3] == 1 and helpers.A or nil, a[2] == 1 and helpers.P or nil)
  snaps, leases, tokens, actions, shown = {}, {}, {}, {}, {}
  natives, handles = 0, 0
end
function OPS.patch(a) apply_patch(a[1]) end
local function first_image()
  mem = {}
  for _, row in ipairs(run.init) do
    for i = 1, #row[2], 2 do mem[row[1] + (i - 1) / 2] = tonumber(row[2]:sub(i, i + 1), 16) end
  end
  state.game, state.exe = run.game, run.exe
end
function OPS.load(a)
  first_image()
  apply_patch(a[1])
  armed = {}
  for _, p in pairs(ports) do p.fifo = {} end
end
function OPS.trap(a)
  if a[1] < 0 then armed = {}
  else armed[#armed + 1] = {at = run.traps[a[1] + 1][1], size = run.traps[a[1] + 1][2], patch = a[2], count = a[3]} end
end
function OPS.ans(a, s)
  local name, text = s:match('^([^|]*)|(.*)$')
  name = name or s
  local nums = {}
  for i = 4, #a do nums[#nums + 1] = a[i] end
  local entry = {code = a[1], patch = a[2], nums = nums, text = text}
  ports[name] = ports[name] or {fifo = {}}
  if a[3] == 1 then ports[name].def = entry elseif a[3] == 2 then ports[name].def = nil; ports[name].fifo = {}
  else table.insert(ports[name].fifo, entry) end
end
function OPS.hook(a, s)
  local on = a[1] == 1
  if s == 'foreign_camera' then
    backend.foreign_camera = on and function()
      T('hook foreign_camera')
      local x = took(answer('foreign_camera'))
      if not x or x.code == 0 then return false end
      if x.code == 1 then return true end
      return nil
    end or nil
  elseif s == 'camera_before_entry' then
    backend.camera_before_entry = on and function()
      T('hook camera_before_entry')
      local x = took(answer('camera_before_entry'))
      if not x or x.code == 1 then return true end
      if x.code == 0 then return false, x.text end
      return nil, x.text
    end or nil
  elseif s == 'placement_resolver' then
    backend.placement_resolver = on and function(key)
      T('hook placement_resolver ' .. tostring(key))
      local x = took(answer('placement_resolver'))
      if not x or x.code ~= 1 then return nil end
      return placement_arg(x.nums, 1)
    end or nil
  elseif s == 'presentation_transition' then
    backend.presentation_transition = on and function()
      T('hook presentation_transition')
      local x = took(answer('presentation_transition'))
      if not x or x.code ~= 1 then return nil end
      return phase_token(x.nums, 1)
    end or nil
  elseif s == 'P.owned_handle' or s == 'P.transition' then
    local name = s:sub(3)
    helpers.P[name] = on and helpers.P.methods[name] or nil
  else error('unknown hook ' .. s) end
end
function OPS.set(a, s)
  if s == 'region' then state.region = {state = a[1], kind = a[2], protect = a[3], allocation = a[4], answered = a[5] == 1}
  elseif s == 'keep_aim_view' then shipped_camera.keep_aim_view = a[1] == 1
  elseif s == 'seated_head' then shipped_camera.seated_head = a[1] == 1
  elseif s == 'camera_profile' then Profile.camera = a[1] == 1 and shipped_camera or nil
  elseif s == 'loader' then mod.require = a[1] == 1 and module_loader or nil
  elseif s == 'support_grip_tracking' then backend.support_grip_tracking = a[1] == 1 or nil
  elseif s == 'fird' then
    backend.fird_camera_active = a[1] == 1 or nil
    backend.fird_camera_offset = a[2] == 1 and {x = a[3], y = a[4], z = a[5]} or nil
  else error('unknown setting ' .. s) end
end
function OPS.begin_frame() backend:begin_frame() end
function OPS.end_frame() backend:end_frame() end
function OPS.camera_ready() guarded(backend.camera_ready, backend) end
function OPS.snap(a)
  local method = ({[0] = 'snapshot', 'player_snapshot', 'pin_player_snapshot'})[a[1]]
  local s, _, raised = guarded(backend[method], backend)
  if s and not raised then
    snaps[a[2]] = s
    trace[#trace] = '= ' .. snapshot_text(s)
  end
end
function OPS.backpack(a)
  local b, _, raised = guarded(backend.backpack, backend, snap_arg(a[1]))
  if b and not raised then trace[#trace] = '= ' .. N(b.ref) .. ' ' .. N(b.entity) .. ' ' .. hex(b.identity:match(':(.*)$')) end
end
function OPS.camera_only(a) guarded(backend.camera_only_weapon, backend, snap_arg(a[1])) end
function OPS.action(a)
  local action, _, raised = guarded(backend.action_state, backend, snap_arg(a[1]))
  if action and not raised then
    actions[a[2]] = action
    trace[#trace] = '= ' .. action_text(action)
  end
end
function OPS.on_foot(a) guarded(backend.on_foot, backend, snap_arg(a[1]), a[2] >= 0 and actions[a[2]] or nil) end
function OPS.camera_config() config_result(guarded(backend.camera_config, backend)) end
function OPS.fov_config(a) config_result(guarded(backend.fov_config, backend, lease_arg(a[1]))) end
function OPS.blur_config(a) config_result(guarded(backend.blur_config, backend, lease_arg(a[1]))) end
function OPS.fird_fov_config() config_result(guarded(backend.fird_fov_config, backend)) end
function OPS.warm_start(a) keep(leases, a[2], guarded(backend.warm_start, backend, snap_arg(a[1]))) end
function OPS.warm_finish(a) guarded(backend.warm_finish, backend, lease_arg(a[1])) end
function OPS.presentation(a) guarded(backend._invoke_presentation, backend, snap_arg(a[1]), a[2] == 1) end
function OPS.prepare_pose(a) keep(tokens, a[2], guarded(backend.prepare_pose, backend, snap_arg(a[1]))) end
function OPS.prepare_alignment(a) keep(tokens, a[2], guarded(backend.prepare_alignment, backend, snap_arg(a[1]))) end
function OPS.prepare_body_pose(a) keep(tokens, a[2], guarded(backend.prepare_body_pose, backend, snap_arg(a[1]))) end
function OPS.enter(a) keep(leases, a[3], guarded(backend.enter, backend, snap_arg(a[1]), token_arg(a[2]))) end
function OPS.enter_pose(a)
  local blend = nil
  if a[4] == 0 then blend = false elseif a[4] == 1 then blend = true elseif a[4] > 1 then blend = a[4] end
  keep(leases, a[5], guarded(backend.enter_pose, backend, snap_arg(a[1]), token_arg(a[2]), a[3], blend))
end
function OPS.set_placement_defaults(a) guarded(backend.set_placement_defaults, backend, placement_arg(a, 1)) end
function OPS.set_pose_placement(a) guarded(backend.set_pose_placement, backend, lease_arg(a[1]), placement_arg(a, 2)) end
function OPS.set_ads_placement(a) guarded(backend.set_ads_placement, backend, lease_arg(a[1]), placement_arg(a, 2)) end
function OPS.set_pose_zoom_policy(a) guarded(backend.set_pose_zoom_policy, backend, lease_arg(a[1]), a[2] == 1) end
function OPS.set_pose_transition(a) guarded(backend.set_pose_transition, backend, lease_arg(a[1]), phase_token(a, 2)) end
function OPS.set_pose_motion(a)
  local value
  if a[2] >= 0 then
    value = {dx = a[3], dy = a[4], dz = a[5], authored_ads = a[11] == 1}
    for i, name in ipairs({'qx', 'qy', 'qz', 'qw'}) do
      if bit.band(a[2], bit.lshift(1, i - 1)) ~= 0 then value[name] = a[6 + i] end
    end
  end
  guarded(backend.set_pose_motion, backend, lease_arg(a[1]), value)
end
local function status(method, a)
  local stats, _, raised = guarded(backend[method], backend, lease_arg(a[1]))
  if stats and not raised then trace[#trace] = '= ' .. stats_text(stats) end
end
function OPS.pose_status(a) status('pose_status', a) end
function OPS.alignment_status(a) status('alignment_status', a) end
function OPS.release_body_pose(a) guarded(backend.release_body_pose, backend, lease_arg(a[1]), token_arg(a[2])) end
function OPS.keep_aim_view(a) guarded(backend.keep_aim_view, backend, snap_arg(a[1])) end
function OPS.detach_seated_camera() guarded(backend.detach_seated_camera, backend) end
function OPS.release(a) guarded(backend.release, backend, lease_arg(a[1])) end
function OPS.renderer(a)
  local function binding(kind, which)
    if kind == 0 then return nil end
    local key = kind .. ':' .. which
    if not getters[key] then
      getters[key] = function() end
    end
    function_addresses[getters[key]] = state.exe + run.renderer[which]
    return getters[key]
  end
  local setting = ({[0] = 'dof_enabled', 'vignette_enabled', 'bloom_enabled'})[a[5]]
  guarded(backend.renderer_dof_value, backend, binding(a[1], a[2]), binding(a[3], a[4]), setting)
end
function OPS.context(a)
  local s = snap_arg(a[1])
  if not s then T('= -'); return end
  local fn = a[2] == 3 and backend._pose_context or backend._alignment_context
  local subject = s
  if s and a[2] == 1 then subject = {viewmodel = s.body, avatar_owner = s.avatar_owner, avatar_index = s.avatar_index} end
  if s and a[2] == 2 then subject = {viewmodel = s.weapon_unit, avatar_owner = s.avatar_owner, avatar_index = s.avatar_index} end
  local packed = {pcall(fn, backend, subject, a[2] ~= 3 and Profile.pose or nil)}
  if not packed[1] then T('= R ' .. Q(packed[2])) else T('= ' .. context_text(packed[2])) end
end

local function raw(show, ok, value)
  if not ok then T('= R ' .. Q(value)) else T('= ' .. show(value)) end
end
function OPS.module(a) raw(address_name, pcall(backend._module, backend, ({[0] = 'full', 'identity', 'entry'})[a[1]])) end
function OPS.executable(a)
  local profile = a[1] == 1 and Profile.pose or a[1] == 2 and shipped_camera or nil
  raw(address_name, pcall(backend._alignment_executable, backend, profile))
end
function OPS.invalidate() backend:_invalidate_frame_checks() end
function OPS.map(a)
  local s = snap_arg(a[4])
  if not s then T('= -'); return end
  local at = a[1] == 0 and s.world + run.entity_map or a[1] == 1 and s.avatar_owner + 0xf8 or s.world + run.light_map
  raw(N, pcall(backend._map, backend, at, a[2], a[3] >= 0 and a[3] or nil))
end
function OPS.descriptor(a)
  local s = snap_arg(a[2])
  if not s then T('= -'); return end
  raw(hex, pcall(backend._descriptor, backend, s.world, a[1]))
end
function OPS.peek(a)
  local s = snap_arg(a[3])
  if not s then T('= -'); return end
  local at = ({[0] = s.controller, s.context, s.context + 0x1f8, 4096, s.camera, 140737488355328 - 4})[a[2]]
  if a[1] == 0 then raw(N, pcall(backend._u8, backend, at))
  elseif a[1] == 1 then raw(N, pcall(backend._u32, backend, at))
  elseif a[1] == 2 then raw(N, pcall(backend._ptr, backend, at))
  else raw(function(bytes) return #bytes .. ':' .. hex(bytes:sub(1, 8)) end, pcall(backend._read, backend, at, ({[3] = 6, [4] = 0, [5] = 1025, [6] = 1024})[a[1]])) end
end

-- ---- main -------------------------------------------------------------------------------
local runs = dofile(script_path)
for _, r in ipairs(runs) do
  run = r
  mem, trace, ports, getters, armed = {}, {}, {}, {}, {}
  state = {game = 0, exe = 0, wine = false, region = {state = 0x1000, kind = 0x1000000, protect = 0x20, allocation = 1, answered = true}}
  Profile.camera = shipped_camera
  Profile.camera.keep_aim_view, Profile.camera.seated_head = true, true
  mod.require = module_loader
  first_image()
  io.write('RUN ', r.name, '\n')
  for index, step in ipairs(r.steps) do
    trace = {}
    local op = assert(OPS[step[1]], 'unknown op ' .. tostring(step[1]))
    local ok, problem = pcall(op, step[2] or {}, step[3])
    if not ok then error(r.name .. ' step ' .. index .. ' (' .. step[1] .. '): ' .. tostring(problem), 0) end
    if backend then T(state_text()) end
    io.write(table.concat(trace, '\n'), '\n.\n')
  end
end
if coverage_path then
  debug.sethook()
  local out = assert(io.open(coverage_path, 'w'))
  for chunk, set in pairs(covered) do
    for line, count in pairs(set) do out:write(chunk, ' ', line, ' ', count, '\n') end
  end
  out:close()
end

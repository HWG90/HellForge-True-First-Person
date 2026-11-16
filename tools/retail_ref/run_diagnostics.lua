-- Drives the retail diagnostics.lua, unmodified, under LuaJIT (see effects_common.lua).
-- Usage: luajit run_diagnostics.lua <Goose Gadget Kit folder>
-- The module calls nothing outside itself, so the trace stays empty; the world holds the sample:
-- slot f_<field> is the value of that field. The text "<table>" stands for a Lua table (a value
-- that is neither Boolean, number nor string), in a field, as the frame, the name or the sample.
package.path = arg[0]:gsub('[^/]*$', '') .. '?.lua;' .. package.path
local F = require('effects_common')
local NAMES = {'time', 'enabled', 'player', 'weapon', 'request', 'camera_kind', 'ads', 'foreign', 'editor', 'phase', 'transition_epoch',
  'transition_progress', 'pinned', 'pin_expected', 'camera_retired', 'view_expected', 'view_visible', 'recovering', 'fov_native',
  'fov_applied', 'camera_blend', 'pose_blend', 'camera_epoch', 'fov_epoch', 'pose_epoch', 'look_sequence', 'look_samples', 'look_skipped',
  'cleanup_pending', 'reason', 'policy', 'event', 'placement_x', 'placement_y', 'placement_z', 'placement_yaw', 'placement_pitch',
  'placement_roll', 'motion_x', 'motion_y', 'motion_z', 'look_yaw', 'look_pitch', 'ads_rebases', 'control_mask', 'control_guard', 'safe',
  'aiming', 'snapshot_reason', 'weapon_ready', 'entry_block', 'action', 'softened', 'head_calls', 'camera_gate', 'head_phase', 'cam_x',
  'cam_y', 'cam_z', 'cam_fx', 'cam_fy', 'cam_fz', 'vm_lx', 'vm_ly', 'vm_lz', 'vm_cx', 'vm_cy', 'vm_cz', 'gun_cx', 'gun_cy', 'gun_cz',
  'gun_turn', 'head_distance'}
local COUNTERS = {'frames', 'duplicate_frames', 'out_of_order_frames', 'events', 'overwritten', 'player_changes', 'weapon_changes',
  'request_changes', 'camera_changes', 'transition_restarts', 'mixed_publication_frames', 'unexpected_view_frames', 'look_duplicates',
  'cleanup_frames'}
local KEPT = {'player', 'weapon', 'request', 'camera_kind', 'transition_epoch', 'transition_progress', 'look_sequence', 'look_samples'}
do local slots = {}; for i, name in ipairs(NAMES) do slots[i] = 'f_' .. name end; F.slots(slots) end
local M = F.load('diagnostics.lua')
local function upvalue(fn, wanted)
  for i = 1, 60 do
    local name, value = debug.getupvalue(fn, i)
    if not name then return nil end
    if name == wanted then return value end
  end
end
do -- the field list above must be retail's
  local fields, seen, count = upvalue(upvalue(M.new({}).record, 'row_of'), 'fields'), {}, 0
  for _, name in ipairs(NAMES) do assert(fields[name] and not seen[name], name); seen[name] = true end
  for _ in pairs(fields) do count = count + 1 end
  assert(count == #NAMES, 'field list differs from retail')
end

local TABLE = '<table>'
local function value(v) if v == TABLE then return {} end; return v end
local function sample()
  local t = {}
  for _, name in ipairs(NAMES) do t[name] = value(F.world['f_' .. name]) end
  return t
end
local function row_text(row)
  local out = {'{' .. F.show(row.frame)}
  for _, name in ipairs(NAMES) do if row[name] ~= nil then out[#out + 1] = name .. '=' .. F.show(row[name]) end end
  return table.concat(out, ' ') .. '}'
end
local d
function F.dispatch(call, a)
  local out
  if call == 'new' then            -- a[1]: nil = no options table; else a[2] is the capacity
    local ok, object = pcall(M.new, a[1] and {capacity = value(a[2])} or nil)
    if not ok then return 'raised ' .. F.show(object) end
    d = object; out = 'new'
  elseif call == 'record' or call == 'event' then
    -- record: a[1] frame, a[2] true = the world's sample, else that value as the sample.
    -- event: a[1] frame, a[2] name, a[3] as record's a[2].
    local pick = call == 'record' and a[2] or a[3]
    local s = pick
    if pick == true then s = sample() else s = value(pick) end
    local ok, r
    if call == 'record' then ok, r = pcall(d.record, d, value(a[1]), s) else ok, r = pcall(d.event, d, value(a[1]), value(a[2]), s) end
    out = ok and F.show(r) or 'raised ' .. F.show(r)
    if ok and r == true then
      local snap = d:snapshot()
      out = out .. ' ' .. row_text(snap.frames[snap.count])
    end
  elseif call == 'clear' then d:clear(); out = 'clear'
  elseif call == 'snapshot' then
    local s = d:snapshot()
    local rows = {}
    for i = 1, s.count do rows[i] = row_text(s.frames[i]) end
    assert(#s.frames == s.count)
    out = 'snapshot ' .. F.show(s.capacity) .. ' ' .. F.show(s.count) .. ' ' .. table.concat(rows, ' ')
  else error(call) end
  local totals, previous, last = upvalue(d.record, 'totals'), upvalue(d.record, 'previous'), upvalue(d.record, 'last_frame')
  local snap, parts = d:snapshot(), {}
  for i, name in ipairs(COUNTERS) do assert(totals[name] == snap.counters[name]); parts[i] = F.num(totals[name]) end
  local kept = {}
  for i, name in ipairs(KEPT) do kept[i] = previous and F.show(previous[name]) or '-' end
  return table.concat({out, '|', F.show(snap.count), table.concat(parts, ' '), F.show(last), '|', table.concat(kept, ' ')}, ' ')
end

local LONG = string.rep('abcdefghij', 30)
local function f(name, v) return {slot = 'f_' .. name, v = v} end
local function ops(list) local e = {}; for name, v in pairs(list) do e[#e + 1] = f(name, v) end
  table.sort(e, function(x, y) return x.slot < y.slot end); return e end
local function clear_all() local e = {}; for i, name in ipairs(NAMES) do e[i] = {slot = 'f_' .. name, v = nil} end; return e end

F.run('life', function()
  F.op('new', {true, 16})
  F.op('snapshot')
  F.op('record', {1, true}, ops({time = 1.25, enabled = true, player = 'p1', weapon = 'w1', request = 'r1', camera_kind = 6, phase = 'running',
    transition_epoch = 1, transition_progress = .25, fov_epoch = 1, pose_epoch = 1, camera_epoch = 1, look_sequence = 5, look_samples = 2}))
  F.op('record', {1, true}); F.op('record', {0, true}); F.op('record', {2, true})
  F.op('record', {3, true}, ops({player = 'p2', weapon = 'w2', request = 'r2', camera_kind = 4}))
  F.op('record', {4, true}, ops({transition_epoch = 2}))                 -- a restart inside an unfinished transition
  F.op('record', {5, true}, ops({transition_progress = 1})); F.op('record', {6, true}, ops({transition_epoch = 3}))
  F.op('record', {7, true}, ops({transition_progress = false})); F.op('record', {8, true}, ops({transition_epoch = 4}))
  F.op('record', {9, true}, ops({look_samples = 3})); F.op('record', {10, true}, ops({look_samples = 4, foreign = true}))
  F.op('record', {11, true}, ops({look_samples = 5, foreign = false, editor = true}))
  F.op('record', {12, true}, ops({look_samples = 6, editor = false, enabled = false}))
  F.op('record', {13, true}, ops({look_samples = 7, enabled = true, look_sequence = 6})); F.op('record', {14, true}, ops({look_samples = 7}))
  F.op('record', {15, true}, ops({pin_expected = true, pinned = false})); F.op('record', {16, true}, ops({foreign = true}))
  F.op('record', {17, true}, ops({foreign = false, editor = true})); F.op('record', {18, true}, ops({editor = false, recovering = true}))
  F.op('record', {19, true}, ops({recovering = false, pinned = true, view_expected = true, view_visible = false}))
  F.op('record', {20, true}, ops({view_visible = true, fov_epoch = 2})); F.op('record', {21, true}, ops({recovering = true}))
  F.op('record', {22, true}, ops({recovering = false, fov_epoch = 1, camera_epoch = 2})); F.op('record', {23, true}, ops({camera_epoch = false}))
  F.op('record', {24, true}, ops({cleanup_pending = true})); F.op('record', {25, true}, ops({cleanup_pending = 0}))
  F.op('event', {25, 'policy_failure', true}); F.op('event', {0, 'x', true}); F.op('event', {1.5, 'x', true}); F.op('event', {3, 7, true})
  F.op('event', {3, LONG, false}); F.op('event', {3, 'named', nil}); F.op('event', {3, 'tabled', TABLE}); F.op('event', {'3', 'x', true})
  F.op('snapshot'); F.op('clear'); F.op('snapshot'); F.op('record', {1, true}); F.op('snapshot')
  -- samples and frames that are refused
  F.op('record', {2, nil}); F.op('record', {2, false}); F.op('record', {2, 5}); F.op('record', {2, 'sample'}); F.op('record', {2, TABLE})
  F.op('record', {nil, true}); F.op('record', {'4', true}); F.op('record', {TABLE, true}); F.op('record', {4.5, true})
  F.op('record', {0 / 0, true}); F.op('record', {math.huge, true}); F.op('record', {-math.huge, true}); F.op('record', {-3, true})
  F.op('record', {1e300, true}); F.op('record', {1e300, true}); F.op('record', {5, true}); F.op('snapshot')
  -- values that are not copied, and texts cut to 200 / 64 bytes
  F.op('clear', nil, clear_all())
  F.op('record', {1, true}, ops({time = 0 / 0, fov_native = math.huge, fov_applied = -math.huge, reason = LONG, policy = TABLE, action = '',
    phase = LONG:sub(1, 200), entry_block = LONG:sub(1, 201), snapshot_reason = 'nul\0byte', cam_x = -0.0, cam_y = 1e-320, event = 'own'}))
  F.op('event', {9, LONG:sub(1, 64), true}); F.op('event', {9, LONG:sub(1, 65), true}); F.op('event', {9, '', true}); F.op('snapshot')
  -- comparisons that raise in retail: the frame is counted, the row is not kept
  F.op('clear', nil, clear_all())
  F.op('record', {1, true}, ops({transition_epoch = 1, transition_progress = 'half', look_sequence = 1, look_samples = 1, enabled = true}))
  F.op('record', {2, true}, ops({transition_epoch = 2})); F.op('record', {3, true}, ops({transition_progress = true})); F.op('snapshot')
  F.op('record', {4, true}, ops({transition_epoch = 1})); F.op('record', {5, true}, ops({transition_epoch = 2}))
  F.op('record', {6, true}, ops({transition_epoch = 1}))          -- the same epoch again: no comparison, the row is kept
  F.op('clear', nil, clear_all())
  F.op('record', {1, true}, ops({look_sequence = 1, look_samples = 'a', enabled = true}))
  F.op('record', {2, true}, ops({look_samples = 'b'})); F.op('record', {3, true}, ops({look_samples = 'a'}))
  F.op('record', {4, true}, ops({look_samples = 2})); F.op('record', {5, true}, ops({look_samples = 'a'}))
  F.op('record', {6, true}, ops({look_samples = 2})); F.op('record', {7, true}, ops({look_sequence = 2}))
  F.op('record', {8, true}, ops({look_samples = 'a'})); F.op('record', {9, true}, ops({look_sequence = 1}))
  F.op('record', {10, true}, ops({look_samples = true})); F.op('record', {11, true}); F.op('record', {12, true}, ops({look_samples = 3}))
  F.op('record', {13, true}, ops({look_sequence = 2, look_samples = 'ab'})); F.op('record', {14, true}, ops({look_samples = 'a\0b'}))
  F.op('record', {15, true}, ops({look_samples = 'a\0c'})); F.op('record', {16, true}, ops({look_samples = 'a'}))
  F.op('record', {17, true}, ops({look_samples = 'a\0'})); F.op('record', {18, true}, ops({look_samples = '\255'}))
  F.op('record', {19, true}, ops({look_samples = 'z'})); F.op('record', {20, true}, ops({look_samples = '\255\255'}))
  F.op('record', {21, true}, ops({look_samples = ''})); F.op('record', {22, true}, ops({look_samples = 'a'}))
  F.op('record', {23, true}, ops({look_samples = 0})); F.op('record', {24, true}, ops({look_samples = 1e-9}))
  F.op('record', {25, true}, ops({look_samples = -1}))
  F.op('record', {26, true}, ops({look_sequence = 3, look_samples = true})); F.op('record', {27, true}); F.op('record', {28, true}, ops({look_samples = 5}))
  F.op('record', {29, true}, ops({look_samples = false}))
  -- the same text twice is not "more"; neither is the same number
  F.op('record', {30, true}, ops({look_sequence = 4, look_samples = 'same'})); F.op('record', {31, true}); F.op('record', {32, true}, ops({look_samples = 7}))
  F.op('record', {33, true}); F.op('record', {34, true}, ops({look_samples = 8})); F.op('record', {35, true}, ops({look_samples = 'same'}))
  F.op('record', {36, true}, ops({look_sequence = 5})); F.op('record', {37, true}); F.op('record', {38, true}, ops({look_sequence = false})); F.op('snapshot')
end)
F.run('capacity', function()
  for _, c in ipairs({15, 4097, 16.5, '16', TABLE, 0 / 0, math.huge, -16, true}) do F.op('new', {true, c}) end
  F.op('new', {nil}); F.op('snapshot'); F.op('new', {true, nil}); F.op('snapshot'); F.op('new', {true, false}); F.op('snapshot')
  F.op('new', {true, 4096}); F.op('record', {1, true}); F.op('snapshot'); F.op('new', {true, 16}); F.op('new', {true, 720}); F.op('snapshot')
end)

-- Seeded walks. `typed`: every field keeps the kind of value the mod gives it; otherwise anything goes.
local BOOL = {enabled = 1, ads = 1, foreign = 1, editor = 1, pinned = 1, pin_expected = 1, camera_retired = 1, view_expected = 1,
  view_visible = 1, recovering = 1, cleanup_pending = 1, safe = 1, aiming = 1, weapon_ready = 1, softened = 1, control_guard = 1}
local TEXT = {player = 1, weapon = 1, request = 1, phase = 1, reason = 1, policy = 1, event = 1, snapshot_reason = 1, entry_block = 1,
  action = 1, head_phase = 1}
local SMALL = {transition_epoch = {1, 2, 3}, transition_progress = {0, .5, 1, 1.5}, camera_epoch = {1, 2}, fov_epoch = {1, 2}, pose_epoch = {1, 2},
  look_sequence = {1, 2}, look_samples = {1, 2, 3, 4}, camera_kind = {4, 6}}
local NUMBERS = {0, 1, 2, 3, .5, .999, 1.5, -1, 1e300, -0.0, 0 / 0, math.huge, -math.huge, 123456789.25, 1 / 3}
local TEXTS = {'', 'a', 'b', 'idle', LONG, LONG:sub(1, 200), LONG:sub(1, 201), 'nul\0byte', 'p1', 'p2'}
local function any()
  local k = F.rnd(10)
  if k <= 2 then return nil elseif k <= 4 then return F.chance(.5) elseif k <= 7 then return F.pick(NUMBERS) elseif k <= 9 then return F.pick(TEXTS) end
  return TABLE
end
local function walk(seed, capacity, count, typed)
  F.seed(seed)
  F.op('new', {true, capacity})
  local frame = 0
  for _ = 1, count do
    local events = {}
    for _ = 1, F.pick({0, 1, 2, 3, 5, 8}) do
      local name, v = F.pick(NAMES)
      if not typed then v = any()
      elseif F.chance(.12) then v = nil
      elseif BOOL[name] then v = F.chance(.5)
      elseif TEXT[name] then v = F.pick({'p1', 'p2', 'idle', 'running', ''})
      elseif SMALL[name] then v = F.pick(SMALL[name])
      else v = F.pick({0, 1, .25, -3.5, 1 / 3, 1e-9}) end
      events[#events + 1] = f(name, v)
    end
    local k = F.rnd(40)
    if k <= 30 then
      frame = frame + F.pick({1, 1, 1, 1, 1, 2, 0, -1})
      local bad = not typed and F.chance(.1)
      F.op('record', {bad and F.pick({0, -1, 1.5, 'x', TABLE, 0 / 0, math.huge, nil, frame + 1000}) or frame,
        (typed or F.chance(.9)) and true or any()}, events)
    elseif k <= 36 then
      F.op('event', {F.chance(.9) and math.max(frame, 1) or F.pick({0, 1.5, 'x', nil, TABLE}),
        F.chance(.9) and F.pick({'policy_failure', 'swap', LONG, ''}) or any(), F.chance(.7) and true or any()}, events)
    elseif k == 39 and F.chance(.3) then F.op('clear', nil, events); frame = 0
    elseif F.chance(capacity and capacity <= 64 and 1 or .05) then F.op('snapshot', nil, events)
    else F.op('record', {frame, true}, events) end
  end
  F.op('snapshot')
end
F.run('typed16', function() walk(1, 16, 1500, true) end)
F.run('typed17', function() walk(2, 17, 1500, true) end)
F.run('typed720', function() walk(3, 720, 2500, true) end)
F.run('wild16', function() walk(4, 16, 1500, false) end)
F.run('wild33', function() walk(5, 33, 1500, false) end)
F.run('wild_default', function() walk(6, nil, 1200, false) end)
F.finish()

-- Replays a script against the retail controller.lua, unmodified, with a
-- scripted backend and clock. Usage: luajit run_controller.lua <retail file> <script file>
mod = {require = function() return {kind = type, number = tonumber} end} -- compat51: the controller uses kind only
local M = dofile(arg[1])
local script = dofile(arg[2])
local REASONS = script.reasons
local world, trace, snapshots = nil, {}, 0
local function reason(index) if index and index > 0 then return REASONS[index] end end
local function view(v, player_level)
  if not v or not v.ok then return nil, reason(v and v.err) end
  return {identity = player_level and ('p' .. v.player) or ('i' .. v.identity), player_identity = 'p' .. v.player,
          aiming = v.aiming, controller_active = v.active, camera_kind = v.kind, safe = v.safe,
          record = v.record > 0 and v.record or nil, reason = reason(v.reason),
          controller_bytes = string.char(0, 0, 0, 0, v.active and 1 or 0, v.guard)}
end
local backend = {}
function backend:snapshot()
  trace[#trace + 1] = 'snapshot'
  snapshots = snapshots + 1
  return view(snapshots > 1 and world.snap2 or world.snap, false)
end
function backend:player_snapshot() trace[#trace + 1] = 'player_snapshot'; return view(world.psnap, true) end
function backend:camera_only_weapon() trace[#trace + 1] = 'hammer'; return world.hammer end
function backend:prepare_pose()
  trace[#trace + 1] = 'prepare'
  if world.prepare == 0 then return true, {} end
  return false, reason(world.prepare)
end
function backend:enter_pose(_, _, remaining, blend)
  trace[#trace + 1] = blend and 'enter(blend)' or 'enter'
  assert(remaining == 0)
  if world.enter == 0 then return true, {request = world.lease_request > 0 and world.lease_request or nil,
                                         cleanup_only = world.lease_cleanup_only or nil} end
  return false, reason(world.enter)
end
function backend:pose_status(lease)
  trace[#trace + 1] = 'status'
  local s = world.status
  if not s.ok then
    if s.retire then lease.pose_owner_retired = true end
    return nil, reason(s.why)
  end
  local stats = {armed = s.armed, expired = s.expired, target_weight = s.tw, blend = s.blend, error = s.error,
                 weapon_updates = s.has_wu and s.wu or nil, zero_restores = s.has_zr and s.zr or nil}
  lease.pose_stats, lease.pose_error = stats, stats.error
  return stats
end
function backend:release(lease)
  trace[#trace + 1] = 'release'
  -- The backend records the helper's final statistics on the lease when it disarms it.
  local f = world.final
  if f.ok then lease.pose_stats = {weapon_updates = f.wu, target_weight = f.tw, blend = f.blend} end
  if world.release == 0 then return true, reason(world.release_note) end
  return false, reason(world.release)
end
function backend:set_pose_transition()
  trace[#trace + 1] = 'set_transition'
  if world.set_transition == 0 then return true end
  return false, reason(world.set_transition)
end
backend.presentation_transition = function()
  trace[#trace + 1] = 'packet'
  local p = world.packet
  if not p.ok then return nil end
  return {identity = 'i' .. p.identity, player_identity = 'p' .. p.player, epoch = p.epoch, progress = p.progress}
end
-- Placement: backend.pose_placement and backend.placement_weapon_key are fields; the two setters answer the script.
local AXES = {'x', 'y', 'z', 'yaw', 'pitch', 'roll'}
local function placement_table(spec)
  local t = {}
  for i, axis in ipairs(AXES) do
    local kind = spec.kind[i]
    if kind == 1 then t[axis] = spec.v[i] elseif kind == 2 then t[axis] = 'text' elseif kind == 3 then t[axis] = false end
  end
  return t
end
local function six(t)
  local parts = {}
  for i, axis in ipairs(AXES) do
    local value = t[axis]
    parts[i] = type(value) == 'number' and string.format('%.17g', value) or tostring(value)
  end
  return table.concat(parts, ',')
end
local c
local function set_answer(name, value)
  trace[#trace + 1] = name .. '(' .. six(value) .. ')'
  local answer = world.set
  if answer == 0 then return true end
  if answer == -1 then return false end
  if answer == -2 then error(reason(7), 0) end
  if answer == -3 then return nil end
  return false, reason(answer)
end
function backend:set_pose_placement(lease, value) assert(lease == c.lease); return set_answer('set_pose_placement', value) end
function backend:set_placement_defaults(value) return set_answer('set_placement_defaults', value) end
c = M.new(backend, nil, function() return world.now end)
for _, op in ipairs(script.ops) do
  world, trace, snapshots = op, {}, 0
  if op.follow then
    -- The game answers an entry with its native view (kind 6 on the lease's request) and the
    -- end of a lease with the normal one. The C test applies the same rule to its own lease.
    local kind, record = c.lease and 6 or 4, c.lease and (c.lease.request or 0) or 0
    for _, v in ipairs({op.snap, op.snap2}) do v.kind, v.record = kind, record end
    op.psnap.kind = kind
  end
  backend.pose_placement = op.bpose.ok and placement_table(op.bpose) or nil
  backend.placement_weapon_key = op.key > 0 and ('weapon' .. op.key) or nil
  local result, text = nil, ''
  if op.call == 'start' then result = c:start(0, 'pose_offset')
  elseif op.call == 'stop' then result = c:stop(reason(op.arg))
  elseif op.call == 'shutdown' then result = c:shutdown()
  elseif op.call == 'suspend' then result = c:suspend(reason(op.arg))
  elseif op.call == 'update' then result = c:update()
  elseif op.call == 'retry_cleanup' then result = c:retry_cleanup()
  elseif op.call == 'set_placement' then
    local value = 5
    if op.value.ok then value = placement_table(op.value) end
    result, text = c:set_placement(value)
    text = text or ''
  elseif op.call == 'get_placement' then
    text = six(c:get_placement())
    result = true
  elseif op.call == 'placement_target' then
    local key = c:placement_target()
    result, text = key ~= nil, key or ''
  end
  local r = c:result()
  print(table.concat({result == true and 1 or 0, c.phase, c:is_enabled() and 1 or 0, c.lease and 1 or 0,
    r.runs, r.enter_attempts, r.release_attempts, r.samples, r.observed_native_without_aim,
    r.reacquisitions or 0, r.aim_transitions or 0, table.concat(trace, ','), c:display_status(), c:status(), text}, '|'))
end

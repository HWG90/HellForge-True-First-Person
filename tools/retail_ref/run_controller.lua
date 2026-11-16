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
  local stats = {armed = s.armed, expired = s.expired, target_weight = s.tw, blend = s.blend, error = s.error}
  lease.pose_stats, lease.pose_error = stats, stats.error
  return stats
end
function backend:release()
  trace[#trace + 1] = 'release'
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
local c = M.new(backend, nil, function() return world.now end)
for _, op in ipairs(script.ops) do
  world, trace, snapshots = op, {}, 0
  if op.follow then
    -- The game answers an entry with its native view (kind 6 on the lease's request) and the
    -- end of a lease with the normal one. The C test applies the same rule to its own lease.
    local kind, record = c.lease and 6 or 4, c.lease and (c.lease.request or 0) or 0
    for _, v in ipairs({op.snap, op.snap2}) do v.kind, v.record = kind, record end
    op.psnap.kind = kind
  end
  local result
  if op.call == 'start' then result = c:start(0, 'pose_offset')
  elseif op.call == 'stop' then result = c:stop(reason(op.arg))
  elseif op.call == 'shutdown' then result = c:shutdown()
  elseif op.call == 'suspend' then result = c:suspend(reason(op.arg))
  elseif op.call == 'update' then result = c:update()
  elseif op.call == 'retry_cleanup' then result = c:retry_cleanup() end
  local r = c:result()
  print(table.concat({result == true and 1 or 0, c.phase, c:is_enabled() and 1 or 0, c.lease and 1 or 0,
    r.runs, r.enter_attempts, r.release_attempts, r.samples, r.observed_native_without_aim,
    r.reacquisitions or 0, r.aim_transitions or 0, table.concat(trace, ','), c:display_status()}, '|'))
end

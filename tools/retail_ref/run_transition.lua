-- Replays a script against the retail transition.lua, unmodified, and prints
-- one result line per operation. Usage: luajit run_transition.lua <retail file> <script file>
local M = dofile(arg[1])
local ops = dofile(arg[2])
local t = M.new()
local function packet(p, why)
  if not p then return 'N ' .. (why or '-') end
  return string.format('P %d %.17g %d %s %s %d %.17g', p.epoch, p.progress, p.ads and 1 or 0, p.reason,
    p.merged or '-', p.continuous and 1 or 0, p.started)
end
local function key(prefix, n) if n >= 0 then return prefix .. n end end
for _, op in ipairs(ops) do
  local kind = op[1]
  if kind == 'update' then
    local s
    if op.available then
      s = {player_identity = 'p' .. op.player, identity = 'i' .. op.identity, controller_active = op.active,
           seated = op.seated, shoulder_aim = op.shoulder, camera_kind = op.kind,
           action = key('a', op.action), zoom_identity = key('z', op.zoom)}
    end
    print(packet(t:update(s, op.now, op.enabled, op.blocked)))
  elseif kind == 'action' then
    print(packet(t:action(key('a', op.action), op.now)))
  elseif kind == 'resume' then
    print(packet(t:resume('i' .. op.identity, 'p' .. op.player, op.now)))
  elseif kind == 'result' then
    print(packet(t:result()))
  elseif kind == 'reset' then
    print(packet(t:reset()))
  elseif kind == 'native_ads' then
    local s = {controller_active = op.active, seated = op.seated,
               controller_bytes = string.char(op.b[1], op.b[2], op.b[3], op.b[4], op.b[5], op.b[6])}
    print(M.native_ads(s) and 'T' or 'F')
  end
end

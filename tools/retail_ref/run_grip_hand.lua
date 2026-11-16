-- Replays scripts against the retail grip_hand.lua, unmodified, under LuaJIT.
-- Usage: luajit run_grip_hand.lua <Goose Gadget Kit folder> <script file>
-- The script is one Lua table per line: {run=name, missing={...}} starts a run (a fresh world), every other
-- line is one operation. One output line per operation:
--   instance|call|snapshot|active|handles|maths failure|recorded engine calls|maths count,checksum|answer
-- For the two module functions the snapshot field carries the arguments instead: `solve` four points
-- (S,E,H,T as float bits), `weight` one number (double bits).
-- See arms_common.lua for the fake engine and what is recorded.
local here = arg[0]:match('^(.*)/[^/]*$') or '.'
local F = dofile(here .. '/arms_common.lua')
local kit = arg[1]
F.install_mod()
local G = F.retail(kit, 'src/first_person/grip_hand.lua')
local num, dhex = F.num, F.dhex

local W, instances
local function answer(g, ret)
  local r = g:result()
  return table.concat({num(ret), table.concat({num(r.frames), num(r.scans), num(r.weapons), num(r.pieces), num(r.bones), num(r.fingers),
    dhex(r.distance), dhex(r.off), num(r.errors), num(r.rig_screen_hidden), num(r.steady), num(r.handled), num(r.turning), num(r.sliding),
    num(r.swaps), num(r.recalled), num(r.partial), num(r.spun), num(r.gun), num(r.arm_root), num(r.gun_turned), dhex(r.right_distance)}, ','),
    num(g:is_clean()), tostring(r.reason), tostring(r.right)}, ';')
end
local function point(p) return F.v(F.f(p[1]), F.f(p[2]), F.f(p[3])) end
local function step(op)
  W.apply(op.edits)
  local index = op.inst or 1
  if op.call == 'engine' then -- from here on the engine lacks these functions
    W.remove(op.missing)
    print(table.concat({index, op.call, #op.missing > 0 and table.concat(op.missing, ' ') or '-', 'F', '~', '0', '', '0,00000000', '-'}, '|'))
    return
  end
  W.begin(op)
  if op.call == 'solve' then
    local S, E, H, T = point(op.S), point(op.E), point(op.H), point(op.T)
    local ok, elbow, reached = pcall(G.solve, W.sr.Vector3, S, E, H, T)
    local text = not ok and ('!' .. tostring(elbow)) or elbow == nil and '~' or (F.vhex(elbow) .. ',' .. F.vhex(reached))
    print(table.concat({index, op.call, table.concat({F.vhex(S), F.vhex(E), F.vhex(H), F.vhex(T)}, ','), 'F', '~',
      op.mfail and (op.mfail[1] .. ':' .. op.mfail[2]) or '0', W.finish(), text}, '|'))
    return
  elseif op.call == 'weight' then
    print(table.concat({index, op.call, dhex(op.distance), 'F', '~', '0', W.finish(), dhex(G.weight(op.distance))}, '|'))
    return
  end
  local s = F.snapshot(op.s)
  local handles = op.h and W.tables[op.h] or nil
  if op.h and not handles then error('script: no handle table ' .. tostring(op.h)) end
  local ret
  if op.call == 'new' then
    instances[index] = G.new(W.backend, W.sr, {clock = W.clock_fn})
  elseif op.call == 'update' then ret = instances[index]:update(s, op.active, handles)
  elseif op.call == 'stop' then ret = instances[index]:stop()
  elseif op.call == 'clean' then ret = instances[index]:is_clean()
  elseif op.call == 'result' then ret = instances[index]:result() ~= nil
  else error('unknown call ' .. tostring(op.call)) end
  print(table.concat({index, op.call, W.inputs(op, s, handles), W.finish(), answer(instances[index], ret)}, '|'))
end
for line in io.lines(arg[2]) do
  local row = assert(loadstring('return ' .. line))()
  if row.run then
    W = F.world(); instances = {}
    W.remove(row.missing)
  else step(row) end
end

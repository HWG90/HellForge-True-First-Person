-- Replays scripts against the retail arm_pieces.lua, unmodified, under LuaJIT.
-- Usage: luajit run_arm_pieces.lua <Goose Gadget Kit folder> <script file>
-- The script is one Lua table per line: {run=name, missing={...}} starts a run (a fresh world; the module
-- itself is loaded once, as in the kit, so its SIDES table is shared by every instance of every run), every
-- other line is one operation. One output line per operation:
--   instance|call|snapshot|active|handles|maths failure|recorded engine calls|maths count,checksum|answer
-- See arms_common.lua for the fake engine and what is recorded.
local here = arg[0]:match('^(.*)/[^/]*$') or '.'
local F = dofile(here .. '/arms_common.lua')
local kit = arg[1]
F.install_mod()
local A = F.retail(kit, 'src/first_person/arm_pieces.lua')
local num = F.num

local W, instances
local function answer(a, ret)
  local r = a:result()
  return table.concat({num(ret), table.concat({num(r.pieces), num(r.hung), num(r.scans), num(r.links), num(r.frames), num(r.errors)}, ','),
    num(a:is_clean()), tostring(r.reason)}, ';')
end
local function step(op)
  W.apply(op.edits)
  local index = op.inst or 1
  if op.call == 'engine' then -- from here on the engine lacks these functions
    W.remove(op.missing)
    print(table.concat({index, op.call, #op.missing > 0 and table.concat(op.missing, ' ') or '-', 'F', '~', '0', '', '0,00000000', '-'}, '|'))
    return
  end
  local s = F.snapshot(op.s)
  local handles = op.h and W.tables[op.h] or nil
  if op.h and not handles then error('script: no handle table ' .. tostring(op.h)) end
  W.begin(op)
  local ret
  if op.call == 'new' then
    instances[index] = A.new(W.backend, W.sr, {clock = W.clock_fn})
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

-- Shared by the run_<module>.lua runners of the effects modules (dof, vignette, aim_markers,
-- first_person_movement, camera_compat, native_ads_option, diagnostics). Each runner loads its retail
-- file's bytes unmodified under LuaJIT, drives it against scripted fakes and prints, per operation,
-- what the C test needs to replay it (tools/retail_ref/effects_gen.py turns that into a C header):
--   S <slot>                       a world slot name, in enum order
--   R <run name>                   a fresh module instance and an empty world
--   O <call> <arg1..arg4>          one operation
--   E <at> <slot> <key|-> <value>  a world change of that operation: applied before the operation
--                                  (at=0) or just before the fake answers host call number <at>
--   X <text>                       what retail returned, its state, and the ordered host-call trace
-- Values: n nil, f false, t true, d<16 hex digits of the double>, s<hex bytes>.
--
-- The world is a table of named slots (plus keyed slots: slot[key]); every fake answers from it and
-- calls F.tick(name, argument text) first, which counts the call, applies the changes due at that
-- count and appends `name(args)` to the trace. A slot named x_<host function> that holds a text
-- makes that host function raise the text, and x_any does so for whichever host function that has
-- an x_ slot is called next; x_ slots are cleared when the operation ends. The slot
-- `mem` is byte-addressed memory: a text value pokes its bytes at <key>, a number n unmaps n bytes.
-- tests/effects_fx.h is the same machinery in C.
local ffi = require('ffi')
local F = {world = {}, keyed = {}, calls = 0, last_calls = 0, trace = {}}
local lines, slots, slot_known = {}, {}, {}
F.kit = arg[1]

local cell = ffi.new('double[1]')
local function bits(x)
  cell[0] = x
  local p = ffi.cast('uint32_t *', cell)
  return string.format('%08x%08x', p[1], p[0])
end
local function hex(s) return (s:gsub('.', function(c) return string.format('%02x', c:byte()) end)) end
local function enc(v)
  if v == nil then return 'n' elseif v == false then return 'f' elseif v == true then return 't' end
  if type(v) == 'number' then return 'd' .. bits(v) end
  assert(type(v) == 'string', 'value cannot be scripted')
  return 's' .. hex(v)
end

-- Texts both sides print. A number: plain when it is an integer below 2^53 (and not -0), else the
-- bits of the double, so two floating-point results are equal only when bit-identical.
function F.num(x)
  if x ~= x then return 'nan' end
  if x == math.floor(x) and math.abs(x) < 9007199254740992 and not (x == 0 and 1 / x < 0) then return string.format('%.0f', x) end
  return 'x' .. bits(x)
end
function F.str(s)
  return '"' .. (s:gsub('.', function(c)
    local b = c:byte()
    if b < 32 or b > 126 or c == '"' or c == '\\' then return string.format('\\x%02x', b) end
    return c
  end)) .. '"'
end
function F.show(v)
  if v == nil then return '~' elseif v == true then return 'T' elseif v == false then return 'F' end
  if type(v) == 'number' then return F.num(v) end
  if type(v) == 'string' then return F.str(v) end
  return '<' .. type(v) .. '>'
end

function F.slots(names)
  for _, name in ipairs(names) do
    assert(not slot_known[name], name)
    slot_known[name] = true; slots[#slots + 1] = name; lines[#lines + 1] = 'S\t' .. name
  end
end
local function apply(e)
  assert(slot_known[e.slot], 'undeclared slot ' .. tostring(e.slot))
  if e.key == nil then F.world[e.slot] = e.v; return end
  local map = F.keyed[e.slot]
  if not map then map = {}; F.keyed[e.slot] = map end
  if e.slot == 'mem' and type(e.v) == 'string' then
    for i = 1, #e.v do map[e.key + i - 1] = e.v:byte(i) end
  elseif e.slot == 'mem' and type(e.v) == 'number' then
    for i = 0, e.v - 1 do map[e.key + i] = nil end
  else map[e.key] = e.v end
end
function F.get(slot) assert(slot_known[slot], slot); return F.world[slot] end
function F.getk(slot, key) assert(slot_known[slot], slot); local map = F.keyed[slot]; return map and map[key] end
-- What a fake changes itself (a write the module asked for); the C fake does the same.
function F.poke(slot, key, v) apply({slot = slot, key = key, v = v}) end
-- Memory: nil when any byte is unmapped.
function F.read(address, size)
  local map, out = F.keyed.mem or {}, {}
  for i = 0, size - 1 do
    local b = map[address + i]
    if not b then return nil end
    out[#out + 1] = string.char(b)
  end
  return table.concat(out)
end

local pending
function F.tick(name, args)
  F.calls = F.calls + 1
  for _, e in ipairs(pending) do if e.at == F.calls then apply(e) end end
  F.trace[#F.trace + 1] = name .. '(' .. (args or '') .. ')'
  local raise = F.world['x_' .. name]
  if raise == nil and slot_known['x_' .. name] then raise = F.world.x_any end
  if raise ~= nil then error(raise, 0) end
end
function F.log(text) F.trace[#F.trace + 1] = 'log(' .. F.show(text) .. ')' end

-- One operation. `dispatch(call, args)` (set by the runner) makes the call and returns the text
-- of its results and of the module's state.
function F.op(call, args, events)
  args, events = args or {}, events or {}
  lines[#lines + 1] = table.concat({'O', call, enc(args[1]), enc(args[2]), enc(args[3]), enc(args[4])}, '\t')
  for _, e in ipairs(events) do
    e.at = e.at or 0
    lines[#lines + 1] = table.concat({'E', e.at, e.slot, e.key and bits(e.key) or '-', enc(e.v)}, '\t')
  end
  F.calls, F.trace, pending = 0, {}, events
  for _, e in ipairs(events) do if e.at == 0 then apply(e) end end
  local text = F.dispatch(call, args)
  for _, name in ipairs(slots) do if name:sub(1, 2) == 'x_' then F.world[name] = nil end end
  F.last_calls = F.calls
  lines[#lines + 1] = 'X\t' .. text .. ' | ' .. table.concat(F.trace, ' ')
  return text
end
function F.run(name, body)
  lines[#lines + 1] = 'R\t' .. name
  F.world, F.keyed, F.calls, F.last_calls = {}, {}, 0, 0
  body()
end
function F.finish() io.write(table.concat(lines, '\n'), '\n') end

-- A deterministic generator (Park-Miller), independent of the LuaJIT build.
local seed = 1
function F.seed(n) seed = n % 2147483646 + 1 end
function F.rnd(n) seed = seed * 16807 % 2147483647; return seed % n + 1 end
function F.chance(p) return F.rnd(1000000) <= p * 1000000 end
function F.pick(list) return list[F.rnd(#list)] end

-- The retail file, unmodified, under the chunk name the mod requires it by. `requires` answers
-- mod.require; compat51.lua is answered with what it is under LuaJIT (kind = type, number = tonumber).
F.PORT51 = {kind = type, number = tonumber}
function F.load(file, requires)
  local handle = assert(io.open(F.kit .. '/src/first_person/' .. file, 'rb'))
  local source = handle:read('*a'); handle:close()
  requires = requires or {}
  requires['src/first_person/compat51.lua'] = requires['src/first_person/compat51.lua'] or F.PORT51
  mod = {require = function(path)
    local value = requires[path]
    if type(value) == 'function' then return value() end
    return assert(value, 'unexpected mod.require ' .. tostring(path))
  end}
  return assert(loadstring(source, '@src/first_person/' .. file))()
end
-- Little-endian bytes of an unsigned integer.
function F.le(value, size)
  local out = {}
  for i = 1, size do out[i] = string.char(value % 256); value = math.floor(value / 256) end
  return table.concat(out)
end
function F.unhex(s) return (s:gsub('%x%x', function(p) return string.char(tonumber(p, 16)) end)) end
return F

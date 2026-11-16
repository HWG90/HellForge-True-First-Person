-- Replays a script against the retail motion.lua, unmodified, and prints one
-- result line per operation. Numbers are printed with 17 significant digits,
-- which names every double exactly.
-- Usage: luajit run_motion.lua <kit folder> <script file>
-- (Plain Lua 5.1 runs it too: the module needs only compat51's `kind`.)
local kit = arg[1]
mod = {require = function(path)
  if path:match('compat51') then
    local ok, bit = pcall(require, 'bit')
    return {kind = type, number = tonumber, bit = ok and bit or nil}
  end
  return dofile(kit .. '/' .. path)
end}
local Motion = dofile(kit .. '/src/first_person/motion.lua')
local ops = dofile(arg[2])
local settings = {'bob_strength', 'bob_speed', 'sway_strength', 'sway_speed', 'sprint_strength', 'camera_bob_strength',
  'camera_bob_speed', 'camera_sway_strength', 'camera_sway_speed'}
local dynamic = {'phase', 'camera_phase', 'camera_look_x', 'camera_look_z', 'amplitude', 'sprint', 'sprint_hold', 'shoulder',
  'aim', 'look_x', 'look_z', 'right_speed', 'forward_speed'}
local outputs = {'dx', 'dy', 'dz', 'pitch', 'yaw', 'roll', 'camera_dx', 'camera_dy', 'camera_dz', 'camera_pitch', 'camera_yaw',
  'camera_roll', 'weight', 'speed', 'phase', 'sprint_weight', 'shoulder_weight', 'bob_weight'}
local function num(n) return string.format('%.17g', n) end
local function flag(v)
  assert(v == true or v == false, 'not a boolean')
  return v and '1' or '0'
end
local m = Motion.new()
for _, op in ipairs(ops) do
  local kind = op[1]
  if kind == 'new' then
    local ok, made = pcall(Motion.new, op.options)
    -- assert adds the file position to the text under LuaJIT; the text itself is what is compared.
    if ok then m = made; print('N T') else m = Motion.new(); print('N F ' .. (tostring(made):gsub('^.*motion%.lua:%d+: ', ''))) end
  elseif kind == 'configure' then
    local ok, why = m:configure(op.value)
    assert(ok == true and why == nil or ok == false and type(why) == 'string')
    print(ok and 'C T' or 'C F ' .. why)
  elseif kind == 'update' then
    local o = m:update(op.sample, op.dt)
    local row = {'U', o.reset or '-', o.rebased or '-', o.camera_dx ~= nil and '1' or '0'}
    assert(o.reset == false or type(o.reset) == 'string')
    assert(o.rebased == false or type(o.rebased) == 'string')
    for _, key in ipairs(outputs) do
      local v = o[key]
      if v == nil then assert(key:match('^camera_') and o.camera_dx == nil); v = 0 end
      row[#row + 1] = num(v)
    end
    row[#row + 1] = flag(o.sprint_target); row[#row + 1] = flag(o.sprint_held)
    print(table.concat(row, ' '))
  elseif kind == 'reset' then
    m:reset(); print('R')
  elseif kind == 'clear_ads' then
    m:clear_ads(); print('R')
  elseif kind == 'state' then
    local row = {'S', flag(m.enabled), flag(m.editor_frozen), flag(m.shoulder_aim_enabled), m.previous and '1' or '0', num(m.scale)}
    for _, key in ipairs(settings) do row[#row + 1] = num(m[key]) end
    local p = m.previous
    row[#row + 1] = p and (type(p.owner) .. ':' .. tostring(p.owner)) or '-'
    for _, key in ipairs({'x', 'y', 'z', 'yaw', 'pitch'}) do row[#row + 1] = num(p and p[key] or 0) end
    for _, key in ipairs(dynamic) do row[#row + 1] = num(m[key]) end
    print(table.concat(row, ' '))
  else
    error('unknown operation ' .. tostring(kind))
  end
end

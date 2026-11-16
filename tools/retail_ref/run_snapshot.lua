-- Runs the retail backend.lua, unmodified, on fake game memory through its own
-- `io` seam and prints what snapshot() and player_snapshot() answer per scene.
-- Usage: luajit run_snapshot.lua <kit folder> <script file>
local kit = arg[1]
local bit = require('bit')
mod = {require = function(path)
  if path:match('compat51') then return {kind = type, number = tonumber, bit = bit} end
  return dofile(kit .. '/' .. path)
end}
local Backend = dofile(kit .. '/src/first_person/backend.lua')
local scenes = dofile(arg[2])
local function flag(v) return v and 1 or 0 end
local function show(s, why, context)
  if not s then
    why = tostring(why):gsub('^.*backend%.lua:%d+: ', '')
    return 'E ' .. why
  end
  return table.concat({'S', s.camera_kind, s.head, s.tail, string.format('%.0f', s.record - context), s.request_avatar,
    s.request_weapon, s.avatar, s.avatar_index, s.body, s.viewmodel, flag(s.aiming), flag(s.shoulder_aim),
    flag(s.seated), flag(s.reloading), flag(s.controller_active), flag(s.blocked), flag(s.safe),
    flag(s.weapon_transition), flag(s.head_camera_transition), s.weapon or 0, s.weapon_unit or 0}, ' ')
end
for _, scene in ipairs(scenes) do
  local mem = {}
  for _, record in ipairs(scene.memory) do
    local at, hex = record[1], record[2]
    for i = 1, #hex, 2 do mem[at + (i - 1) / 2] = tonumber(hex:sub(i, i + 1), 16) end
  end
  local io = {module = function() return scene.base end, read = function(at, size)
    local out = {}
    for i = 0, size - 1 do
      local b = mem[at + i]
      if not b then error('Memory unavailable', 0) end
      out[#out + 1] = string.char(b)
    end
    return table.concat(out)
  end}
  local b = Backend.new(nil, io)
  local full, why = b:snapshot()
  local player, problem = b:player_snapshot()
  print(show(full, why, scene.context) .. '|' .. show(player, problem, scene.context))
end

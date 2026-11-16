-- Replays a script against the retail action_placement.lua, unmodified, and
-- prints one tab-separated result line per operation.
-- Usage: luajit run_action_placement.lua <kit folder> <script file>
local kit = arg[1]
mod = {require = function(path)
  if path:match('compat51') then return {kind = type, number = tonumber, bit = require('bit')} end
  return dofile(kit .. '/' .. path)
end}
local A = dofile(kit .. '/src/first_person/action_placement.lua')
local script = dofile(arg[2])
local configs, mappings, ops = script.configs, script.mappings, script.ops
local function num(n) return string.format('%.17g', n) end
local function bare(text) return (tostring(text):gsub('^.*action_placement%.lua:%d+: ', '')) end
local fields = {'x', 'y', 'z', 'yaw', 'pitch', 'roll'}
local function placement(p, row)
  assert(type(p) == 'table')
  local mask = 0
  for i, key in ipairs(fields) do
    if p[key] ~= nil then assert(type(p[key]) == 'number'); mask = mask + 2 ^ (i - 1) end
  end
  for key in pairs(p) do assert(key == 'x' or key == 'y' or key == 'z' or key == 'yaw' or key == 'pitch' or key == 'roll') end
  row[#row + 1] = string.format('%d', mask)
  for _, key in ipairs(fields) do row[#row + 1] = num(p[key] or 0) end
end
-- The module keeps its catalogue in the local `weapons`, an upvalue of weapon_info.
local function weapons()
  for i = 1, 10 do
    local name, value = debug.getupvalue(A.weapon_info, i)
    if name == 'weapons' then return value end
  end
  error('weapons upvalue not found')
end
local function weapon(row, key, v)
  row[#row + 1] = key
  if v == false then row[#row + 1] = '!'; row[#row + 1] = '-'; row[#row + 1] = '0'; row[#row + 1] = '-'
  else
    assert(v.key == key and type(v.name) == 'string' and type(v.kind) == 'number')
    row[#row + 1] = v.name; row[#row + 1] = v.family; row[#row + 1] = string.format('%d', v.kind)
    row[#row + 1] = v.item == nil and '-' or string.format('%.0f', v.item)
  end
end
-- The list of actions and labels, the observed set: constants of the module.
do
  local row = {'A'}
  for _, a in ipairs(A.actions) do row[#row + 1] = a[1]; row[#row + 1] = a[2]; row[#row + 1] = A.observed_actions[a[1]] and '1' or '0' end
  local n = 0
  for _ in pairs(A.observed_actions) do n = n + 1 end
  row[#row + 1] = string.format('%d', n)
  print(table.concat(row, '\t'))
end
for _, op in ipairs(ops) do
  local kind = op[1]
  local config = op.config and configs[op.config]
  local row
  if kind == 'gear' then -- the retail catalogue rows as the module sees them
    local gear = dofile(kit .. '/src/loadout_catalogue.lua').gear
    row = {'R', string.format('%d', #gear)}
    for _, g in ipairs(gear) do
      assert(type(g.kind) == 'number' and g.kind % 1 == 0 and type(g.name) == 'string', 'catalogue row shape changed')
      assert(g.id == nil or type(g.id) == 'number' and g.id % 1 == 0 and g.id >= 0 and g.id < 4294967296, 'catalogue id shape changed')
      assert(g.type == nil or type(g.type) == 'string', 'catalogue type shape changed')
      row[#row + 1] = string.format('%d', g.kind); row[#row + 1] = g.name
      row[#row + 1] = g.id == nil and '-' or string.format('%.0f', g.id); row[#row + 1] = g.type or '-'
    end
  elseif kind == 'catalogue' then
    local rows = op.rows
    if op.real then rows = dofile(kit .. '/src/loadout_catalogue.lua').gear end
    A.configure_catalogue(rows)
    local w, keys = weapons(), {}
    for key in pairs(w) do keys[#keys + 1] = key end
    table.sort(keys)
    row = {'G', string.format('%d', #keys)}
    for _, key in ipairs(keys) do
      weapon(row, key, w[key])
      local info = A.weapon_info(key)
      assert((info == nil) == (w[key] == false))
      if info then for k, v in pairs(info) do assert(w[key][k] == v) end end
    end
  elseif kind == 'info' then
    local v = A.weapon_info(op.key)
    row = {'I'}
    if v then weapon(row, v.key, v) else row[2] = '-' end
  elseif kind == 'baseline' then
    local v = A.family_baseline(op.key)
    row = {'B'}
    if v then placement(v, row) else row[2] = '-' end
  elseif kind == 'valid' then
    local v = A.valid(op.p)
    assert(v == true or v == false)
    row = {'V', v and '1' or '0'}
  elseif kind == 'resolve' then
    local ok, p, explicit = pcall(A.resolve, config, op.key, op.action)
    if ok then
      assert(explicit == true or explicit == false)
      row = {'P'}; placement(p, row); row[#row + 1] = explicit and '1' or '0'
    else row = {'E', bare(p)} end
  elseif kind == 'default' then
    row = {'P'}; placement(A.default_placement(config, op.key, op.action), row); row[#row + 1] = '0'
  elseif kind == 'saved' then
    local ok, out = pcall(A.saved, config, op.key, op.action, op.value, op.all)
    if ok then
      local ids = {}
      for id in pairs(out) do assert(type(id) == 'string'); ids[#ids + 1] = id end
      table.sort(ids)
      row = {'T', string.format('%d', #ids)}
      for _, id in ipairs(ids) do
        local n = 0
        for a in pairs(out[id]) do assert(type(a) == 'string'); n = n + 1 end
        row[#row + 1] = id; row[#row + 1] = string.format('%d', n)
        local listed = 0
        for _, a in ipairs(A.actions) do
          local v = out[id][a[1]]
          if v ~= nil then listed = listed + 1; row[#row + 1] = a[1]; placement(v, row) end
        end
        assert(listed == n, 'saved row holds an action outside the list')
      end
    else row = {'E', bare(out)} end
  elseif kind == 'select' then
    local ok, name = pcall(A.select, op.sample, op.mapping and mappings[op.mapping])
    row = ok and {'S', name} or {'E', bare(name)}
  else
    error('unknown operation ' .. tostring(kind))
  end
  print(table.concat(row, '\t'))
end

-- Replays a script against the retail settings chain, unmodified: src/first_person.lua (the facade
-- whose host answers mod.config / mod.set), src/first_person/action_placement.lua and
-- src/first_person/menu.lua, and prints one tab-separated result line per operation.
-- Usage: luajit run_settings.lua <kit folder> <script file>
local kit = arg[1]
local loaded = {}
mod = {log = function() end}
mod.require = function(path)
  if path:match('compat51') then return {kind = type, number = tonumber, bit = require('bit')} end
  if loaded[path] == nil then loaded[path] = dofile(kit .. '/' .. path) end
  return loaded[path]
end
local Facade = mod.require('src/first_person.lua')
local Actions = mod.require('src/first_person/action_placement.lua')
local Menu = mod.require('src/first_person/menu.lua')
local script = dofile(arg[2])

local function num(n) return string.format('%.17g', n) end
local function bare(text) return (tostring(text):gsub('^.*%.lua:%d+: ', '')) end
local fields = {'x', 'y', 'z', 'yaw', 'pitch', 'roll'}
local function placement(p, row)
  local mask = 0
  for i, key in ipairs(fields) do
    if p[key] ~= nil then assert(type(p[key]) == 'number'); mask = mask + 2 ^ (i - 1) end
  end
  row[#row + 1] = string.format('%d', mask)
  for _, key in ipairs(fields) do row[#row + 1] = num(p[key] or 0) end
end
local function emit(row) print(table.concat(row, '\t')) end
local function flat(value, path, out)
  if type(value) == 'table' then
    local keys = {}
    for k in pairs(value) do keys[#keys + 1] = tostring(k) end
    table.sort(keys)
    if #keys == 0 then out[#out + 1] = {path, '{}'} end
    for _, k in ipairs(keys) do flat(value[k], path .. '.' .. k, out) end
  elseif type(value) == 'number' then out[#out + 1] = {path, num(value)}
  else out[#out + 1] = {path, tostring(value)} end
end

-- The kit as the facade's parent: its config, and a set that stores (the loader's mod.set).
local parent = {config = {first_person = script.config}}
parent.set = function(key, value) parent.config[key] = value; return true end
local host = Facade.host(parent)
Actions.configure_catalogue(dofile(kit .. '/src/loadout_catalogue.lua').gear)

-- D: the facade's own defaults table (what mod.config answers when nothing was saved).
do
  local out = {}
  flat(Facade.host({config = {}}).config, 'first_person', out)
  for _, row in ipairs(out) do emit({'D', row[1], row[2]}) end
end

-- The menu, with a handle that answers the stored control values.
local stored = script.menu
local current
local function new_menu()
  local menu = Menu.new({}, function() end, nil, {
    presentation_settings = function() return host.config end,
    current_weapon = function() return current end,
  })
  menu.handle = {get = function(id) return stored[id] end}
  return menu
end
-- C: every control the menu registers with a default (id, type, default), read off the registration itself.
do
  rawset(_G, 'DBFMCM', {register = function() end, mods = {}, camera_input = {}})
  local menu = Menu.new({display_status = function() return 'off' end, is_enabled = function() return false end}, function() end, nil, {
    presentation_settings = function() return host.config end,
    aim_markers_setting = function() return host.config.show_aim_markers end,
  })
  pcall(menu.update, menu)
  local pages = Facade.pages()
  assert(#pages == 2, 'the menu did not register its two pages')
  for _, page in ipairs(pages) do
    for _, control in ipairs(page.controls) do
      if control.default ~= nil then
        emit({'C', (control.id:gsub('^first_person_', '')), control.type, type(control.default) == 'number' and num(control.default) or tostring(control.default)})
      end
    end
  end
  rawset(_G, 'DBFMCM', nil)
end

for _, op in ipairs(script.ops) do
  local kind = op[1]
  local row
  if kind == 'resolve' then
    local ok, p, explicit = pcall(Actions.resolve, host.config, op.key, op.action)
    if ok then row = {'P'}; placement(p, row); row[#row + 1] = explicit and '1' or '0'
    else row = {'E', bare(p)} end
  elseif kind == 'default' then
    row = {'P'}; placement(Actions.default_placement(host.config, op.key, op.action), row); row[#row + 1] = '0'
  elseif kind == 'info' then
    local v = Actions.weapon_info(op.key)
    row = v and {'I', v.name, v.family} or {'I', '-', '-'}
  elseif kind == 'near' then
    current = op.key and {key = op.key, identity = 'W'} or nil
    local v = new_menu():near_options()
    row = {'N', num(v.hip), num(v.aim), v.profile and '1' or '0'}
  elseif kind == 'motion' then -- runtime.lua:816-821
    local config = host.config or {}
    local defaults = {enabled = config.motion_enabled ~= false, scale = config.motion_scale or 1, camera_bob_strength = 0, camera_sway_strength = 0}
    local v = new_menu():motion_options(defaults)
    row = {'O', v.enabled and '1' or '0', num(v.scale)}
    for _, key in ipairs({'bob_strength', 'bob_speed', 'sway_strength', 'sway_speed', 'sprint_strength', 'camera_bob_strength', 'camera_sway_strength'}) do
      row[#row + 1] = num(v[key])
    end
    for _, key in ipairs({'camera_bob_speed', 'camera_sway_speed', 'editor_frozen', 'shoulder_aim_enabled'}) do assert(v[key] == nil) end
  elseif kind == 'flags' then
    local menu = new_menu()
    row = {'F', menu:fird_person_arms() and '1' or '0', menu:show_torso() and '1' or '0', menu:native_ads() and '1' or '0', menu:ads_mode()}
  elseif kind == 'field' then
    local v = (host.config or {})[op.name]
    if type(v) == 'table' and (op.name == 'placement' or op.name == 'fird_head_camera' or op.name == 'grab_offset') then
      row = {'V', 'placement'}; placement(v, row)
    elseif type(v) == 'table' then
      local n = 0
      for _, inner in pairs(v) do
        if op.name == 'action_state_mapping' then for _ in pairs(inner) do n = n + 1 end else n = n + 1 end
      end
      row = {'V', 'table', string.format('%d', n)}
    elseif type(v) == 'number' then row = {'V', 'number', num(v)}
    else row = {'V', tostring(v)} end
  elseif kind == 'mapped' then -- runtime.lua:772-774
    local found = false
    for _, mapping in pairs((host.config or {}).action_state_mapping or {}) do
      if type(mapping) == 'table' then for _, name in pairs(mapping) do if name == op.action then found = true end end end
    end
    row = {'M', found and '1' or '0'}
  elseif kind == 'save' then -- runtime.lua:782-783
    local ok, profiles = pcall(Actions.saved, host.config, op.key, op.action, op.value, op.all)
    if ok then row = {'T', tostring(host.set('action_placements', profiles))}
    else row = {'E', bare(profiles)} end
  elseif kind == 'reset_all' then -- runtime.lua:806-811
    local values = {weapon_placements = {}, action_placements = {}, placement = {x = .15, y = 0, z = -.12}}
    local done = true
    for _, key in ipairs({'weapon_placements', 'action_placements', 'placement'}) do done = done and host.set(key, values[key]) == true end
    row = {'T', tostring(done)}
  elseif kind == 'set' then
    row = {'T', tostring(host.set(op.name, op.value))}
  elseif kind == 'menu_set' then
    stored[op.name] = op.value
    row = {'T', 'true'}
  else
    error('unknown operation ' .. tostring(kind))
  end
  emit(row)
end

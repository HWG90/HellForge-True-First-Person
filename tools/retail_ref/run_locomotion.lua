-- Replays a script against the retail locomotion.lua, unmodified.
--  * pure operations (rotation_to_parent, to_parent, sprinting): one line each;
--  * the sampler, M.new(backend):sample(lease), over the retail backend.lua and action_state.lua (also unmodified,
--    on fake game memory through the backend's own `io` seam): one line per step, with the ordered list of memory
--    reads made outside the player snapshot.
-- Numbers are printed with 17 significant digits, which names every double exactly.
-- Usage: luajit run_locomotion.lua <kit folder> <script file>
local kit = arg[1]
local bit = require('bit')
local ffi = require('ffi')
mod = {require = function(path)
  if path:match('compat51') then return {kind = type, number = tonumber, bit = bit, ffi = ffi} end
  return dofile(kit .. '/' .. path)
end}
local Backend = dofile(kit .. '/src/first_person/backend.lua')
local Locomotion = dofile(kit .. '/src/first_person/locomotion.lua')
local script = dofile(arg[2])
local function num(n) return string.format('%.17g', n) end
local function strip(text) return (tostring(text):gsub('^.*%.lua:%d+: ', '')) end

-- ---------------------------------------------------------------- pure
for _, op in ipairs(script.pure) do
  local kind = op[1]
  if kind == 'rotation' then
    local ok, q = pcall(Locomotion.rotation_to_parent, op.value, op.camera, op.parent)
    if ok then
      local n = 0
      for _ in pairs(q) do n = n + 1 end
      assert(n == 4)
      print(table.concat({'Q', num(q.qx), num(q.qy), num(q.qz), num(q.qw)}, '\t'))
    else print('E\t' .. strip(q)) end
  elseif kind == 'to_parent' then
    local ok, v = pcall(Locomotion.to_parent, op.value, op.camera, op.parent)
    if ok then
      local n = 0
      for _ in pairs(v) do n = n + 1 end
      assert(n == 7)
      print(table.concat({'V', num(v.dx), num(v.dy), num(v.dz), num(v.qx), num(v.qy), num(v.qz), num(v.qw)}, '\t'))
    else print('E\t' .. strip(v)) end
  elseif kind == 'sprinting' then
    local v = Locomotion.sprinting(op.action, op.verified, op.flags)
    assert(v == true or v == false)
    print('B\t' .. (v and 1 or 0))
  else
    error('unknown operation ' .. tostring(kind))
  end
end

-- ------------------------------------------------------------- sampler
local images = script.images
local function decode(hex) return (hex:gsub('%x%x', function(pair) return string.char(tonumber(pair, 16)) end)) end
for _, image in ipairs(images) do
  image.own = {}
  for _, record in ipairs(image.records) do
    record.bytes = record[2] and decode(record[2])
    record.at = record[1]
    image.own[record.at] = true
  end
end
local function covering(records, at)
  for _, record in ipairs(records) do
    local size = record.bytes and #record.bytes or 1
    if at >= record.at and at < record.at + size then return record end
  end
end
local function fetch(image, at, size)
  local out = {}
  while size > 0 do
    local record = covering(image.records, at)
    if not record and image.base then
      record = covering(images[image.base].records, at)
      if record and image.own[record.at] then record = nil end
    end
    if not record or not record.bytes then error('Memory unavailable', 0) end
    local from = at - record.at
    local n = math.min(size, #record.bytes - from)
    out[#out + 1] = record.bytes:sub(from + 1, from + n)
    at, size = at + n, size - n
  end
  return table.concat(out)
end
local function hex(text) return (text:gsub('.', function(c) return string.format('%02x', c:byte()) end)) end

for _, sequence in ipairs(script.sequences) do
  local current, in_snapshot, reads, trace, triggers
  local io = {module = function() return script.game end, executable = function() return sequence.exe or script.exe end,
    read = function(at, size)
      if not in_snapshot then
        reads = reads + 1
        for _, trigger in ipairs(triggers) do
          if trigger[1] == reads then current = images[trigger[2]] end
        end
        trace[#trace + 1] = string.format('%.0f:%d', at, size)
      end
      return fetch(current, at, size)
    end}
  local backend = Backend.new(nil, io)
  backend.player_snapshot = function(self) -- the already-proven part: its reads are not listed or counted
    in_snapshot = true
    local s, why = Backend.player_snapshot(self)
    in_snapshot = false
    return s, why
  end
  local sampler = Locomotion.new(backend)
  for _, step in ipairs(sequence.steps) do
    reads, trace, triggers = 0, {}, {}
    -- The lease the sampler is handed: made for the player of image `origin`, as the controller would have.
    current = images[step.origin or step.image]
    local origin = backend:player_snapshot()
    local lease = {origin = {player_identity = origin and origin.player_identity or 'nobody'},
      request = (origin and origin.record or 0) + (step.request_delta or 0), alignment = {context = {object = step.rig_object}},
      done = step.done, cleanup_only = step.cleanup_only}
    backend.alignment_lease = step.other_lease and {} or lease
    if step.no_lease then lease = nil end
    current = images[step.image]
    triggers = step.triggers or {}
    local r, why, line
    if step.on_foot then
      -- backend:on_foot(s, action) by itself, on the player snapshot of the image and a given action answer.
      local s = backend:player_snapshot()
      if s and step.edit == 'index' then s.avatar_index = 8 end
      if s and step.edit == 'address' then s.flags_address = s.flags_address + 8 end
      local action = step.on_foot ~= 'nil' and step.on_foot or nil
      local v = backend:on_foot(s, action)
      assert(v == true or v == false)
      line = 'F\t' .. (v and 1 or 0) .. '\t' .. (s and 1 or 0)
    else
      r, why = sampler:sample(lease)
    end
    if line then
    elseif r then
      assert(why == nil)
      local fresh = backend:player_snapshot()
      assert(type(r.ads) == 'boolean' and type(r.shoulder_aim) == 'boolean' and type(r.sprint) == 'boolean' and type(r.reload) == 'boolean')
      assert(#r.stance_flags == 24 and #r.camera_basis == 16 and #r.parent_basis == 16)
      local row = {'R', num(r.x), num(r.y), num(r.z), num(r.yaw), num(r.pitch), r.ads and 1 or 0, r.shoulder_aim and 1 or 0,
        r.sprint and 1 or 0, r.reload and 1 or 0, hex(r.stance_flags), r.action_resource or '-',
        r.action_verified == nil and '-' or (r.action_verified and 1 or 0), r.l1 or '-', r.l3 or '-', r.l14 or '-',
        string.format('%.0f', r.body_object), (fresh and r.owner == fresh.player_identity) and 1 or 0, r.sprint_evidence}
      for i = 1, 16 do row[#row + 1] = num(r.camera_basis[i]) end
      for i = 1, 16 do row[#row + 1] = num(r.parent_basis[i]) end
      line = table.concat(row, '\t')
    else
      line = 'E\t' .. strip(why)
    end
    print(line .. '\t|\t' .. table.concat(trace, ','))
  end
end

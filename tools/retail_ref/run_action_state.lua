-- Runs the retail action_state.lua, unmodified, over the retail backend.lua
-- (also unmodified, on fake game memory through its own `io` seam) and prints
-- what sample() answers per step, with the ordered list of memory reads the
-- two files made outside the player snapshot.
-- Usage: luajit run_action_state.lua <kit folder> <script file>
local kit = arg[1]
local bit = require('bit')
mod = {require = function(path)
  if path:match('compat51') then return {kind = type, number = tonumber, bit = bit, ffi = require('ffi')} end
  return dofile(kit .. '/' .. path)
end}
local Backend = dofile(kit .. '/src/first_person/backend.lua')
local Action = dofile(kit .. '/src/first_person/action_state.lua')
local script = dofile(arg[2])
local images = script.images

-- Fake memory: an image is a list of records {at, bytes}; an image with a base shows the base's records except
-- those it replaces (same address) or withdraws (bytes false).
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

local function strip(text) return (tostring(text):gsub('^.*%.lua:%d+: ', '')) end
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
  -- The player snapshot is the already-proven part (run_snapshot.lua): its reads are not listed or counted.
  backend.player_snapshot = function(self)
    in_snapshot = true
    local s, why = Backend.player_snapshot(self)
    in_snapshot = false
    return s, why
  end
  local sampler = Action.new(backend)
  for _, step in ipairs(sequence.steps) do
    reads, trace, triggers = 0, {}, {}
    local expected, reference
    if step.expected then
      current = images[step.expected]
      expected = backend:player_snapshot() -- a scene without a player hands in nothing
      if expected then
        reference = expected.player_identity
        if step.edit == 'body' then expected.body = step.body
        elseif step.edit == 'identity' then expected.player_identity = expected.player_identity .. '/edited' end
      end
    end
    current = images[step.image]
    triggers = step.triggers or {}
    local r, why, line
    if step.context then
      -- The two backend methods themselves: _alignment_context on a Unit reference, then _alignment_executable.
      local ok, c = pcall(backend._alignment_context, backend, {viewmodel = step.context, avatar_owner = 7, avatar_index = 3})
      local found, exe = pcall(backend._alignment_executable, backend)
      if ok then
        assert(c.owner == 7 and c.avatar_index == 3 and c.exe == (sequence.exe or script.exe))
        line = table.concat({'C', c.unit, c.index, c.generation, string.format('%.0f', c.object), string.format('%.0f', c.registry),
          string.format('%.0f', c.units), string.format('%.0f', c.generations)}, '\t')
      else
        line = 'E\t' .. strip(c)
      end
      line = line .. '\t#\t' .. (found and '' or strip(exe))
    else
      r, why = sampler:sample(expected, step.layers)
    end
    if line then
    elseif r then
      assert(why == nil)
      if not reference then
        local s = backend:player_snapshot()
        reference = s and s.player_identity
      end
      local states, count = {}, 0
      for layer = 1, 32 do
        if r.states[layer] ~= nil then count = count + 1; states[#states + 1] = string.format('%d:%d', layer, r.states[layer]) end
      end
      for layer in pairs(r.states) do assert(type(layer) == 'number' and layer >= 1 and layer <= 32 and layer % 1 == 0) end
      assert(r.l1 == r.states[1] and r.l3 == r.states[3] and r.l10 == r.states[10] and r.l11 == r.states[11] and r.l14 == r.states[14])
      assert(type(r.resource_match) == 'boolean' and type(r.sprint_hypothesis) == 'boolean')
      line = table.concat({'R', r.body, string.format('%.0f', r.body_object), string.format('%.0f', r.graph),
        string.format('%.0f', r.instance), r.layer_count, r.resource, r.resource_match and 1 or 0, r.sprint_hypothesis and 1 or 0,
        r.owner == reference and 1 or 0, count, table.concat(states, ','), #r.unknown_layers, table.concat(r.unknown_layers, ','),
        r.sprint_evidence}, '\t')
    else
      line = 'E\t' .. strip(why)
    end
    print(line .. '\t|\t' .. table.concat(trace, ','))
  end
end

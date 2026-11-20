-- Per-frame recorder for the game's first-person arm rig and the third-person body.
-- Sent through the harness; installs once, each later send restarts the recording.
-- Fetch with:  return TFPRigMotion.report()
-- Per 0.1 s bin it keeps the largest joint change between two frames (rotation component or metres),
-- so a frozen rig reads 0 and an animated one does not.
local s = stingray
local U, W = s.Unit, s.World
local JOINTS = {'r_hand', 'l_hand', 'r_elbow', 'l_elbow', 'r_shoulder', 'l_shoulder', 'r_index_finger1',
    'l_index_finger2', 'attach_hand_r', 'attach_hand_l', 'weapon_aim', 'recoil_offset'}
local BIN = 0.1

local rec = rawget(_G, 'TFPRigMotion')
if not rec then
    rec = {}
    rawset(_G, 'TFPRigMotion', rec)
    local previous = rawget(_G, 'update')
    rawset(_G, 'update', function(dt, ...)
        if rec.on then
            local ok, problem = pcall(rec.step, dt)
            if not ok then rec.err, rec.on = tostring(problem), false end
        end
        if type(previous) == 'function' then return previous(dt, ...) end
    end)
end

local function find(first_person)
    local units = W.units(s.Application.main_world())
    for i = 1, #units do
        local u = units[i]
        if U.has_node(u, 'weapon_aim') and U.has_node(u, 'attach_hand_r') and U.has_node(u, 'r_index_finger1')
            and U.has_node(u, 'fp_cha_avatar') == first_person then
            return u
        end
    end
end

local function snap(u)
    local out = {}
    for i = 1, #JOINTS do
        if U.has_node(u, JOINTS[i]) then
            local k = U.node(u, JOINTS[i])
            local x, y, z, w = s.Quaternion.to_elements(U.local_rotation(u, k))
            local p = U.local_position(u, k)
            out[i] = {x, y, z, w, p.x, p.y, p.z}
        end
    end
    local p = U.world_position(u, 0)
    out.world = {p.x, p.y, p.z}
    return out
end

local function change(a, b, joint)
    local most = 0
    for i = 1, #JOINTS do
        local x, y = a[i], b[i]
        if x and y then
            for c = 1, 7 do
                local d = math.abs(x[c] - y[c])
                if d > most then most = d end
                if d > (joint[i] or 0) then joint[i] = d end
            end
        end
    end
    return most
end

local function track(name, first_person, bin)
    local side = rec[name]
    local u = find(first_person)
    if not u then
        side.missing = side.missing + 1
        side.last = nil
        return
    end
    local now = snap(u)
    if side.last then
        local d = change(side.last, now, side.joint)
        if d > (side.bins[bin] or 0) then side.bins[bin] = d end
        if d > 0 then side.moved = side.moved + 1 end
    end
    side.bins[bin] = side.bins[bin] or 0
    side.last, side.world = now, now.world
    side.first_world = side.first_world or now.world
end

function rec.step(dt)
    rec.t = rec.t + (type(dt) == 'number' and dt or 0.0166)
    rec.frames = rec.frames + 1
    local bin = math.floor(rec.t / BIN) + 1
    rec.top = bin
    track('fp', true, bin)
    track('body', false, bin)
end

local function line(side)
    local parts = {}
    for i = 1, rec.top or 0 do parts[i] = string.format('%.4f', side.bins[i] or -1) end
    local function at(v) return v and string.format('%.2f,%.2f,%.2f', v[1], v[2], v[3]) or '-' end
    local joints = {}
    for i = 1, #JOINTS do
        if (side.joint[i] or 0) > 0 then joints[#joints + 1] = string.format('%s=%.4f', JOINTS[i], side.joint[i]) end
    end
    return {joints = table.concat(joints, ' '), bins = table.concat(parts, ' '), moved = side.moved, missing = side.missing, first_world = at(side.first_world),
        world = at(side.world)}
end

function rec.report()
    return {frames = rec.frames, seconds = rec.t, err = rec.err, fp = line(rec.fp), body = line(rec.body)}
end

rec.t, rec.frames, rec.top, rec.err = 0, 0, 0, nil
rec.fp = {bins = {}, joint = {}, moved = 0, missing = 0}
rec.body = {bins = {}, joint = {}, moved = 0, missing = 0}
rec.on = true
return 'recording'

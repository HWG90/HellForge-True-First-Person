-- Experiment for docs/design/specs/2026-11-22-locked-view-design.md part 4: copy the third-person body's arm joint
-- rotations onto the game's first-person arm rig every frame, for a few seconds, and report whether the writes hold.
-- Sent through the harness:  TFPArmCopy.start(seconds, side)   side: 'both' | 'left' | 'right'
-- Fetch with:                return TFPArmCopy.report()
-- It stops by itself; never leave it running into a level change (HARNESS.md).
local s = stingray
local U, W = s.Unit, s.World
local rec = rawget(_G, 'TFPArmCopy')
if not rec then
    rec = {}
    rawset(_G, 'TFPArmCopy', rec)
    local previous = rawget(_G, 'update')
    rawset(_G, 'update', function(dt, ...)
        if rec.on then
            local ok, problem = pcall(rec.step, dt)
            if not ok then rec.err, rec.on = tostring(problem), false end
        end
        if type(previous) == 'function' then return previous(dt, ...) end
    end)
end
local NAMES = {}
for _, side in ipairs({'r', 'l'}) do
    for _, part in ipairs({'clavicle', 'shoulder', 'upper_arm', 'arm', 'elbow', 'forearm', 'wrist', 'hand'}) do NAMES[#NAMES + 1] = side .. '_' .. part end
    for _, finger in ipairs({'thumb', 'index_finger', 'middle_finger', 'ring_finger', 'pinky_finger', 'little_finger'}) do
        for n = 1, 3 do NAMES[#NAMES + 1] = side .. '_' .. finger .. n end
    end
end
local function find(first_person)
    local units = W.units(s.Application.main_world())
    for i = 1, #units do
        local u = units[i]
        if U.has_node(u, 'weapon_aim') and U.has_node(u, 'attach_hand_r') and U.has_node(u, 'r_index_finger1')
            and U.has_node(u, 'fp_cha_avatar') == first_person then return u end
    end
end
function rec.start(seconds, side)
    rec.left, rec.frames, rec.kept, rec.lost, rec.err = seconds or 3, 0, 0, 0, nil
    rec.side, rec.last, rec.pairs = side or 'both', {}, {}
    local fp, body = find(true), find(false)
    if not fp or not body then rec.err = 'units not found'; return rec.err end
    rec.fp, rec.body = fp, body
    local only_fp, only_body = {}, {}
    for _, name in ipairs(NAMES) do
        local a, b = U.has_node(fp, name), U.has_node(body, name)
        local wanted = rec.side == 'both' or (rec.side == 'left') == (name:sub(1, 1) == 'l')
        if a and b and wanted then rec.pairs[#rec.pairs + 1] = {name, U.node(fp, name), U.node(body, name)}
        elseif a and not b then only_fp[#only_fp + 1] = name elseif b and not a then only_body[#only_body + 1] = name end
    end
    rec.only_fp, rec.only_body = table.concat(only_fp, ' '), table.concat(only_body, ' ')
    local p = U.world_position(fp, 1)
    rec.fp_world = string.format('%.2f %.2f %.2f', p.x, p.y, p.z)
    rec.on = true
    return 'copying ' .. #rec.pairs .. ' joints'
end
function rec.step(dt)
    rec.left = rec.left - dt
    if rec.left <= 0 or not U.alive(rec.fp) or not U.alive(rec.body) then rec.on = false; return end
    rec.frames = rec.frames + 1
    for i, pair in ipairs(rec.pairs) do
        local x, y, z, w = s.Quaternion.to_elements(U.local_rotation(rec.fp, pair[2]))
        local last = rec.last[i]
        if last then
            local d = math.abs(x - last[1]) + math.abs(y - last[2]) + math.abs(z - last[3]) + math.abs(w - last[4])
            if d < 0.0005 then rec.kept = rec.kept + 1 else rec.lost = rec.lost + 1 end
        end
        local q = U.local_rotation(rec.body, pair[3])
        U.set_local_rotation(rec.fp, pair[2], q)
        local a, b, c, e = s.Quaternion.to_elements(q)
        rec.last[i] = {a, b, c, e}
    end
end
function rec.report()
    local names = {}
    for i, pair in ipairs(rec.pairs or {}) do names[i] = pair[1] end
    return {on = rec.on or false, frames = rec.frames, kept = rec.kept, lost = rec.lost, err = rec.err, joints = table.concat(names, ' '),
        only_fp = rec.only_fp, only_body = rec.only_body, fp_world = rec.fp_world}
end
return 'ready'

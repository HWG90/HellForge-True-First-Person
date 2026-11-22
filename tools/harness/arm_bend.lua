-- Experiment (locked-view design part 4): bend ONE joint of the first-person rig at the Lua `render` callback, which
-- runs after the whole update and before the primary draw, then World.update_unit the rig. Judged by capture.
--   TFPArmBend.start(seconds, joint, degrees, where)   where: 'render' | 'update'
--   return TFPArmBend.report()
-- It puts the joint back when it stops. Never leave it running into a level change (HARNESS.md).
local s = stingray
local U, W, Q = s.Unit, s.World, s.Quaternion
local rec = rawget(_G, 'TFPArmBend')
if not rec then
    rec = {calls = {render = 0, update = 0}}
    rawset(_G, 'TFPArmBend', rec)
    for _, name in ipairs({'render', 'update'}) do
        local previous = rawget(_G, name)
        rawset(_G, name, function(...)
            rec.calls[name] = rec.calls[name] + 1
            if rec.on and rec.where == name then
                local ok, problem = pcall(rec.step)
                if not ok then rec.err, rec.on = tostring(problem), false end
            end
            if type(previous) == 'function' then return previous(...) end
        end)
    end
end
local function find()
    local units = W.units(s.Application.main_world())
    for i = 1, #units do
        local u = units[i]
        if U.has_node(u, 'weapon_aim') and U.has_node(u, 'attach_hand_r') and U.has_node(u, 'fp_cha_avatar') then return u end
    end
end
function rec.start(seconds, joint, degrees, where)
    local rig = find()
    if not rig then rec.err = 'rig not found'; return rec.err end
    if not U.has_node(rig, joint) then rec.err = 'no joint ' .. joint; return rec.err end
    rec.rig, rec.node, rec.where, rec.err = rig, U.node(rig, joint), where or 'render', nil
    rec.until_clock = os.clock() + (seconds or 3)
    rec.turn = {degrees or 60}
    rec.frames, rec.kept, rec.moved, rec.last, rec.base = 0, 0, 0, nil, nil
    rec.on = true
    return 'bending ' .. joint .. ' at ' .. rec.where
end
local function elements(q) local x, y, z, w = Q.to_elements(q); return {x, y, z, w} end
local function same(a, b) return math.abs(a[1] - b[1]) + math.abs(a[2] - b[2]) + math.abs(a[3] - b[3]) + math.abs(a[4] - b[4]) < 0.0005 end
function rec.step()
    local rig, node = rec.rig, rec.node
    if not U.alive(rig) or U.num_scene_graph_items(rig) < node then rec.on = false; return end
    local now = elements(U.local_rotation(rig, node))
    if rec.last and same(now, rec.last) then rec.kept = rec.kept + 1 else rec.base = now end   -- the game re-posed it: a fresh base
    if os.clock() >= rec.until_clock then
        U.set_local_rotation(rig, node, Q.from_elements(rec.base[1], rec.base[2], rec.base[3], rec.base[4]))
        W.update_unit(s.Application.main_world(), rig)
        rec.on = false
        return
    end
    rec.frames = rec.frames + 1
    local before = elements(U.world_rotation(rig, node))
    local base = Q.from_elements(rec.base[1], rec.base[2], rec.base[3], rec.base[4])
    local bent = Q.multiply(base, Q.axis_angle(s.Vector3(0, 0, 1), math.rad(rec.turn[1])))
    U.set_local_rotation(rig, node, bent)
    local world = s.Application.main_world()
    W.update_unit(world, rig)
    if rec.pieces then   -- the sleeves, gloves and weapon are units of their own: found again every frame, never kept
        local at, units, n = U.world_position(rig, 1), W.units(world), 0
        for i = 1, #units do
            local u = units[i]
            if u ~= rig and U.alive(u) and U.num_scene_graph_items(u) > 0 and not U.has_node(u, 'weapon_aim') then
                local p = U.world_position(u, 1)
                local dx, dy, dz = p.x - at.x, p.y - at.y, p.z - at.z
                if dx * dx + dy * dy + dz * dz < 0.0025 then W.update_unit(world, u); n = n + 1 end
            end
        end
        rec.piece_count = n
    end
    rec.last = elements(U.local_rotation(rig, node))
    if not same(before, elements(U.world_rotation(rig, node))) then rec.moved = rec.moved + 1 end
end
function rec.report()
    return {on = rec.on or false, frames = rec.frames, kept = rec.kept, world_moved = rec.moved, err = rec.err, where = rec.where,
        pieces = rec.piece_count, render_calls = rec.calls.render, update_calls = rec.calls.update}
end
return 'ready'

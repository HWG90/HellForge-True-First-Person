-- The scripted fake engine the retail arm_pieces.lua and grip_hand.lua are run against
-- (run_arm_pieces.lua, run_grip_hand.lua), unmodified, under LuaJIT.
--
-- Two kinds of engine function:
--   * Stateful ones (Unit, World, Mesh, Application, the backend's memory reads, the clock) answer from a
--     small world the script edits. Every call is recorded with its arguments and its answer; the C test
--     has no world at all: it checks that the C makes the same call with the same argument bits and hands
--     back the recorded answer.
--   * Value maths (Vector3, Quaternion, Matrix4x4 and the vector operators) is a pure function of its
--     arguments. It is written twice, here and in tests/arms_fx.h, on 32-bit floats as the engine's values
--     are, and every call is folded into one running checksum with its arguments and result, so the order
--     and the bits of all of them are compared too.
-- A matrix is a rigid pose (rotation, position), composed as the engine's row matrices are:
-- multiply(local, parent) is "local, then parent" (the same model as the kit's own tests).
local ffi = require('ffi')
local bit = require('bit')
jit.off()

local F = {}
F.ORIGIN = 0x1A2B3C4D5E60
local fb = ffi.new('float[1]')
local function f(x) fb[0] = x; return fb[0] end
local fu = ffi.new('union{float f;uint32_t i;}')
local du = ffi.new('union{double d;uint32_t i[2];}')
-- A NaN is written and summed as one canonical NaN: which sign and payload an operation on NaNs gives
-- depends on the order the CPU instruction gets its operands in, which neither language defines.
local function fhex(x) if x ~= x then return '7fc00000' end; fu.f = x; return string.format('%08x', fu.i) end
local function dhex(x)
  if x == nil then return '~' end
  if x ~= x then return '7ff8000000000000' end
  du.d = x; return string.format('%08x%08x', du.i[1], du.i[0])
end
local function num(x)
  if x == nil then return '~' end
  if x == true then return 'T' elseif x == false then return 'F' end
  if type(x) ~= 'number' then return tostring(x) end
  if x ~= x then return 'nan' elseif x == math.huge then return 'inf' elseif x == -math.huge then return '-inf' end
  return string.format('%.17g', x)
end
F.f, F.fhex, F.dhex, F.num = f, fhex, dhex, num

-- ---- value maths -------------------------------------------------------------------------------
local mh, mc, mfail_at, mfail_text = 0, 0, 0, nil
local function mix(w) mh = bit.tobit(bit.bxor(bit.rol(mh, 5), w) + 0x3243F6A9) end
local function mixf(x) if x ~= x then mix(0x7fc00000) else fu.f = x; mix(fu.i) end end
local function mixd(x) if x ~= x then mix(0x7ff80000); mix(0) else du.d = x; mix(du.i[1]); mix(du.i[0]) end end
local function enter(id)
  mc = mc + 1; mix(id)
  if mc == mfail_at then error(mfail_text, 0) end
end
local vmt, qmt, mmt = {}, {}, {}
local function v(x, y, z) return setmetatable({x = x, y = y, z = z}, vmt) end
local function q(x, y, z, w) return setmetatable({x = x, y = y, z = z, w = w}, qmt) end
local function m(rotation, position) return setmetatable({q = rotation, p = position}, mmt) end
local function mixv(a) mixf(a.x); mixf(a.y); mixf(a.z) end
local function mixq(a) mixf(a.x); mixf(a.y); mixf(a.z); mixf(a.w) end
local function mixm(a) mixq(a.q); mixv(a.p) end
-- the arithmetic itself, uncounted (also used by the world for linked bones)
local function qmul(a, b)
  return q(f(a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y), f(a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x),
    f(a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w), f(a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z))
end
local function qrot(a, p)
  local r = qmul(qmul(a, q(p.x, p.y, p.z, 0)), q(-a.x, -a.y, -a.z, a.w))
  return v(r.x, r.y, r.z)
end
local function vlen(a) return f(math.sqrt(a.x * a.x + a.y * a.y + a.z * a.z)) end
local function vnorm(a) local n = 1 / vlen(a); return v(f(a.x * n), f(a.y * n), f(a.z * n)) end
local function mmul(a, b)
  local p = qrot(b.q, a.p)
  return m(qmul(b.q, a.q), v(f(p.x + b.p.x), f(p.y + b.p.y), f(p.z + b.p.z)))
end
local function minv(a)
  local i = q(-a.q.x, -a.q.y, -a.q.z, a.q.w)
  return m(i, qrot(i, v(-a.p.x, -a.p.y, -a.p.z)))
end
local IDENTITY_Q = q(0, 0, 0, 1)
local IDENTITY = m(IDENTITY_Q, v(0, 0, 0))
F.v, F.q, F.m, F.qmul, F.qrot, F.mmul, F.minv, F.IDENTITY = v, q, m, qmul, qrot, mmul, minv, IDENTITY

vmt.__add = function(a, b) enter(2); mixv(a); mixv(b); local r = v(f(a.x + b.x), f(a.y + b.y), f(a.z + b.z)); mixv(r); return r end
vmt.__sub = function(a, b) enter(3); mixv(a); mixv(b); local r = v(f(a.x - b.x), f(a.y - b.y), f(a.z - b.z)); mixv(r); return r end
vmt.__mul = function(a, n) enter(4); mixv(a); mixd(n); local r = v(f(a.x * n), f(a.y * n), f(a.z * n)); mixv(r); return r end
vmt.__unm = function(a) enter(5); mixv(a); local r = v(-a.x, -a.y, -a.z); mixv(r); return r end
local function new_math()
  local V = setmetatable({}, {__call = function(_, x, y, z)
    enter(1); mixd(x); mixd(y); mixd(z); local r = v(f(x), f(y), f(z)); mixv(r); return r
  end})
  V.length = function(a) enter(6); mixv(a); local r = vlen(a); mixd(r); return r end
  V.dot = function(a, b) enter(7); mixv(a); mixv(b); local r = f(a.x * b.x + a.y * b.y + a.z * b.z); mixd(r); return r end
  V.cross = function(a, b)
    enter(8); mixv(a); mixv(b)
    local r = v(f(a.y * b.z - a.z * b.y), f(a.z * b.x - a.x * b.z), f(a.x * b.y - a.y * b.x)); mixv(r); return r
  end
  V.normalize = function(a) enter(9); mixv(a); local r = vnorm(a); mixv(r); return r end
  V.lerp = function(a, b, t)
    enter(10); mixv(a); mixv(b); mixd(t)
    local r = v(f(a.x + (b.x - a.x) * t), f(a.y + (b.y - a.y) * t), f(a.z + (b.z - a.z) * t)); mixv(r); return r
  end
  V.x = function(a) enter(11); mixv(a); return a.x end
  V.y = function(a) enter(12); mixv(a); return a.y end
  V.z = function(a) enter(13); mixv(a); return a.z end
  local Q = {}
  Q.identity = function() enter(14); return IDENTITY_Q end
  Q.axis_angle = function(axis, angle)
    enter(15); mixv(axis); mixd(angle)
    local a = vnorm(axis); local s = math.sin(angle / 2)
    local r = q(f(a.x * s), f(a.y * s), f(a.z * s), f(math.cos(angle / 2))); mixq(r); return r
  end
  Q.multiply = function(a, b) enter(16); mixq(a); mixq(b); local r = qmul(a, b); mixq(r); return r end
  Q.rotate = function(a, p) enter(17); mixq(a); mixv(p); local r = qrot(a, p); mixv(r); return r end
  Q.lerp = function(a, b, t)
    enter(18); mixq(a); mixq(b); mixd(t)
    local r
    if t >= 1 then r = b elseif t <= 0 then r = a
    else
      local x, y, z, w = a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t
      local n = math.sqrt(x * x + y * y + z * z + w * w)
      r = q(f(x / n), f(y / n), f(z / n), f(w / n))
    end
    mixq(r); return r
  end
  Q.inverse = function(a) enter(19); mixq(a); local r = q(-a.x, -a.y, -a.z, a.w); mixq(r); return r end
  Q.to_elements = function(a) enter(20); mixq(a); return a.x, a.y, a.z, a.w end
  Q.from_elements = function(x, y, z, w)
    enter(21); mixd(x); mixd(y); mixd(z); mixd(w); local r = q(f(x), f(y), f(z), f(w)); mixq(r); return r
  end
  local M4 = {}
  M4.identity = function() enter(22); return IDENTITY end
  M4.from_quaternion_position = function(a, p) enter(23); mixq(a); mixv(p); return m(a, p) end
  M4.multiply = function(a, b) enter(24); mixm(a); mixm(b); local r = mmul(a, b); mixm(r); return r end
  M4.inverse = function(a) enter(25); mixm(a); local r = minv(a); mixm(r); return r end
  M4.translation = function(a) enter(26); mixm(a); return a.p end
  M4.rotation = function(a) enter(27); mixm(a); return a.q end
  return V, Q, M4
end
local function vhex(a) return fhex(a.x) .. fhex(a.y) .. fhex(a.z) end
local function qhex(a) return fhex(a.x) .. fhex(a.y) .. fhex(a.z) .. fhex(a.w) end
local function mhex(a) return qhex(a.q) .. vhex(a.p) end
F.vhex, F.qhex, F.mhex = vhex, qhex, mhex

-- ---- the world -----------------------------------------------------------------------------------
-- unit: {id, ref, address, handle, alive, items, count (the u32 at +0x70, default #names), names, links, parent,
--        poses={[node]=pose}, linked={[node]={unit,node}}, locals={[node]=pose}, meshes={{visible,materials}},
--        bad={names=,links=,ptr_names=,ptr_links=,parent=,count=,object=}}
function F.world()
  local W = {units = {}, by_ref = {}, by_address = {}, tables = {}, table_ids = {}, serial = 0, trace = {}, calls = {}, now = 0, clock = 0}
  local trace
  local function rec(name, args, body, ...)
    local key = name .. ':' .. args
    W.calls[name] = (W.calls[name] or 0) + 1
    local fail = W.fail
    if fail and fail[1] == name and fail[2] == W.calls[name] then
      trace[#trace + 1] = key .. '!' .. fail[3]
      error(fail[3], 0)
    end
    local ok, a, b = pcall(body, ...)
    if not ok then
      a = tostring(a):gsub('^.-:%d+: ', '')
      trace[#trace + 1] = key .. '!' .. a
      error(a, 0)
    end
    trace[#trace + 1] = key .. '>' .. b
    return a
  end
  local function hid(h) if h == nil then return '0' end; return tostring(type(h) == 'table' and h.id or '?') end
  local function live(h, node)
    local u = type(h) == 'table' and W.units[h.id]
    if not u or u.handle ~= h then error('not a unit', 0) end
    if not u.alive then error('unit is not alive', 0) end
    if node and (type(node) ~= 'number' or node < 1 or node > u.items or node % 1 ~= 0) then error('node ' .. num(node) .. ' out of range', 0) end
    return u
  end
  local function pose_of(u, node, depth)
    local link = u.linked[node]
    if link and (depth or 0) < 8 then return mmul(u.locals[node] or IDENTITY, pose_of(link[1], link[2], (depth or 0) + 1)) end
    return u.poses[node] or IDENTITY
  end
  W.pose_of = pose_of
  function W.unit(id, spec)
    local u = {id = id, ref = spec.ref, address = spec.address, handle = {id = id}, alive = true, items = spec.items or #spec.names,
      names = spec.names, links = spec.links or {}, parent = spec.parent or 0, poses = {}, linked = {}, locals = {}, meshes = spec.meshes or {}, bad = {}}
    W.units[id] = u
    if u.ref then W.by_ref[u.ref] = u end
    if u.address then W.by_address[u.address] = u end
    return u
  end
  -- memory
  local function u32s(list, count)
    local out = {}
    for i = 1, count do
      local n = list[i] or 0
      out[i] = string.char(n % 256, math.floor(n / 256) % 256, math.floor(n / 65536) % 256, math.floor(n / 16777216) % 256)
    end
    return table.concat(out)
  end
  local function hexbytes(s) return (s:gsub('.', function(c) return string.format('%02x', c:byte()) end)) end
  local function region(at)
    local base = at - (at - F.ORIGIN) % 0x100000          -- units sit 1 MiB apart from ORIGIN, every byte of an address in use
    local u = W.by_address[base]
    if not u then error('Memory unavailable', 0) end
    return u, at - base
  end
  local backend = {}
  function backend:_alignment_context(t)
    return rec('obj', num(t.viewmodel) .. ',' .. num(t.avatar_owner) .. ',' .. num(t.avatar_index), function()
      local u = W.by_ref[t.viewmodel]
      if t.viewmodel == nil then error('Native first-person viewmodel unavailable', 0) end
      if not u or not u.address then error('Viewmodel registry index unavailable', 0) end
      if u.bad.object then error('Viewmodel generation retired', 0) end
      return {object = u.address}, dhex(u.address)
    end)
  end
  function backend:_u32(at)
    return rec('u32', num(at), function()
      local u, off = region(at)
      if off ~= 0x70 or u.bad.count then error('Memory unavailable', 0) end
      local n = u.count or #u.names
      return n, dhex(n)
    end)
  end
  function backend:_ptr(at)
    return rec('ptr', num(at), function()
      local u, off = region(at)
      local to
      if off == 0xA0 and not u.bad.ptr_names then to = u.address + 0x1000
      elseif off == 0x98 and not u.bad.ptr_links then to = u.address + 0x2000
      else error(string.format('Pointer unavailable at0x%x (value0x%x)', at, 0), 0) end
      return to, dhex(to)
    end)
  end
  function backend:_read(at, size)
    return rec('rd', num(at) .. ',' .. num(size), function()
      local u, off = region(at)
      local bytes
      if off == 0x1000 and not u.bad.names then bytes = u32s(u.names, size / 4)
      elseif off == 0x2000 and not u.bad.links then bytes = u32s(u.links, size / 4)
      elseif off == 0x1D0 and not u.bad.parent and size == 8 then
        local p = u.parent
        bytes = u32s({p % 4294967296, math.floor(p / 4294967296)}, 2)
      else error('Memory unavailable', 0) end
      return bytes, hexbytes(bytes)
    end)
  end
  W.backend = backend
  local V, Q, M4 = new_math()
  local world_object = {id = 1}
  local sr = {Vector3 = V, Quaternion = Q, Matrix4x4 = M4}
  sr.Matrix4x4Box = function(a) return {m = a, unbox = function(self) return self.m end, store = function(self, n) self.m = n end} end
  sr.Application = {main_world = function()
    return rec('mw', '', function() if W.no_world then return nil, '0' end; return world_object, '1' end)
  end}
  sr.World = {link_unit = function(w, child, node, parent, parent_node)
    return rec('lk', hid(w) .. ',' .. hid(child) .. ',' .. num(node) .. ',' .. hid(parent) .. ',' .. num(parent_node), function()
      if w ~= world_object then error('not a world', 0) end
      local c, p = live(child, node), live(parent, parent_node)
      c.linked[node] = {p, parent_node}
      c.links[node] = 2 + (parent_node - 1) * 65536         -- the unit's link table shows the new link, as the game's does
      return nil, ''
    end)
  end}
  sr.Unit = {
    alive = function(h)
      return rec('al', hid(h), function()
        local u = type(h) == 'table' and W.units[h.id]
        if not u then error('not a unit', 0) end
        local answer = u.alive
        if u.odd_alive then answer = 1 end -- a truthy answer that is not `true`
        return answer, answer == true and 'T' or 'F'
      end)
    end,
    num_scene_graph_items = function(h)
      return rec('ns', hid(h), function() local u = live(h); return u.items, dhex(u.items) end)
    end,
    world_pose = function(h, node)
      return rec('wp', hid(h) .. ',' .. num(node), function() local p = pose_of(live(h, node), node); return p, mhex(p) end)
    end,
    world_position = function(h, node)
      return rec('wo', hid(h) .. ',' .. num(node), function() local p = pose_of(live(h, node), node).p; return p, vhex(p) end)
    end,
    world_rotation = function(h, node)
      return rec('wr', hid(h) .. ',' .. num(node), function() local p = pose_of(live(h, node), node).q; return p, qhex(p) end)
    end,
    set_local_pose = function(h, node, pose)
      return rec('sp', hid(h) .. ',' .. num(node) .. ',' .. mhex(pose), function() live(h, node).locals[node] = pose; return nil, '' end)
    end,
    num_meshes = function(h)
      return rec('nm', hid(h), function() local u = live(h); local n = u.mesh_count or #u.meshes; return n, dhex(n) end)
    end,
    mesh = function(h, index)
      return rec('ms', hid(h) .. ',' .. num(index), function()
        local mesh = live(h).meshes[index]
        if not mesh then return nil, '0' end
        mesh.id = h.id * 100 + index
        return mesh, tostring(mesh.id)
      end)
    end,
    set_mesh_visibility = function(h, index, visible)
      return rec('sv', hid(h) .. ',' .. num(index) .. ',' .. num(visible), function()
        local mesh = live(h).meshes[index]
        if not mesh then error('mesh ' .. num(index) .. ' out of range', 0) end
        mesh.visible = visible
        return nil, ''
      end)
    end,
  }
  sr.Mesh = {
    visibility = function(mesh)
      return rec('mv', hid(mesh), function()
        if type(mesh) ~= 'table' then error('not a mesh', 0) end
        return mesh.visible, mesh.visible == true and 'T' or 'F'
      end)
    end,
    num_materials = function(mesh)
      return rec('mm', hid(mesh), function()
        if type(mesh) ~= 'table' then error('not a mesh', 0) end
        return mesh.materials, dhex(mesh.materials)
      end)
    end,
  }
  W.sr = sr
  function W.clock_fn()
    return rec('clk', '', function()
      if W.clock == 1 then error('clock broke', 0) end
      return W.now, dhex(W.now)
    end)
  end
  -- Functions the engine does not have, for a run or from one operation on: "Unit_alive", or a whole
  -- table: "Vector3". Each call starts again from the whole engine.
  local whole, call_of_v = {}, getmetatable(V)
  for name, value in pairs(sr) do
    whole[name] = value
    if type(value) == 'table' then
      local copy = {}
      for k, f in pairs(value) do copy[k] = f end
      whole[name] = {value, copy}
    end
  end
  function W.remove(missing)
    for name, value in pairs(whole) do
      if type(value) == 'table' then
        sr[name] = value[1]
        for k, f in pairs(value[2]) do value[1][k] = f end
      else sr[name] = value end
    end
    setmetatable(V, call_of_v)
    for _, name in ipairs(missing or {}) do
      local t, fn = name:match('^(%w+)_(.+)$')
      if name == 'Vector3_call' then setmetatable(sr.Vector3, nil)
      elseif t then sr[t][fn] = nil else sr[name] = nil end
    end
  end

  -- ---- edits ------------------------------------------------------------------------------------
  local function pose_from(e, at) return m(q(f(e[at]), f(e[at + 1]), f(e[at + 2]), f(e[at + 3])), v(f(e[at + 4]), f(e[at + 5]), f(e[at + 6]))) end
  local edit = {}
  function edit.unit(e) W.unit(e[2], e[3]) end
  function edit.pose(e) W.units[e[2]].poses[e[3]] = pose_from(e, 4) end                 -- {'pose',unit,node,qx,qy,qz,qw,px,py,pz}
  function edit.alive(e) W.units[e[2]].alive = e[3] end
  function edit.odd_alive(e) W.units[e[2]].odd_alive = e[3] end
  function edit.items(e) W.units[e[2]].items = e[3] end
  function edit.count(e) W.units[e[2]].count = e[3] end                                   -- the u32 at +0x70; false: back to #names
  function edit.names(e) W.units[e[2]].names = e[3] end
  function edit.links(e) W.units[e[2]].links = e[3] end
  function edit.link(e) W.units[e[2]].links[e[3]] = e[4] end                              -- one word, 1-based
  function edit.name(e) W.units[e[2]].names[e[3]] = e[4] end
  function edit.parent(e) W.units[e[2]].parent = e[3] end
  function edit.bad(e) W.units[e[2]].bad[e[3]] = e[4] end
  function edit.mesh(e) W.units[e[2]].meshes[e[3]] = {visible = e[4], materials = e[5]} end
  function edit.mesh_count(e) W.units[e[2]].mesh_count = e[3] end
  function edit.engine_link(e)                                                            -- the game links a bone itself
    local u = W.units[e[2]]
    if e[4] then u.linked[e[3]] = {W.units[e[4]], e[5]} else u.linked[e[3]] = nil end
  end
  function edit.engine_local(e) W.units[e[2]].locals[e[3]] = e[4] and pose_from(e, 4) or nil end
  function edit.no_world(e) W.no_world = e[2] end
  function edit.handles(e)                                                                -- {'handles',key,{{ref,unit},...},fresh}
    local t = W.tables[e[2]]
    if not t or e[4] then
      t = {}; W.tables[e[2]] = t
      W.serial = W.serial + 1; W.table_ids[t] = W.serial
    else
      for k in pairs(t) do t[k] = nil end
    end
    for _, row in ipairs(e[3]) do t[row[1]] = W.units[row[2]].handle end
  end
  function edit.handle(e)                                                                 -- {'handle',key,ref,unit or false}: in place
    W.tables[e[2]][e[3]] = e[4] and W.units[e[4]].handle or nil
  end
  function W.apply(edits)
    for _, e in ipairs(edits or {}) do
      local fn = edit[e[1]]
      if not fn then error('unknown edit ' .. tostring(e[1])) end
      fn(e)
    end
  end

  -- ---- one operation ----------------------------------------------------------------------------
  function W.begin(op)
    trace = {}; W.trace = trace; W.calls = {}
    W.fail = op.fail or nil
    mh, mc = 0, 0
    mfail_at, mfail_text = 0, nil
    if op.mfail then mfail_at, mfail_text = op.mfail[1], op.mfail[2] end
    W.now = op.now or W.now
    W.clock = op.clock or 0
  end
  function W.snapshot_text(s)
    if s == nil then return '~' end
    return table.concat({num(s.body), num(s.viewmodel), num(s.avatar_owner), num(s.avatar_index), num(s.aiming), num(s.shoulder_aim),
      num(s.weapon), num(s.request_weapon), num(s.weapon_transition)}, ',')
  end
  function W.handles_text(t)
    if t == nil then return '~' end
    local rows = {}
    for ref, handle in pairs(t) do rows[#rows + 1] = num(ref) .. '=' .. handle.id end
    return W.table_ids[t] .. ':' .. table.concat(rows, ',')
  end
  function W.inputs(op, s, handles)
    return table.concat({W.snapshot_text(s), num(op.active == true), W.handles_text(handles),
      op.mfail and (op.mfail[1] .. ':' .. op.mfail[2]) or '0'}, '|')
  end
  function W.finish()
    return table.concat(trace, ';') .. '|' .. mc .. ',' .. bit.tohex(mh)
  end
  return W
end

-- The retail file's bytes as they are, under the name the mod requires it by.
function F.retail(kit, path)
  local file = assert(io.open(kit .. '/' .. path, 'rb'))
  local source = file:read('*a')
  file:close()
  return assert(loadstring(source, '@' .. path))()
end
function F.install_mod()
  local compat = {kind = type, number = tonumber}
  mod = {dir = 'KIT', require = function(path)
    if path == 'src/first_person/compat51.lua' then return compat end
    error('unexpected require ' .. path)
  end}
end
-- The snapshot table of an operation: `s` false/nil is no table at all.
function F.snapshot(spec)
  if not spec then return nil end
  local s = {}
  for k, value in pairs(spec) do s[k] = value end
  return s
end
return F

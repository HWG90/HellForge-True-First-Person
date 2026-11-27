-- The levers' kernel: installed once per game session into _G.TFP_LEVERS by tools/harness/levers.py
-- (docs/design/HARNESS.md "Levers"). It reads the game's memory through ReadProcessMemory only (a bad address is
-- a nil, never a fault), writes data pages only and keeps the bytes it replaced, and calls a game function only
-- while the code at its address still matches the pattern the site was found by in the saved image of game.dll
-- (build 25480438). Offsets HellForge 0.5 moves are read out of the game's own instructions each time.
-- `SITES` and `VERSION` are put in front of this file by levers.py.
local K = rawget(_G, 'TFP_LEVERS')
if K and K.version == VERSION then return 'kernel ' .. VERSION .. ' already in' end
local ffi, bit = require('ffi'), require('bit')
for _, decl in ipairs({
    'void *TFPL_GetCurrentProcess(void) __asm__("GetCurrentProcess");',
    'int TFPL_ReadProcessMemory(void *, const void *, void *, size_t, size_t *) __asm__("ReadProcessMemory");',
    'int TFPL_WriteProcessMemory(void *, void *, const void *, size_t, size_t *) __asm__("WriteProcessMemory");',
    'size_t TFPL_VirtualQuery(const void *, void *, size_t) __asm__("VirtualQuery");',
    'void *TFPL_GetModuleHandleA(const char *) __asm__("GetModuleHandleA");',
    'void *TFPL_GetProcAddress(void *, const char *) __asm__("GetProcAddress");',
    [[typedef struct { void *base; void *allocation_base; uint32_t allocation_protection; uint16_t partition;
        uint16_t reserved; size_t size; uint32_t state; uint32_t protection; uint32_t type; } TFPLRegion;]],
}) do pcall(ffi.cdef, decl) end
local kernel = ffi.load('kernel32')
local process = kernel.TFPL_GetCurrentProcess()
local old = K
K = { version = VERSION, sites = SITES, journal = old and old.journal or {}, keep = old and old.keep or {},
    fn = {}, log = old and old.log or {}, types = old and old.types or {} }
rawset(_G, 'TFP_LEVERS', K)

local scratch, got = ffi.new('uint8_t[?]', 65536), ffi.new('size_t[1]')
local region = ffi.new('TFPLRegion[1]')
local function say(text) K.log[#K.log + 1] = text; if #K.log > 400 then table.remove(K.log, 1) end; return text end
K.say = say
local function refuse(text) error('refused: ' .. text, 0) end
K.refuse = refuse

function K.read(address, size)
    if type(address) ~= 'number' or address < 65536 or address >= 2 ^ 47 or size < 1 or size > 65536 then return nil end
    if kernel.TFPL_ReadProcessMemory(process, ffi.cast('const void *', address), scratch, size, got) == 0 then return nil end
    if tonumber(got[0]) ~= size then return nil end
    return ffi.string(scratch, size)
end
local read = K.read
local function word(s, o) o = o or 0; local a, b, c, d = s:byte(o + 1, o + 4); return a + b * 256 + c * 65536 + d * 16777216 end
K.word = word
function K.u32(address) local s = read(address, 4); return s and word(s) end
function K.u8(address) local s = read(address, 1); return s and s:byte() end
function K.ptr(address)
    local s = read(address, 8); if not s then return nil end
    local v = word(s) + word(s, 4) * 4294967296
    return v >= 65536 and v < 2 ^ 47 and v or nil
end
function K.f32(address)
    local s = read(address, 4); if not s then return nil end
    return tonumber(ffi.cast('const float *', ffi.cast('const char *', s))[0])
end
function K.vec3(address)
    local s = read(address, 12); if not s then return nil end
    local f = ffi.cast('const float *', ffi.cast('const char *', s))
    return tonumber(f[0]), tonumber(f[1]), tonumber(f[2])
end
local u32, ptr = K.u32, K.ptr
function K.hex(s) return (s:gsub('.', function(c) return string.format('%02x', c:byte()) end)) end
function K.le32(n) n = n % 4294967296; return string.char(n % 256, math.floor(n / 256) % 256, math.floor(n / 65536) % 256, math.floor(n / 16777216)) end
function K.f32le(...) local n = select('#', ...); local f = ffi.new('float[?]', n, ...); return ffi.string(f, 4 * n) end
-- a 64-bit id typed as 16 hex digits -> uint64_t cdata (LuaJIT numbers lose the low bits)
function K.id64(hex16)
    if type(hex16) ~= 'string' or #hex16 ~= 16 or not hex16:match('^%x+$') then refuse('an id is 16 hex digits, got ' .. tostring(hex16)) end
    local v = ffi.new('uint64_t[1]'); local b = ffi.cast('uint8_t *', v)
    for i = 0, 7 do b[7 - i] = tonumber(hex16:sub(i * 2 + 1, i * 2 + 2), 16) end
    return v
end
function K.id64_hex(address) local s = read(address, 8); return s and K.hex(s:reverse()) end

-- Data pages only: committed, private (not an image), read-write. The bytes before go into the journal.
function K.write(address, bytes, what)
    if kernel.TFPL_VirtualQuery(ffi.cast('const void *', address), region, ffi.sizeof(region[0])) == 0 then refuse('no region at ' .. string.format('0x%X', address)) end
    local r = region[0]
    if r.state ~= 0x1000 or r.protection ~= 0x04 or r.type == 0x1000000 then
        refuse(string.format('%s: 0x%X is not a private read-write data page (state %X protect %X type %X)', what, address, r.state, r.protection, r.type))
    end
    local before = read(address, #bytes)
    if not before then refuse(what .. ': unreadable before the write') end
    if kernel.TFPL_WriteProcessMemory(process, ffi.cast('void *', address), bytes, #bytes, got) == 0 or tonumber(got[0]) ~= #bytes then
        refuse(what .. ': the write failed')
    end
    if read(address, #bytes) ~= bytes then refuse(what .. ': the read-back differs') end
    K.journal[#K.journal + 1] = { at = address, before = before, after = bytes, what = what }
    say(string.format('wrote %s: 0x%X %s -> %s', what, address, K.hex(before), K.hex(bytes)))
    return true
end
-- Puts back the newest n writes where the bytes are still what the lever wrote.
function K.undo(n)
    local done, skipped = 0, 0
    for _ = 1, n or #K.journal do
        local e = table.remove(K.journal); if not e then break end
        if read(e.at, #e.after) == e.after and kernel.TFPL_WriteProcessMemory(process, ffi.cast('void *', e.at), e.before, #e.before, got) ~= 0 then
            done = done + 1; say(string.format('undid %s at 0x%X', e.what, e.at))
        else skipped = skipped + 1; say(string.format('left %s at 0x%X: changed since', e.what, e.at)) end
    end
    return done, skipped
end

function K.module(name)
    local h = kernel.TFPL_GetModuleHandleA(name)
    local base = h ~= nil and tonumber(ffi.cast('uintptr_t', h)) or nil
    if not base or base == 0 then return nil end
    return base
end
function K.game() return K.module('game.dll') or refuse('game.dll is not loaded') end

-- A site: {module, rva, kind, pattern [, at, size, minus]}; the pattern's `??` are the bytes HellForge may rewrite
-- (displacements) or that differ by build. start: the function's address. rip: the global an instruction names.
-- u32: the 4 bytes at `at` (less `minus`), read from the code as it is now.
local function site_value(name)
    local s = K.sites[name] or refuse('no site called ' .. tostring(name))
    local base = K.module(s.module) or refuse(s.module .. ' is not loaded')
    local n = #s.pattern / 2
    local have = read(base + s.rva, n) or refuse(string.format('%s: the code at %s+0x%X is unreadable', name, s.module, s.rva))
    for i = 1, n do
        local want = s.pattern:sub(i * 2 - 1, i * 2)
        if want ~= '??' and tonumber(want, 16) ~= have:byte(i) then
            refuse(string.format('%s: the code at %s+0x%X differs from the reviewed pattern at byte %d (%s)', name, s.module, s.rva, i - 1, K.hex(have)))
        end
    end
    if s.kind == 'start' then return base + s.rva end
    local d = word(have, s.at)
    if s.kind == 'rip' then
        if d >= 0x80000000 then d = d - 4294967296 end
        return base + s.rva + s.size + d
    end
    return d - (s.minus or 0)
end
K.site = site_value
function K.native(name, ctype)
    local at = site_value(name)                      -- checked at every call, cast once
    local f = K.fn[name]
    if not f or f.at ~= at then f = { at = at, call = ffi.cast(ctype, at) }; K.fn[name] = f end
    return f.call
end
function K.global(name) return ptr(site_value(name)) or refuse(name .. ': the global is empty (not in a session?)') end

-- The engine's open-addressed u32 -> u32 map: {u64 buckets, u32 capacity, u32 empty key, u32 multiplier}.
function K.hashmap(header, key)
    local h = read(header, 20); if not h then return nil end
    local data, cap, empty, mult = ptr(header), word(h, 8), word(h, 12), word(h, 16)
    if not data or cap == 0 or cap > 1048576 or bit.band(cap, cap - 1) ~= 0 then return nil end
    local product = tonumber(ffi.cast('uint32_t', ffi.new('uint64_t', key) * ffi.new('uint64_t', mult)))
    for probe = 0, math.min(cap, 4096) - 1 do
        local row = read(data + 8 * bit.band(product + probe, cap - 1), 8); if not row then return nil end
        local found = word(row)
        if found == key then local v = word(row, 4); return v ~= 0xffffffff and v or nil end
        if found == empty then return nil end
    end
end

-- HellForge's Lua bridge (hellforge_modloader.dll's exports, sdk/hellforge_lua.h): the checked package load.
function K.bridge(name, ctype)
    local key = 'bridge:' .. name
    if K.fn[key] then return K.fn[key].call end
    local h = kernel.TFPL_GetModuleHandleA('hellforge_modloader.dll')
    if h == nil then refuse('hellforge_modloader.dll is not in the game') end
    local p = kernel.TFPL_GetProcAddress(h, name)
    if p == nil then refuse('the loader does not export ' .. name) end
    K.fn[key] = { call = ffi.cast(ctype, p) }
    return K.fn[key].call
end
function K.mod()
    if not K.hfmod then
        K.hfmod = K.bridge('hellforge_lua_mod', 'void *(*)(const char *)')('tfp-harness-levers')
        if K.hfmod == nil then K.hfmod = nil; refuse('the loader gave no Lua mod handle') end
    end
    return K.hfmod
end
function K.last() return ffi.string(K.bridge('hellforge_lua_last', 'const char *(*)(void *)')(K.mod())) end

-- ---------------------------------------------------------------------------------------- the session --
function K.state()
    local g = K.game()
    local game = K.global('GAME_STATE')
    local state = u32(game + site_value('STATE_VALUE'))
    local mode = ptr(site_value('GAME_MODE'))
    return state, mode and u32(mode + 8) or 0
end
function K.need_mission()
    local state, mode = K.state()
    if state ~= 4 or mode ~= 1 then refuse(string.format('only in a mission (state %s, mode %s)', tostring(state), tostring(mode))) end
end
-- Solo and host: this machine's peer is the host's and the only player's. Spawns and loadout edits happen in this
-- machine's world only, so with other players they would see a broken item.
function K.need_solo()
    local net = K.global('NET')
    local peer, host = read(net + 0xB398, 8), read(net + 0xB3A8, 8)
    local players = K.global('PLAYER')
    local count = u32(players + 0x84)
    if not peer or peer ~= host or peer == string.rep('\0', 8) or count ~= 1 then
        refuse('only alone and as the host (' .. tostring(count) .. ' players)')
    end
    return peer, players
end
-- The local Helldiver: its entity, from its network id through the game's own EntityOfNetwork.
function K.avatar()
    K.need_mission()
    local peer, players = K.need_solo()
    if read(players + 0x2C8, 8) ~= peer then refuse('player slot 0 is not this machine\'s') end
    if u32(players + 0x2E0) ~= 3 then refuse('the Helldiver is not on the ground (lifecycle ' .. tostring(u32(players + 0x2E0)) .. ')') end
    local network = u32(players + 0x3A8)
    local out = ffi.new('uint32_t[1]')
    K.native('ENTITY_OF_NETWORK', 'uint32_t *(*)(uint32_t *, uint32_t)')(out, network)
    local e = tonumber(out[0])
    if e == 0 or e == 0xffffffff or e == u32(site_value('ENTITY_NONE')) then refuse('the Helldiver has no entity now') end
    return e, network
end
-- An entity's descriptor: its resource (16 hex digits) and whether this machine owns it.
function K.descriptor(entity)
    local d = K.native('ENTITY_DESCRIPTOR', 'const uint8_t *(*)(uint32_t)')(entity)
    if d == nil then return nil end
    local at = tonumber(ffi.cast('uintptr_t', d))
    local raw = read(at, 24); if not raw then return nil end
    return { at = at, resource = K.hex(raw:sub(1, 8):reverse()), unit = word(raw, 12), owned = raw:byte(21) % 2 == 1 }
end
function K.inventory(entity)
    local inv = K.global('INVENTORY')
    local i = K.hashmap(inv + 0x28, entity) or refuse('the Helldiver has no inventory row')
    local rows = ptr(inv + 0x50) or refuse('no inventory rows')
    local raw = read(rows + i * 0x30, 0x30) or refuse('the inventory row is unreadable')
    return { manager = inv, at = rows + i * 0x30, primary = word(raw, 0), secondary = word(raw, 4), support = word(raw, 8),
        backpack = word(raw, 12), held = word(raw, 16), active = word(raw, 28), raw = raw }
end
-- The motion record's index and where its position lives (MotionManager, sdk/hellforge_globals.h).
function K.motion(entity)
    local m = K.global('MOTION')
    local i = K.hashmap(m + 0x48A0, entity); if not i then return nil end
    local d = ptr((ptr(m + 0x48B8) or 0) + i * 8)
    if not d or u32(d + 8) ~= entity then return nil end
    return { manager = m, index = i, live = (K.u8(d + 0x14) or 0) % 2 == 1, record = (ptr(m + 0x48C8) or 0) + i * 0x84,
        position = (ptr(m + 0x48D0) or 0) + i * 0xA4, source = (ptr(m + 0x48D8) or 0) + i * 0x2C }
end
function K.position(entity)
    local m = K.motion(entity) or refuse('entity ' .. tostring(entity) .. ' has no motion record')
    local x, y, z = K.vec3(m.position)
    if not x or x ~= x or math.abs(x) > 1e6 or math.abs(y) > 1e6 or math.abs(z) > 1e6 then refuse('the motion position does not look like one') end
    return x, y, z, m
end

-- Per-frame jobs: a job is a function called once a game update until it returns true (or fails, or 600 frames
-- pass). The game's `update` is wrapped once per session; the wrapper finds the kernel of the moment.
function K.tick()
    local jobs = K.jobs; if not jobs or #jobs == 0 then return end
    for i = #jobs, 1, -1 do
        local j = jobs[i]; j.frames = j.frames + 1
        local ok, done = pcall(j.fn, j.frames)
        if not ok then say('job ' .. j.name .. ' failed: ' .. tostring(done)) end
        if not ok or done or j.frames >= 600 then table.remove(jobs, i) end
    end
end
function K.job(name, fn)
    K.jobs = K.jobs or {}
    if #K.jobs >= 8 then refuse('eight jobs already run') end
    K.jobs[#K.jobs + 1] = { name = name, fn = fn, frames = 0 }
    if not rawget(_G, 'TFP_LEVERS_UPDATE') then
        local previous = rawget(_G, 'update')
        if type(previous) ~= 'function' then refuse('the game has no update to ride on') end
        rawset(_G, 'TFP_LEVERS_UPDATE', previous)
        rawset(_G, 'update', function(...)
            local k = rawget(_G, 'TFP_LEVERS'); if k and k.tick then pcall(k.tick) end
            return previous(...)
        end)
    end
end
-- A write to an address the caller derived from a manager this frame, without a journal entry (per-frame holds).
function K.poke(address, bytes)
    return kernel.TFPL_WriteProcessMemory(process, ffi.cast('void *', address), bytes, #bytes, got) ~= 0
end

-- ------------------------------------------------------------------------------------------ the levers --
-- Where the Helldiver stands and faces (the UnitComponent record: rotation +720, position +736), what it carries.
function K.where()
    local e, network = K.avatar()
    local units = K.global('UNITS')
    local i = K.hashmap(units + 0x40, e) or refuse('the Helldiver has no unit record')
    local at = (ptr(units + 0x68) or refuse('no unit records')) + i * 776
    local raw = read(at + 720, 28) or refuse('the unit record is unreadable')
    local f = ffi.cast('const float *', ffi.cast('const char *', raw))
    local qx, qy, qz, qw = f[0], f[1], f[2], f[3]
    local x, y, z = K.position(e)
    local inv = K.inventory(e)
    local _, _, _, m = K.position(e)
    return { entity = e, network = network, x = x, y = y, z = z, raw = K.hex(read(m.position, 12)), in_hand = K.in_hand(e), facing_x = 2 * (qx * qy - qw * qz), facing_y = 1 - 2 * (qx * qx + qz * qz),
        unit_x = tonumber(f[4]), unit_y = tonumber(f[5]), unit_z = tonumber(f[6]),
        primary = inv.primary, secondary = inv.secondary, support = inv.support, backpack = inv.backpack, held = inv.held, active = inv.active }
end
-- The physics body that carries the Helldiver (an 0x80-byte motion record of the physics world: centre +0,
-- rotation +0x10, speed limits 200 and 100 at +0x4C and +0x5C). levers.py finds the candidates from outside
-- (/proc/<pid>/mem); this proves one against the Helldiver's own position before anything is written.
function K.body_check(at, e)
    local x, y, z = K.position(e)
    local bx, by, bz = K.vec3(at)
    if not bx then return nil, 'unreadable' end
    if math.abs(bx - x) > 0.02 or math.abs(by - y) > 0.02 or bz - z < 0.3 or bz - z > 1.8 then return nil, 'not at the Helldiver' end
    if K.f32(at + 0x4C) ~= 200 or K.f32(at + 0x5C) ~= 100 then return nil, 'not a body record (speed limits)' end
    local q = read(at + 0x10, 16); local f = ffi.cast('const float *', ffi.cast('const char *', q))
    local n = f[0] * f[0] + f[1] * f[1] + f[2] * f[2] + f[3] * f[3]
    if math.abs(n - 1) > 0.01 then return nil, 'not a body record (rotation)' end
    return bz - z
end
function K.teleport(candidates, tx, ty, tz)
    local e = K.avatar()
    local found, lift
    for _, at in ipairs(candidates) do
        local up = K.body_check(at, e)
        if up then if found then refuse('two physics bodies sit at the Helldiver: not moving either') end; found, lift = at, up end
    end
    if not found then refuse('no physics body proven to be the Helldiver\'s among ' .. #candidates .. ' candidates') end
    local x, y, z = K.position(e)
    for _, v in ipairs({ tx, ty, tz }) do if type(v) ~= 'number' or v ~= v or math.abs(v) > 20000 then refuse('a target coordinate is not a map position') end end
    K.write(found, K.f32le(tx, ty, tz + lift), string.format('teleport %.2f %.2f %.2f -> %.2f %.2f %.2f (body)', x, y, z, tx, ty, tz))
    return { from = { x, y, z }, to = { tx, ty, tz }, body = string.format('0x%X', found) }
end

-- An entity resource's loadout package (16 hex digits), or nil when it has none.
function K.package_of(resource)
    local rec = K.native('PACKAGE_OF', 'const uint8_t *(*)(uint64_t)')(K.id64(resource)[0])
    if rec == nil then return nil end
    local pkg = K.id64_hex(tonumber(ffi.cast('uintptr_t', rec)) + 8)
    return pkg ~= '0000000000000000' and pkg or nil
end
-- The game's own package references (PackageSystem: 16-byte rows {package, references}).
function K.package_refs(package)
    local sys = K.global('PACKAGES')
    local rows, cap = ptr(sys), u32(sys + 8) or 0
    if not rows or cap == 0 or cap > 65536 then return nil end
    local want = ffi.string(K.id64(package), 8)
    local raw = read(rows, math.min(cap * 16, 65536)); if not raw then return nil end
    for i = 0, #raw / 16 - 1 do
        if raw:sub(i * 16 + 1, i * 16 + 8) == want then return word(raw, i * 16 + 8) end
    end
    return 0
end
-- HellForge's checked package load: 'resident', 'loading' (ask again) or a refusal. A package HellForge does not
-- know is taken as resident only where the game itself already holds it for this mission (the loadout's own).
function K.load(package)
    local r = K.bridge('hellforge_lua_load_package', 'int (*)(void *, uint64_t)')(K.mod(), K.id64(package)[0])
    if r == 2 then return 'resident' end
    if r == 1 then return 'loading' end
    local why = K.last()
    local refs = K.package_refs(package)
    if refs and refs > 0 then say('package ' .. package .. ': not HellForge\'s to load, the game holds it (' .. refs .. ' references)'); return 'resident' end
    refuse('HellForge would not load package ' .. package .. ' and the game does not hold it: ' .. why)
end
function K.ready(resource)
    local pkg = K.package_of(resource) or refuse('entity ' .. resource .. ' has no loadout package: it cannot be made ready')
    return K.load(pkg), pkg
end
-- The game's entity spawn request. `mods`: the customization, up to 10 ids of 16 hex digits (nil: none).
-- `yaw`: radians about the up axis.
function K.spawn(resource, x, y, z, mods, yaw)
    K.need_mission(); K.need_solo()
    local raw = ffi.new('uint8_t[?]', 0x58 + 16)
    local at = raw + (16 - tonumber(ffi.cast('uintptr_t', raw)) % 16) % 16
    at[0] = 1
    ffi.cast('uint32_t *', at + 4)[0] = 0x7FFF
    local pose = ffi.cast('float *', at + 8)
    local c, sn = math.cos(yaw or 0), math.sin(yaw or 0)      -- turned about the up axis; 0 faces +y
    local m = { c, sn, 0, 0, -sn, c, 0, 0, 0, 0, 1, 0, x, y, z, 1 }
    for k = 1, 16 do pose[k - 1] = m[k] end
    local keep = { raw }
    if mods then
        local list = ffi.new('uint64_t[11]')
        ffi.cast('uint32_t *', list)[0] = #mods
        for i, id in ipairs(mods) do list[i] = K.id64(id)[0] end
        ffi.cast('uintptr_t *', at + 0x50)[0] = ffi.cast('uintptr_t', list)
        keep[2] = list
    end
    K.keep[#K.keep + 1] = keep                       -- the game may read the request after the call
    local out = ffi.new('uint32_t[1]')
    K.native('SPAWN', 'uint32_t *(*)(uint32_t *, uint64_t, void *)')(out, K.id64(resource)[0], at)
    local e = tonumber(out[0])
    if e == 0 or e == 0xffffffff then refuse('the game made no entity of ' .. resource) end
    say(string.format('spawned %s as entity %d at %.2f %.2f %.2f%s', resource, e, x, y, z, mods and (' with ' .. #mods .. ' attachments') or ''))
    return e
end
-- A weapon's default customization: the ids (16 hex digits) the game's own default builder names.
function K.defaults(resource)
    local defs = ffi.new('uint64_t[10]')
    K.native('DEFAULTS', 'void (*)(void *, uint64_t, void *)')(nil, K.id64(resource)[0], defs)
    local out = {}
    for i = 0, 9 do
        local a = tonumber(defs[i])
        if a ~= 0 then
            if a < 65536 or a >= 2 ^ 47 then refuse('a default attachment record is not a pointer') end
            local id = K.id64_hex(a + 0x20)
            if id and id ~= '0000000000000000' then out[#out + 1] = { slot = i, id = id, record = string.format('0x%X', a) } end
        end
    end
    return out
end
-- The customization items near a weapon's defaults: the game keeps them as records (name pointer +0, id +0x20)
-- in one table, the names in a string table (each name followed by its slot type, u32). Returns every record in
-- the table's stretch around the weapon's defaults whose name pointer lands on a readable name: {name, slot, id}.
function K.attachments(resource)
    local defs = K.defaults(resource)
    if #defs == 0 then refuse('weapon ' .. resource .. ' has no default customization') end
    local lo, hi, slo, shi = math.huge, 0, math.huge, 0
    for _, d in ipairs(defs) do
        local a = tonumber(d.record); lo = math.min(lo, a); hi = math.max(hi, a)
        local n = ptr(a) or refuse('a default attachment has no name'); slo = math.min(slo, n); shi = math.max(shi, n)
        d.name = (read(n, 48) or ''):match('^[%w%p ]+') or '?'
    end
    lo, hi, slo, shi = lo - 0x4000, hi + 0x4000, slo - 0x8000, shi + 0x8000
    local out, seen = {}, {}
    local at = lo - lo % 4
    while at < hi do
        local chunk = read(at, 0x4000)
        if chunk then
            for off = 0, #chunk - 0x58, 4 do
                local low, high = word(chunk, off), word(chunk, off + 4)
                local p = low + high * 4294967296
                if p >= slo and p < shi and word(chunk, off + 0x38) == 1 then
                    local text = read(p, 52)
                    local name = text and text:match('^([%w%p ]+)%z')
                    if name and #name >= 3 and #name <= 44 then
                        local pad = math.floor((#name + 4) / 4) * 4          -- the name, zero padded to 4 bytes
                        local slot = word(text, pad)
                        local id = K.hex(chunk:sub(off + 0x21, off + 0x28):reverse())
                        if slot <= 12 and id ~= '0000000000000000' and not seen[id] then
                            seen[id] = true; out[#out + 1] = { name = name, slot = slot, id = id }
                        end
                    end
                end
            end
        end
        at = at + 0x4000 - 0x58
    end
    return out, defs
end
-- A weapon's customization for a spawn: its defaults, with the named items put in their slots.
function K.customization(resource, wanted)
    local defs = K.defaults(resource)
    local mods, said = {}, {}
    local all
    for _, d in ipairs(defs) do mods[d.slot] = d.id end
    for _, name in ipairs(wanted or {}) do
        all = all or K.attachments(resource)
        local hit
        for _, a in ipairs(all) do if a.name:lower() == name:lower() then if hit and hit.id ~= a.id then refuse('two attachments are called ' .. name) end; hit = a end end
        if not hit then refuse('no attachment called "' .. name .. '" near the defaults of ' .. resource) end
        said[#said + 1] = name .. ' (slot ' .. hit.slot .. ', ' .. hit.id .. (mods[hit.slot] and ', in place of the default' or ', a slot with no default') .. ')'
        mods[hit.slot] = hit.id
    end
    local list = {}
    for slot = 0, 9 do if mods[slot] then list[#list + 1] = mods[slot] end end
    return list, said
end
-- A pick-up's zones (events manager): zone 1 is the ground pick-up. open: the game's own call.
function K.zone(entity, zone, open)
    local events = K.global('EVENTS')
    local i = K.hashmap(events + K.site('EVENT_INDEX'), entity)
    if not i then return nil, 'no pick-up record' end
    local rows = ptr(events + K.site('EVENT_ROWS')) or refuse('no pick-up rows')
    local offs = { [0] = 0x40, [1] = 0xA8 }
    if open ~= nil then
        K.native('ZONE_SET', 'void (*)(void *, uint32_t, uint32_t, uint8_t)')(ffi.cast('void *', events), entity, zone, open and 1 or 0)
        say(string.format('pick-up zone %d of entity %d %s', zone, entity, open and 'opened' or 'closed'))
    end
    local raw = read(rows + i * 0x370, 0x370); if not raw then return nil, 'unreadable' end
    local function z(at) local h = word(raw, at); if h >= 0x80000000 then h = h - 4294967296 end; return { handle = h, category = word(raw, at + 4) } end
    return { zone0 = z(0x40), zone1 = z(0xA8), settled = word(raw, 0x360) }
end
-- What the Helldiver holds in a hand (WielderManager: an instance's slot 0).
function K.in_hand(e)
    local w = K.global('WIELDER')
    local i = K.hashmap(w + 0x30, e); if not i then return nil end
    return u32((ptr(w + 0x60) or 0) + i * 0x1D0)
end
local SLOT_OFFSET = { [1] = 0, [2] = 4, [3] = 8, [6] = 12 }
-- The game's equip of a spawned entity into an inventory slot (1 primary, 2 secondary, 3 support, 6 backpack).
function K.equip(entity, slot)
    local e = K.avatar()
    local off = SLOT_OFFSET[slot] or refuse('no inventory slot ' .. tostring(slot))
    local d = K.descriptor(entity) or refuse('entity ' .. tostring(entity) .. ' is gone')
    if not d.owned then refuse('entity ' .. entity .. ' is not this machine\'s') end
    local inv = K.inventory(e)
    local old = word(inv.raw, off)
    K.native('EQUIP', 'void (*)(void *, uint32_t, uint32_t, uint32_t, uint8_t)')(ffi.cast('void *', inv.manager), e, entity, slot, 0)
    say(string.format('equipped entity %d (%s) into slot %d of the Helldiver %d; the slot held %d', entity, d.resource, slot, e, old))
    return old
end
function K.select(slot)
    local e = K.avatar()
    local inv = K.inventory(e)
    K.native('SELECT', 'void (*)(void *, uint32_t, uint32_t)')(ffi.cast('void *', inv.manager), e, slot)
    say('selected inventory slot ' .. slot)
end
-- The game's deferred removal of an entity the levers displaced (never the Helldiver, never something carried).
function K.remove(entity)
    local e = K.avatar()
    if entity == e then refuse('not removing the Helldiver') end
    local inv = K.inventory(e)
    for _, off in pairs(SLOT_OFFSET) do if word(inv.raw, off) == entity then refuse('entity ' .. entity .. ' is still carried') end end
    if K.in_hand(e) == entity then refuse('entity ' .. entity .. ' is still in the hand') end
    local d = K.descriptor(entity); if not d then return false end
    K.native('REMOVE', 'void (*)(void *, uint32_t)')(ffi.cast('void *', K.global('WORLD')), entity)
    say(string.format('removal of entity %d (%s) requested', entity, d.resource))
    return true
end

-- The player's weapon controller (in the avatar's simulation block: +0 the entity, +0xC the slot it wants): the
-- game puts the inventory selection back to what this says, so a selection goes through it.
function K.take(slot)
    local e = K.avatar()
    if slot ~= 1 and slot ~= 2 and slot ~= 3 then refuse('a hand slot is 1, 2 or 3') end
    local av = K.global('AVATARS')
    local i = K.hashmap(av + 0xF8, e) or refuse('the Helldiver has no avatar record')
    if i >= (u32(av + 0x70) or 0) then refuse('the Helldiver is not simulated on this machine') end
    local c = av + 0x53E170 - 0x4A8 + i * 0x1238
    local inv = K.inventory(e)
    if u32(c) ~= e or (u32(c + 0xC) ~= inv.active and u32(c + 0x14) ~= inv.active) then
        refuse(string.format('the weapon controller is not where it was found (entity %s, slot %s, inventory slot %d)', tostring(u32(c)), tostring(u32(c + 0x14)), inv.active))
    end
    if word(inv.raw, SLOT_OFFSET[slot]) == 0 then refuse('inventory slot ' .. slot .. ' is empty') end
    K.native('PLAYER_SELECT', 'void (*)(void *, uint32_t)')(ffi.cast('void *', c), slot)
    K.native('SELECT', 'void (*)(void *, uint32_t, uint32_t)')(ffi.cast('void *', inv.manager), e, slot)
    say(string.format('the weapon controller takes slot %d (was %d)', slot, inv.active))
end

-- The mission's equipped stratagems: PlayerHistory's record of this peer, its loadout +0x38, slots of 0x30 bytes
-- at +0x188 (the type at +0, the uses left at +4), their count at +0x788.
local function stratagem_list()
    K.need_mission()
    local peer = K.need_solo()
    local h = K.global('LOADOUTS')
    local n = u32(h + K.site('LOADOUT_COUNT')) or 0
    if n < 1 or n > 4 then refuse('the loadout records do not look right (' .. n .. ')') end
    for i = 0, n - 1 do
        local rec = h + i * 0x1690
        if read(rec, 8) == peer then
            local l = rec + 0x38
            local count = u32(l + 0x788) or 0
            if count < 1 or count > 32 then refuse('the stratagem count does not look right (' .. count .. ')') end
            return l + 0x188, count
        end
    end
    refuse('no loadout record of this peer')
end
function K.stratagems()
    local at, count = stratagem_list()
    local out = {}
    for s = 0, count - 1 do
        local raw = read(at + s * 0x30, 0x30) or refuse('a stratagem slot is unreadable')
        out[#out + 1] = { slot = s + 1, type = word(raw, 0), uses = word(raw, 4), raw = K.hex(raw) }
    end
    return out
end
-- A stratagem type's row, through HellForge's bridge: the package it loads, the item it loads, its traits.
function K.stratagem_row(kind)
    local v = ffi.new('uint8_t[64]')
    local get = K.bridge('hellforge_lua_get', 'int (*)(void *, const char *, uint64_t, uint32_t, uint32_t, void *)')
    if get(K.mod(), 'StratagemDefinition', kind, 0xA8, 6, v) == 0 then refuse('no stratagem type ' .. tostring(kind) .. ': ' .. K.last()) end
    local package = K.hex(ffi.string(v + 24, 8):reverse())
    get(K.mod(), 'StratagemDefinition', kind, 0xF8, 3, v)
    local item = tonumber(ffi.cast('double *', v + 8)[0])
    get(K.mod(), 'StratagemDefinition', kind, 0x104, 3, v)
    return { package = package ~= '0000000000000000' and package ~= 'ffffffffffffffff' and package or nil, item = item, traits = tonumber(ffi.cast('double *', v + 8)[0]) }
end
-- Adds a type to the equipped stratagems as a new slot after the last (uses unlimited), the way the Gadget Kit's
-- fifth slot does: the entry is a copy of slot 1 with its type and uses changed, then the count goes up by one.
-- Nothing already equipped is changed (a changed slot keeps the old one's cooldown, and the mission's own
-- Reinforce and Resupply sit in this list too). A type already equipped is left as it is.
function K.add_stratagem(kind)
    local at, count = stratagem_list()
    K.stratagem_row(kind)
    for s = 0, count - 1 do if u32(at + s * 0x30) == kind then return s + 1, false end end
    if count >= 12 then refuse('twelve stratagem slots are enough') end
    local cell = at + count * 0x30
    if read(cell, 0x30) ~= string.rep('\0', 0x30) then refuse('the slot after the last is not empty') end
    local model = read(at, 0x30)
    K.write(cell, K.le32(kind) .. K.le32(0xFFFFFFFF) .. model:sub(9), string.format('stratagem slot %d: type %d', count + 1, kind))
    K.write(at - 0x188 + 0x788, K.le32(count + 1), string.format('stratagem count %d -> %d', count, count + 1))
    return count + 1, true
end

-- The vehicles of this machine's world (VehicleManager's entity map): {entity, resource, x, y, z, facing_x, facing_y}.
function K.vehicles()
    local v = K.global('VEHICLES')
    local rows, cap, empty = ptr(v + 0x38), u32(v + 0x40) or 0, u32(v + 0x44)
    local out = {}
    if not rows or cap == 0 or cap > 4096 then return out end
    local raw = read(rows, cap * 8) or refuse('the vehicle map is unreadable')
    local units = K.global('UNITS'); local records = ptr(units + 0x68)
    for i = 0, cap - 1 do
        local e = word(raw, i * 8)
        if e ~= empty and e ~= 0xffffffff then
            local d = K.descriptor(e)
            local u = d and K.hashmap(units + 0x40, e)
            local q = u and records and read(records + u * 776 + 720, 28)
            if q then
                local f = ffi.cast('const float *', ffi.cast('const char *', q))
                out[#out + 1] = { entity = e, resource = d.resource, x = tonumber(f[4]), y = tonumber(f[5]), z = tonumber(f[6]),
                    facing_x = 2 * (f[0] * f[1] - f[3] * f[2]), facing_y = 1 - 2 * (f[0] * f[0] + f[2] * f[2]) }
            end
        end
    end
    return out
end
return 'kernel ' .. VERSION .. ' in'

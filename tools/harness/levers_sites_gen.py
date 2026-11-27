#!/usr/bin/env python3
"""Makes tools/harness/levers_sites.json: where the levers' game functions, globals and moved offsets are in
game.dll of build 25480438, each with the byte pattern that finds it (and that the kernel checks in the running
game before every use). Run with a Python that has capstone, against the saved image:

    python levers_sites_gen.py [IMAGE]      (default: native-work/evidence/game-pre1020.dll of the Linux HD2 workspace)

Three sources: the retail LLtM kit's patterns (hd2_addresses.lua, written to survive game updates; they find the same
things in this older build), HellForge's SDK headers (hellforge_functions.h / hellforge_globals.h name the RVAs of
this build), and the Goose Gadget Kit spawner's fingerprints (found again by similarity). A site is kept only when
its pattern matches the image exactly once.
"""
import json, os, re, sys
IMAGE = os.path.expanduser('~/Documents/ChatGPT/Linux HD2/native-work/evidence/game-pre1020.dll')
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'levers_sites.json')

# name, kind, pattern [, at, size, minus]: from the retail kit's hd2_addresses.lua
KIT = [
    ('PLAYER', 'rip', '48 8b 05 ?? ?? ?? ?? 48 8d 54 24 48 48 8b 88 e8', 3, 7, 0),
    ('INVENTORY', 'rip', '4c 8b 05 ?? ?? ?? ?? 33 db 8b cb 45 8b 48 30 45', 3, 7, 0),
    ('UNITS', 'rip', '4c 8b 15 ?? ?? ?? ?? 75 17 b8 ff ff ff ff 8b c8 48 69 c1 08', 3, 7, 0),
    ('WORLD', 'rip', '48 8b 0d ?? ?? ?? ?? 8b 50 08 e8 ?? ?? ?? ?? eb 28 48 8b cf e8 ?? ?? ??', 3, 7, 0),
    ('GAME_STATE', 'rip', '48 8b 0d ?? ?? ?? ?? ba 2d 00 00 00 48 81 c1 ?? ?? ?? ?? e8', 3, 7, 0),
    ('GAME_MODE', 'rip', '48 8b 05 ?? ?? ?? ?? 83 78 08 00 74 26 8b 40 40', 3, 7, 0),
    ('EVENTS', 'rip', '48 8b 3d ?? ?? ?? ?? 4c 89 7c 24 20 8b 9f ?? ??', 3, 7, 0),
    ('SPAWN', 'start', '4c 8b dc 49 89 5b 10 49 89 73 20 57 48 81 ec a0 00 00 00 48 8b 05 ?? ?? ?? ?? 48 33 c4 48 89 84 24 90 00 00 00 33 c0 49'),
    ('EQUIP', 'start', '40 53 55 41 55 48 83 ec 40 48 89 7c 24 68 41 8b'),
    ('DROP', 'start', '48 89 5c 24 18 48 89 6c 24 20 48 89 54 24 10 56 48 83 ec 30'),
    ('REMOVE', 'start', '48 89 5c 24 08 57 48 83 ec 30 3b 15 ?? ?? ?? ??'),
    ('ZONE_SET', 'start', '41 56 41 57 48 83 ec 28 3b 15 ?? ?? ?? ?? 45 0f b6 f1 45 8b f8 4c 8b d1'),
    ('WORLD_RECORDS', 'u32', '48 8d 85 ?? ?? ?? ?? 48 8d 04 c8 eb 07 48 8d 05', 3, 0, 8),
    ('STATE_VALUE', 'u32', '83 b8 ?? ?? ?? ?? 04 75 1f 44 38 a0 ?? ?? ?? ??', 2, 0, 0),
    ('LOADOUTS', 'rip', '4c 8b 15 ?? ?? ?? ?? 33 c0 4c 8b 8c 3e c8 02 00', 3, 7, 0),
    ('LOADOUT_COUNT', 'u32', '45 8b 82 ?? ?? ?? ?? 45 85 c0 74 32 49 8b ca 4c', 3, 0, 0),
    ('NET', 'rip', '48 8b 05 ?? ?? ?? ?? 44 8b 41 08 48 8b 98 a8 b3', 3, 7, 0),
    ('PROFILE', 'rip', '4c 8b 05 ?? ?? ?? ?? 8b 42 50 41 39 80 ?? ?? ??', 3, 7, 0),
    ('EVENT_INDEX', 'u32', '49 8b b3 ?? ?? ?? ?? 0f 1f 40 00 66 0f 1f 84 00', 3, 0, 0),
    ('EVENT_ROWS', 'u32', '48 03 8f ?? ?? ?? ?? 49 c1 e3 04 43 8b 1c 03 4b', 3, 0, 0),
    ('PACKAGES', 'rip', '48 8b 0d ?? ?? ?? ?? 48 8d 94 24 08 01 00 00 41', 3, 7, 0),
    ('ENGINE', 'rip', '48 8b 05 ?? ?? ?? ?? 48 8b 50 10 48 8b 82 10 03 00 00 ff d0 85 c0 74 36', 3, 7, 0),
    ('ENTITY_NONE', 'rip', '3b 3d ?? ?? ?? ?? 48 8b 05 ?? ?? ?? ?? 41 8b 5d 04 41 8b 75', 2, 6, 0),
]
# name -> RVA of a function in build 25480438 (the SDK's, or found by the Gadget Kit spawner's fingerprint)
FUNCTIONS = {
    'ENTITY_OF_NETWORK': 0xFD9BA0,   # sdk: EntityOfNetwork(out, network)
    'ENTITY_DESCRIPTOR': 0xFD9D40,   # sdk: EntityDescriptor(entity)
    'PACKAGE_OF': 0x4F5680,          # kit: the LoadoutPackage record of an entity resource (+8 its package)
    'DEFAULTS': 0x74B1B0,            # spawner: a weapon's default customization records (10 pointers)
    'SELECT': 0x9AAA50,              # spawner: select an inventory slot
    'SET_VELOCITY': 0x4A7550,        # sdk: SetVelocity(NULL, entity, velocity)
    'PLAYER_SELECT': 0xA93E90,       # spawner: the player's weapon controller takes a slot
}
# name -> RVA of a global (a pointer cell) in build 25480438, from the SDK's hellforge_globals.h
GLOBALS = {
    'MOTION': 0x3326558, 'WIELDER': 0x3326420, 'AVATARS': 0x3326D20, 'VEHICLES': 0x3326668,
    'HEALTH': 0x3326688, 'WIELDABLES': 0x3326730,
}

def regex(pattern):
    return re.compile(b''.join(b'.' if t == '??' else re.escape(bytes([int(t, 16)])) for t in pattern.split()), re.S)

def main():
    import capstone
    image = open(sys.argv[1] if len(sys.argv) > 1 else IMAGE, 'rb').read()
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64); md.detail = True
    sites = {}
    def keep(name, site):
        hits = [m.start() for m in regex(site['pattern']).finditer(image)]
        if len(hits) != 1: raise SystemExit('%s: %d matches' % (name, len(hits)))
        site['rva'] = hits[0]; site['module'] = 'game.dll'
        site['pattern'] = site['pattern'].replace(' ', '')
        sites[name] = site
    def masked(rva, least):
        """The instructions from rva covering at least `least` bytes, displacements and branch targets wildcarded."""
        out, size = [], 0
        for ins in md.disasm(image[rva:rva + 96], rva):
            b = ['%02x' % x for x in ins.bytes]
            if ins.disp_size == 4 and (ins.modrm & 0xC7) == 0x05:            # rip-relative
                for i in range(ins.disp_offset, ins.disp_offset + 4): b[i] = '??'
            if ins.mnemonic in ('call', 'jmp') and ins.bytes[0] in (0xE8, 0xE9):
                for i in range(1, 5): b[i] = '??'
            out += b; size += ins.size
            if size >= least: break
        return ' '.join(out)
    for row in KIT:
        site = {'kind': row[1], 'pattern': row[2]}
        if row[1] != 'start': site.update(at=row[3], size=row[4], minus=row[5])
        keep(row[0], site)
    for name, rva in FUNCTIONS.items():
        for least in (24, 32, 40, 56):
            p = masked(rva, least)
            if len(list(regex(p).finditer(image))) == 1: break
        keep(name, {'kind': 'start', 'pattern': p})
        if sites[name]['rva'] != rva: raise SystemExit(name + ': found elsewhere')
    for name, rva in GLOBALS.items():
        found = None
        for m in re.finditer(rb'[\x48\x4c]\x8b[\x05\x0d\x15\x1d\x35\x3d]', image[:0x2000000]):
            at = m.start()
            if at + 7 + int.from_bytes(image[at + 3:at + 7], 'little', signed=True) != rva: continue
            p = ' '.join('%02x' % x for x in image[at:at + 3]) + ' ?? ?? ?? ?? ' + masked(at + 7, 12)
            if len(list(regex(p).finditer(image))) == 1: found = p; break
        if not found: raise SystemExit(name + ': no unique reference')
        keep(name, {'kind': 'rip', 'pattern': found, 'at': 3, 'size': 7, 'minus': 0})
        r = sites[name]
        if r['rva'] + 7 + int.from_bytes(image[r['rva'] + 3:r['rva'] + 7], 'little', signed=True) != rva: raise SystemExit(name)
    json.dump({'build': '25480438', 'image': os.path.basename(IMAGE), 'sites': sites}, open(OUT, 'w'), indent=1, sort_keys=True)
    for name, s in sorted(sites.items()): print('%-18s %-5s game+0x%X' % (name, s['kind'], s['rva']))

if __name__ == '__main__':
    main()

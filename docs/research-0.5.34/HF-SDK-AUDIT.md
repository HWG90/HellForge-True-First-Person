# HellForge SDK16 audit for native True First Person

Evidence: the installed build25480438 SDK headers and documentation. **No game calls, live memory reads, hooks, settings edits, or installation changes were made by this audit.** This is complete declaration coverage of the named headers, not a claim that every engine API or every game function has been discovered.

## Result

`HfApi` has **93 members: 2 ABI metadata fields and 91 function pointers**. None is a dedicated gameplay first-person camera, mouse-look, viewmodel, animation, bone, shadow, DOF or vignette API. The named native-function wrappers cover **10** combat/spawn/motion functions; the typed global helpers cover **15** globals. The game’s Stingray SDK and native camera/rig code therefore remain a separate investigation, rather than a hidden complete first-person API that this header already provides.

`hf-sdk-inventory.json` contains every field and type spelling, source line, enum member, macro, inline wrapper and exported declaration from the seven installed public SDK headers, including generated component/catalogue declarations and the legacy BTS aliases.

| Inventory | Count |
|---|---:|
| HfApi_fields | 93 |
| HfApi_functions | 91 |
| primary_header_record_types | 51 |
| primary_header_complete_record_types | 50 |
| primary_header_record_fields | 428 |
| all_header_record_types | 131 |
| all_header_record_fields | 1923 |
| typedefs | 164 |
| enums | 1 |
| inline_and_exported_functions | 96 |
| macro_definitions | 2059 |
| typed_named_native_functions | 10 |
| typed_named_globals | 15 |

## What is useful to this mod

| Need | What the installed SDK provides | Limit / next step |
|---|---|---|
| Stable startup and shutdown | `on`, `on_game_thread`, `load_phase`, `unhook`, SDK16 unload contract | Game calls belong on the game thread. SESSION_END may run under Windows loader lock: save/flush only; no engine calls or waits. A DLL using its own raw dispatch/data leases must still restore them itself. |
| Native function discovery | `function`, typed `hf_fn_*`, `global`, `module_base`, `code_matches`, `read_code`, `find_pattern` | Reviewed named wrappers do not include FPS camera/input. Add verified mapping/module evidence rather than treating a string or RVA candidate as callable. `find_pattern/read` see current hooked bytes; `code_matches/read_code` see through HellForge hook chains. |
| Cooperative hooks | `hook`, `hook_ex`, `hook_function`, `unhook` | These are detour facilities, not SDK-pointer leases. An allowed SDK pointer hook must not silently become an inline patch. Always call the currently published `*original`: later mods can change the chain. |
| Player / weapon / mission lifecycle | `subscribe`, `game_state`, `HfPlayerEvent`, `HfWeaponEvent`, named PlayerManager, WielderManager, InventoryManager | Promising replacement for expensive repeated discovery. Events are queued and build-reviewed; live subscription/source availability and respawn/generation ordering still need verification. |
| Input | No mouse/key/raw-look API in HfApi | Use the game’s native input binding/controller path after separate evidence; global/raw memory access alone does not define its ABI, filtering order or sensitivity semantics. |
| Camera / viewmodel / bone animation | No dedicated HfApi entry; terminal browser camera state exists | `browse_ask(SHOWN/HIDDEN)` controls the ship terminal view, not a gameplay FPS mode. Do not repurpose it as a pin. Native rig/camera SDK investigation remains necessary. |
| Rendering and UI | No generic overlay/mesh/shadow/DOF/vignette draw API in HfApi | Goose Overlay Host is a separate interface. Stingray model/renderer functions are another interface. They must retain valid engine userdata handles and per-object ownership. |
| Authored weapon behavior | Component records, get_live/set_live, HfWeaponData, HfProjectileWeapon | These describe authored data, not automatically the live per-entity layout. Recoil/handling edits change gameplay; native firing/recoil should be preserved for this visual mode. `fire_loop_camera_shake` is explicitly unverified. |
| Effects/sounds/models and residency | `resource_hash`, `load_package`, content/data registration | Native packages can be held until resident; assets still need separate contracts. Loading is per client and bounded to 64 held packages with timeout. |
| Co-op authority / portability | `session`, `is_authority`, `entity_of_network`, messaging/sharing/sync APIs | First-person presentation should stay local. Gameplay actions or stat edits are not needed to simulate camera behavior. Authority belongs to entities/session, not merely “this is the local player.” |

## All HfApi members

The category column classifies all 91 functions, including unrelated areas. Signatures and complete comments are in JSON. A declaration proves the SDK contract is shipped, not that a live call is currently available.

| Field | Header line | Introduced SDK | Category |
|---|---:|---:|---|
| `version` | 1104 | 1 | ABI_metadata |
| `size` | 1105 | 1 | ABI_metadata |
| `log` | 1108 | 1 | identity_logging |
| `build_id` | 1111 | 1 | identity_logging |
| `mod_id` | 1112 | 1 | identity_logging |
| `mod_folder` | 1113 | 1 | identity_logging |
| `on` | 1116 | 1 | loader_and_game_lifecycle |
| `module_base` | 1119 | 1 | memory_code_and_symbols |
| `find_pattern` | 1124 | 1 | memory_code_and_symbols |
| `read` | 1128 | 1 | memory_code_and_symbols |
| `write` | 1129 | 1 | memory_code_and_symbols |
| `write_u32_if` | 1131 | 1 | memory_code_and_symbols |
| `hook` | 1140 | 1 | hook_lifecycle |
| `network_field_type` | 1144 | 1 | network_schema |
| `set_network_field_bits` | 1145 | 1 | network_schema |
| `component_record` | 1150 | 1 | component_and_stats_editing |
| `add_weapon` | 1157 | 2 | content_registration_and_catalogue |
| `weapon_entity` | 1159 | 2 | content_registration_and_catalogue |
| `add_projectile` | 1164 | 3 | content_registration_and_catalogue |
| `projectile_row` | 1167 | 3 | content_registration_and_catalogue |
| `add_damage` | 1170 | 3 | content_registration_and_catalogue |
| `damage_row` | 1173 | 3 | content_registration_and_catalogue |
| `resource_hash` | 1175 | 3 | content_registration_and_catalogue |
| `add_stratagem` | 1180 | 4 | content_registration_and_catalogue |
| `stratagem_row` | 1183 | 4 | content_registration_and_catalogue |
| `add_entity` | 1186 | 4 | content_registration_and_catalogue |
| `add_behaviour` | 1189 | 4 | content_registration_and_catalogue |
| `add_difficulty` | 1194 | 5 | content_registration_and_catalogue |
| `difficulty_row` | 1197 | 5 | content_registration_and_catalogue |
| `register_entity` | 1203 | 6 | content_registration_and_catalogue |
| `copy_field` | 1210 | 6 | component_and_stats_editing |
| `set_field` | 1211 | 6 | component_and_stats_editing |
| `set_tag` | 1212 | 6 | component_and_stats_editing |
| `mount` | 1213 | 6 | component_and_stats_editing |
| `record` | 1216 | 6 | component_and_stats_editing |
| `find` | 1221 | 6 | content_registration_and_catalogue |
| `edit_entity` | 1224 | 6 | component_and_stats_editing |
| `set_live` | 1234 | 6 | component_and_stats_editing |
| `get_live` | 1238 | 6 | component_and_stats_editing |
| `set_passives` | 1245 | 6 | armor_passives |
| `get_passives` | 1247 | 6 | armor_passives |
| `on_game_thread` | 1251 | 6 | loader_and_game_lifecycle |
| `load_package` | 1258 | 6 | package_residency |
| `last_log` | 1261 | 6 | identity_logging |
| `logs_folder` | 1262 | 6 | identity_logging |
| `get_stat` | 1265 | 6 | component_and_stats_editing |
| `session` | 1267 | 6 | co_op_session_authority |
| `subscribe` | 1274 | 7 | game_event_subscription |
| `game_state` | 1276 | 7 | loader_and_game_lifecycle |
| `content` | 1280 | 7 | content_registration_and_catalogue |
| `add_ability` | 1283 | 7 | content_registration_and_catalogue |
| `add_stratagem_category` | 1286 | 8 | content_registration_and_catalogue |
| `set_stat` | 1294 | 9 | component_and_stats_editing |
| `copy_row` | 1299 | 9 | component_and_stats_editing |
| `mods` | 1304 | 10 | mod_status_and_notices |
| `heal` | 1308 | 11 | gameplay_actions |
| `heal_limb` | 1310 | 11 | gameplay_actions |
| `injure` | 1313 | 11 | gameplay_actions |
| `limb_state` | 1315 | 11 | gameplay_actions |
| `apply_status` | 1317 | 11 | gameplay_actions |
| `add_velocity` | 1320 | 11 | gameplay_actions |
| `resupply_from_pack` | 1322 | 11 | gameplay_actions |
| `spawn_explosion` | 1325 | 11 | gameplay_actions |
| `spawn_projectile` | 1328 | 11 | gameplay_actions |
| `credit_to_player` | 1330 | 11 | gameplay_actions |
| `freeze_mission_timer` | 1332 | 11 | gameplay_actions |
| `spawn_pelican` | 1334 | 11 | gameplay_actions |
| `modify_shots` | 1337 | 11 | gameplay_actions |
| `homing` | 1338 | 11 | gameplay_actions |
| `stop_shots` | 1339 | 11 | gameplay_actions |
| `notice` | 1343 | 12 | mod_status_and_notices |
| `hook_ex` | 1354 | 13 | hook_lifecycle |
| `code_matches` | 1360 | 13 | memory_code_and_symbols |
| `read_code` | 1363 | 13 | memory_code_and_symbols |
| `sync_state` | 1367 | 14 | mod_synchronization |
| `sync_mod` | 1371 | 14 | mod_synchronization |
| `sync_answer` | 1375 | 14 | mod_synchronization |
| `stats_source` | 1379 | 15 | co_op_session_authority |
| `browse_state` | 1384 | 16 | lobby_browser_and_terminal_camera |
| `browse_lobby` | 1386 | 16 | lobby_browser_and_terminal_camera |
| `browse_ask` | 1394 | 16 | lobby_browser_and_terminal_camera |
| `function` | 1400 | 16 | memory_code_and_symbols |
| `hook_function` | 1405 | 16 | hook_lifecycle |
| `global` | 1410 | 16 | memory_code_and_symbols |
| `set_status` | 1418 | 16 | mod_status_and_notices |
| `is_authority` | 1423 | 16 | co_op_session_authority |
| `entity_of_network` | 1426 | 16 | co_op_session_authority |
| `send` | 1431 | 16 | co_op_mod_channel |
| `share_set` | 1437 | 16 | co_op_mod_channel |
| `share_get` | 1439 | 16 | co_op_mod_channel |
| `share_row` | 1441 | 16 | co_op_mod_channel |
| `load_phase` | 1444 | 16 | loader_and_game_lifecycle |
| `unhook` | 1447 | 16 | hook_lifecycle |

## Every primary-header record type and field

All field names are enumerated here; JSON preserves each field’s type and line. Opaque means the SDK intentionally exposes no layout.

- `HfMod` (line 43): opaque.
- `HfWeapon` (line 91): `size`, `base`, `unit`, `designation`, `name`, `description`, `own_components`, `projectile`, `sound_from`, `own_all`.
- `HfEntity` (line 139): `size`, `base`, `name`, `unit`, `own_components`, `behaviour`, `model_from`, `own_all`, `title`, `designation`, `description`.
- `HfBehaviour` (line 180): `size`, `base`, `hold_seconds`, `orbit_radius`, `orbit_height`, `face_target`, `burst_seconds`, `reserve_rounds`, `aim_height`.
- `HfDamage` (line 207): `size`, `base`, `name`, `scale`.
- `HfEditorEntity` (line 233): `entity`, `like`.
- `HfStratagemCategory` (line 242): `size`, `name`, `title`, `sort_order`.
- `HfStratagem` (line 250): `size`, `base`, `name`, `title`, `description`, `code`, `icon`, `cooldown`, `uses`, `payload`, `payload_count`, `package`, `loads_item`, `category`, `beam`, `flags`, `editor_like`, `editor_entities`, `selection_category`, `selection_order`.
- `HfProjectile` (line 303): `size`, `base`, `name`, `damage`, `visual_from`, `spawn_effect`, `flight_effect`.
- `HfFieldType` (line 322): `kind`, `bits`, `min`.
- `HfDifficulty` (line 339): `size`, `name`, `title`, `description`, `encounter_points`, `guardforce_points`, `spawner_cooldown`, `icon`, `map_scale`, `optional_objectives`, `major_objectives`, `outpost_scale`, `mission_minutes`, `flags`, `patrol_frequency`, `patrol_cap`, `mission_stratagem`.
- `HfField` (line 432): `component`, `layout`, `offset`, `size`, `type`, `line`.
- `HfValue` (line 448): `type`, `number`, `u64`.
- `HfPassives` (line 464): `armor`, `second`, `kit_armor`, `kit_second`, `want_armor`, `want_second`, `players`, `held`, `status`.
- `HfLiveValue` (line 476): `now`, `base`, `mod`.
- `HfStat` (line 486): `kind`, `component`, `resource`, `record`, `offset`, `type`.
- `HfSession` (line 501): `role`, `version`, `host`.
- `HfNotice` (line 522): `version`, `kind`, `title`, `text`.
- `HfAbility` (line 592): `size`, `base`, `hold_seconds`, `start_event`, `second_event`, `final_event`, `package`, `flags`.
- `HfContent` (line 606): `size`, `kind`, `id`, `base`, `link`, `mod`, `folder`, `name`, `title`, `icon`, `editor_like`, `editor_entities`, `designation`, `category`, `entity`, `base_entity`.
- `HfEventInfo` (line 629): `size`, `kind`, `update`.
- `HfStateEvent` (line 637): `info`, `from`, `to`.
- `HfPlayerEvent` (line 638): `info`, `peer`, `entity`, `local`.
- `HfDamageEvent` (line 646): `info`, `target`, `kind`, `element`, `dealer`, `owner`, `amount`, `creditor`, `position`.
- `HfStratagemEvent` (line 659): `info`, `type`, `thrower`, `position`.
- `HfKillEvent` (line 666): `info`, `victim`, `dealer`, `creditor`, `kind`, `element`.
- `HfSpawnEvent` (line 673): `info`, `entity`, `network`, `resource`, `authority`.
- `HfHealEvent` (line 684): `info`, `entity`, `amount`, `health`, `max_health`, `peer`, `local`.
- `HfStatSource` (line 702): `resource`, `added`, `reserved`.
- `HfStatEvent` (line 703): `info`, `stat`, `added`, `total`, `unattributed`, `player`, `count`, `sources`.
- `HfWeaponEvent` (line 716): `info`, `avatar`, `previous_avatar`, `entity`, `previous`, `resource`, `previous_resource`, `selection`, `previous_selection`.
- `HfExplosionEvent` (line 726): `info`, `type`, `source`, `owner`, `reserved`, `creditor`, `position`.
- `HfBugholeEvent` (line 737): `info`, `entity`, `unit`, `resource`, `stage`, `owned`, `position`, `has_position`.
- `HfDamagePreEvent` (line 751): `damage`, `cancel`.
- `HfMessageEvent` (line 768): `info`, `from`, `bytes`, `data`.
- `HfShareEvent` (line 777): `info`, `key`, `bytes`, `value`.
- `HfLimbState` (line 800): `health`, `max_health`, `injured`.
- `HfShotRule` (line 820): `size`, `projectile`, `sources`, `damage`, `armor_penetration`, `speed`, `gravity`, `drag`, `flags`.
- `HfHoming` (line 830): `size`, `projectile`, `sources`, `turn_rate`, `cone`, `range`, `arm_distance`, `aim_height`, `flags`.
- `HfPelicanCall` (line 844): `size`, `position`, `hover_seconds`, `approach_distance`, `approach_height`, `facing`.
- `HfModInfo` (line 858): `size`, `kind`, `id`, `folder`, `name`, `loaded`.
- `HfSyncState` (line 880): `size`, `state`, `version`, `mods`, `bytes`, `done`, `host`, `message`, `flags`.
- `HfSyncMod` (line 915): `size`, `action`, `kind`, `flags`, `bytes`, `id`, `name`, `version`, `url`.
- `HfBrowseState` (line 949): `size`, `state`, `version`, `lobbies`, `terminal`, `message`, `camera`, `owner`.
- `HfBrowseLobby` (line 981): `size`, `verdict`, `flags`, `members`, `max_members`, `difficulty`, `planet`, `mods`, `download_bytes`, `lobby_id`, `host`, `reason`, `players`, `mod_list`, `fix`.
- `HfEntityDef` (line 1064): `size`, `name`, `base`, `model_from`, `unit`, `behaviour`, `behaviour_id`, `copy`, `set`, `add_tags`, `remove_tags`, `mount`, `own_all`, `title`, `designation`, `description`.
- `HfCopy` (line 1031): `from`, `field`.
- `HfSet` (line 1036): `field`, `value`.
- `HfMountDef` (line 1048): `slot`, `entity`, `resource`.
- `HfEditDef` (line 1091): `size`, `entity`, `copy`, `set`, `add_tags`, `remove_tags`, `mount`.
- `HfApi` (line 1102): `version`, `size`, `log`, `build_id`, `mod_id`, `mod_folder`, `on`, `module_base`, `find_pattern`, `read`, `write`, `write_u32_if`, `hook`, `network_field_type`, `set_network_field_bits`, `component_record`, `add_weapon`, `weapon_entity`, `add_projectile`, `projectile_row`, `add_damage`, `damage_row`, `resource_hash`, `add_stratagem`, `stratagem_row`, `add_entity`, `add_behaviour`, `add_difficulty`, `difficulty_row`, `register_entity`, `copy_field`, `set_field`, `set_tag`, `mount`, `record`, `find`, `edit_entity`, `set_live`, `get_live`, `set_passives`, `get_passives`, `on_game_thread`, `load_package`, `last_log`, `logs_folder`, `get_stat`, `session`, `subscribe`, `game_state`, `content`, `add_ability`, `add_stratagem_category`, `set_stat`, `copy_row`, `mods`, `heal`, `heal_limb`, `injure`, `limb_state`, `apply_status`, `add_velocity`, `resupply_from_pack`, `spawn_explosion`, `spawn_projectile`, `credit_to_player`, `freeze_mission_timer`, `spawn_pelican`, `modify_shots`, `homing`, `stop_shots`, `notice`, `hook_ex`, `code_matches`, `read_code`, `sync_state`, `sync_mod`, `sync_answer`, `stats_source`, `browse_state`, `browse_lobby`, `browse_ask`, `function`, `hook_function`, `global`, `set_status`, `is_authority`, `entity_of_network`, `send`, `share_set`, `share_get`, `share_row`, `load_phase`, `unhook`.

## Included public surfaces

| Header | Investigated surface |
|---|---|
| hellforge_components.h | All 62 component/settings record types and named/unknown fields, field type macros, source offsets, verification/apply-time annotations; no direct FPS lens/input/animation mode setter. |
| hellforge_content.h | All generated catalogue constants (entities, items, projectiles, stratagems, damage, etc.) captured as macros. These are content identities, not callable APIs. |
| hellforge_functions.h | All 10 signatures, getters, permitted hook wrappers and their reviewed-function comments. |
| hellforge_globals.h | All 15 typed global getters and known/opaque layouts. Includes player, local user, game state, motion, wield and inventory managers. |
| hellforge_lua.h | All loader C exports for the Lua bridge, HfLuaInfo/HfLuaValue/HfLuaEvent. This is a Lua-to-HellForge bridge, not a generic C-to-Stingray camera API. A C-only mod need not depend on Lua mod scripts to call existing engine bindings via the Lua C API. |
| bts_mod.h | Legacy BTS typedef/macro/init aliases; no additional game capabilities. |

### Reviewed native function names

`ConsumeDamageEvent`, `KillDispatch`, `ActivateStratagemBeacon`, `SpawnEntity`, `RequestExplosion`, `AddHealthFraction`, `RestoreZone`, `QueueStatusRequest`, `SetVelocity`, `FireProjectile`.

### Reviewed global helper names

`Game`, `PlayerManager`, `LocalUser`, `GameMode`, `EntityManager`, `HealthManager`, `MotionManager`, `ExplosionQueue`, `ProjectileSystem`, `CombatQueues`, `ScoreManager`, `WielderManager`, `InventoryManager`, `Destructibles`, `DestructionRecords`.

## Constraints that matter for a native-feeling implementation

- `on_game_thread` runs **before** the game update. It is a lifecycle/control point; it does not by itself establish the final post-animation camera pose. Its raw API returns 1 on success, while the convenience `hf_on_game_thread` wrapper returns 0 on success. Header line1251; runtime.md lines173–196.
- Native pointers can retire across mission/respawn. Named globals must be requested afresh and used as pointers, not giant struct copies. hooks.md lines134–169. The SDK’s witnessed global resolution improves address discovery but does not replace instance/generation proofs.
- `write` can change code/data and automatically changes protection. That capability does not authorize using an inline patch where the user requires a pointer hook. A pure presentation mode should prefer native arguments/calls and generation-owned conditional restoration.
- Hook callback threading is inherited from the caller, and shared chains can change. `*original` must be read when calling it. Header lines1133–1140,1346–1354; hooks.md lines273–324.
- Load events, live changes and game-thread updates can overlap. Raw C state needs explicit synchronization. SDK16 SESSION_END may occur with other threads already gone and loader lock held. Header lines59–64; concepts.md lines89–102.
- `set_live` and `set_stat` preserve conflict/ownership layers, but do not control arbitrary Unit instance transforms. Per-entity runtime camera/rig offsets cannot be assumed to be authored component fields.
- The lobby browser’s camera states are an existing native camera owner to respect during first-person suspension, not a generally reusable FPS camera controller. Header lines963–968 and1388–1394.

## Coverage and remaining unknowns

Complete: all declarations and macros in the exact seven installed headers; all 91 HfApi functions classified; main-header types/fields enumerated; generated component layouts and content constants inventoried. The compiler ran syntax-only on local headers; no target executed.

Partially answered: native presentation can use lifecycle events, named globals and reviewed byte reads to reduce repeated private discovery, but the exact live availability/order/performance still needs a game-thread read-only probe and actual gameplay comparison.

Unresolved by this audit: the complete Stingray engine SDK binding catalogue; unmapped native FPS camera/rig/input functions and data; correct native event order for ADS/weapon swaps/reload/vehicle transitions; live rendering/shadow property semantics; whether a separate viewmodel projection path exists. Those require the separate engine/binary/live investigation. The absence of a camera function in HfApi is a bounded header finding, not proof the game lacks one.

## Source evidence

All source paths and SHA256s are recorded in JSON. Key references:

- entire HfApi: [hellforge_mod.h](/home/goose/.local/share/hellforge-prefix/pfx/drive_c/users/steamuser/Documents/HellForge/app/sdk/hellforge_mod.h:1102), lines 1102–1448.
- game events and states: [hellforge_mod.h](/home/goose/.local/share/hellforge-prefix/pfx/drive_c/users/steamuser/Documents/HellForge/app/sdk/hellforge_mod.h:543), lines 543–570.
- weapon change event: [hellforge_mod.h](/home/goose/.local/share/hellforge-prefix/pfx/drive_c/users/steamuser/Documents/HellForge/app/sdk/hellforge_mod.h:716), lines 716–724.
- terminal camera state: [hellforge_mod.h](/home/goose/.local/share/hellforge-prefix/pfx/drive_c/users/steamuser/Documents/HellForge/app/sdk/hellforge_mod.h:963), lines 963–968.
- authored weapon handling versus live layout: [hellforge_components.h](/home/goose/.local/share/hellforge-prefix/pfx/drive_c/users/steamuser/Documents/HellForge/app/sdk/hellforge_components.h:177), lines 177–213.
- unverified firing camera shake: [hellforge_components.h](/home/goose/.local/share/hellforge-prefix/pfx/drive_c/users/steamuser/Documents/HellForge/app/sdk/hellforge_components.h:68), lines 68–69.
- all typed reviewed native functions: [hellforge_functions.h](/home/goose/.local/share/hellforge-prefix/pfx/drive_c/users/steamuser/Documents/HellForge/app/sdk/hellforge_functions.h:35), lines 35–184.
- all typed named global objects: [hellforge_globals.h](/home/goose/.local/share/hellforge-prefix/pfx/drive_c/users/steamuser/Documents/HellForge/app/sdk/hellforge_globals.h:27), lines 27–203.
- named function/global rules: [docs/modding/hooks.md](/home/goose/.local/share/hellforge-prefix/pfx/drive_c/users/steamuser/Documents/HellForge/app/sdk/docs/modding/hooks.md:85), lines 85–171.
- game-thread timing: [docs/modding/runtime.md](/home/goose/.local/share/hellforge-prefix/pfx/drive_c/users/steamuser/Documents/HellForge/app/sdk/docs/modding/runtime.md:173), lines 173–196.
- live unload ownership: [docs/modding/live.md](/home/goose/.local/share/hellforge-prefix/pfx/drive_c/users/steamuser/Documents/HellForge/app/sdk/docs/modding/live.md:33), lines 33–61.
- threading: [docs/modding/concepts.md](/home/goose/.local/share/hellforge-prefix/pfx/drive_c/users/steamuser/Documents/HellForge/app/sdk/docs/modding/concepts.md:89), lines 89–102.

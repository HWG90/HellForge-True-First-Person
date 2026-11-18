# Engine SDK inventory for native True First Person

Build **25480438**. This pass reads saved files only: **no process attachment, instrumentation, gameplay calls or installed-file changes**. The only created files are this report and `engine-sdk-inventory.json`.

## What is established

The original-build captured executable contains **1,223 unique function paths in 101 namespaces**, supported by **1846 explicit Lua function-registration sites**. Each JSON entry retains the namespace/member, registration RVA, callback RVA and exact byte evidence. A prior later-build live census contributes **1,874 unique names across 117 namespaces** overall; all 1,223 original-build registered names occur there. **651 names are historical-only in this extraction** and must not acquire current-build call authority from their names.

The extractor requires the exact root `stingray`, the namespace helper at EXE+0x1c4490, and the observed Lua pushstring → pushcclosure → rawset sequence. It excludes constant registration and rejects other templates instead of guessing their callback. It covers 2256 root-reference sites, accepts 1846, and leaves 410 as other/rejected forms. Dynamic device instances, generic component methods, global constructors, lazy registrations, other templates and private game APIs remain outside that proof. **This is not a complete behavior audit of the SDK.**

## Immediate implications for the first-person design

- **Camera.set_mode is a no-op on this build.** Its binding at EXE+0x41f3b0 is exactly `33 c0 c3` (`xor eax,eax; ret`). It cannot switch the gameplay controller to first person. There are 23 registered paths with literal immediate-return entrypoints; the JSON lists them. A supported-looking SDK name does not establish a functional feature.
- Camera exposes real transform, FOV, near/far, frustum, projection and post-projection operations. `set_post_projection_transform` at EXE+0x41e630 reads two Lua arguments and calls EXE+0x30da00; `set_projection_type` at EXE+0x41e3a0 writes camera+0x30 and calls EXE+0x4cfa10. These are per-camera operations. **Neither is evidence for an independent weapon-only projection layer.**
- Unit and component APIs expose native animation state, variables, constraints, crossfades, root/bone modes, merge options, graph nodes and parent transforms. These offer a concrete investigation route before manually retargeting or pinning individual bones. Their names alone do not establish valid arguments or compatibility with the FPS graph.
- Native input surfaces include event queues and device axis/button APIs. Mouse/pad instances are dynamically constructed and absent from this narrow static template; their later-build census cannot be treated as current proof. Native zoom compensation requires exact input-path and lifetime qualification rather than an independent replacement look integrator.
- Application/Viewport/Renderer expose render-world, viewport, render-target and resource operations; Mesh/World expose shader-pass flags. A separate viewmodel render pass is **unproven**. The next useful question is render ownership and pass membership, not an unguarded call to a projection setter.
- The census has no public `set_first_person` gameplay-controller function. That is a bounded name-inventory result, not proof there is no private native function. Vehicle/action/death/ADS routing still belongs to proprietary game.dll camera state and native rig behavior.

## Complete namespace surface ledger for these sources

Every discovered namespace is included below, including those with no first-person role. “Target” counts exact original-build registration matches. “History” counts named paths in the prior later-build census, including lookups reported as errors. Full member lists and each exact target registration are in the JSON. All groups remain signature/lifecycle/behavior-unqualified unless the finding above says otherwise.

| Namespace | Target functions | Historical names | Domain |
|---|---:|---:|---|
| `AI` | 1 | 1 | spatial math queries |
| `Achievement` | 4 | 4 | network platform persistence |
| `AnimationBlenderComponent` | 10 | 15 | animation rig transforms |
| `AnimationCurves` | 1 | 1 | animation rig transforms |
| `AnimationStateMachineComponent` | 8 | 13 | animation rig transforms |
| `Application` | 57 | 63 | world entities resources |
| `BakedLighting` | 5 | 5 | rendering geometry lighting |
| `Broadphase` | 7 | 7 | spatial math queries |
| `Camera` | 31 | 31 | camera projection |
| `Cloud` | 7 | 7 | network platform persistence |
| `CloudToken` | 1 | 1 | network platform persistence |
| `CommandWindow` | 5 | 5 | gui debug diagnostics |
| `Console` | 0 | 3 | gui debug diagnostics |
| `DataComponent` | 3 | 8 | world entities resources |
| `DebugNameComponent` | 0 | 5 | gui debug diagnostics |
| `DisplayAdapter` | 5 | 5 | rendering geometry lighting |
| `DynamicData` | 2 | 2 | world entities resources |
| `EntityManager` | 19 | 19 | world entities resources |
| `Float32Array` | 21 | 21 | spatial math queries |
| `FlowComponent` | 3 | 8 | world entities resources |
| `Friends` | 10 | 10 | network platform persistence |
| `GameSession` | 31 | 31 | network platform persistence |
| `Gui` | 44 | 44 | gui debug diagnostics |
| `Http` | 5 | 6 | network platform persistence |
| `INT` | 1 | 1 | script types identifiers |
| `IdString32` | 4 | 4 | script types identifiers |
| `IdString32Box` | 2 | 2 | script types identifiers |
| `IdString64` | 4 | 4 | script types identifiers |
| `IdString64Box` | 2 | 2 | script types identifiers |
| `InfluenceMap` | 4 | 4 | spatial math queries |
| `Input` | 1 | 1 | input window |
| `Keyboard` | 1 | 38 | input window |
| `LODObject` | 15 | 15 | rendering geometry lighting |
| `LanClient` | 1 | 1 | network platform persistence |
| `LanLobby` | 11 | 11 | network platform persistence |
| `LanLobbyBrowser` | 4 | 4 | network platform persistence |
| `Leaderboard` | 10 | 10 | network platform persistence |
| `LeaderboardTransactionToken` | 1 | 1 | network platform persistence |
| `Level` | 29 | 30 | world entities resources |
| `LevelResource` | 7 | 7 | world entities resources |
| `Light` | 48 | 48 | rendering geometry lighting |
| `LightUserData` | 7 | 7 | script types identifiers |
| `LineObject` | 10 | 10 | gui debug diagnostics |
| `Localizer` | 2 | 2 | gui debug diagnostics |
| `Material` | 16 | 17 | rendering geometry lighting |
| `Math` | 8 | 8 | spatial math queries |
| `Matrix4x4` | 40 | 40 | spatial math queries |
| `Matrix4x4Box` | 2 | 2 | spatial math queries |
| `Mesh` | 22 | 22 | rendering geometry lighting |
| `MeshComponent` | 2 | 7 | rendering geometry lighting |
| `Mouse` | 0 | 37 | input window |
| `Network` | 36 | 36 | network platform persistence |
| `Pad1` | 0 | 37 | input window |
| `Pad2` | 0 | 37 | input window |
| `Pad3` | 0 | 37 | input window |
| `Pad4` | 0 | 37 | input window |
| `Pad5` | 0 | 37 | input window |
| `Pad6` | 0 | 37 | input window |
| `Pad7` | 0 | 37 | input window |
| `Pad8` | 0 | 37 | input window |
| `PluginManager` | 6 | 6 | world entities resources |
| `Prefab` | 6 | 6 | world entities resources |
| `Presence` | 4 | 4 | network platform persistence |
| `ProceduralMesh` | 12 | 12 | rendering geometry lighting |
| `Profiler` | 4 | 4 | gui debug diagnostics |
| `Quaternion` | 29 | 29 | spatial math queries |
| `QuaternionBox` | 2 | 2 | spatial math queries |
| `RefTracker` | 2 | 2 | gui debug diagnostics |
| `RenderBuffer` | 12 | 12 | rendering geometry lighting |
| `RenderDataComponent` | 3 | 8 | rendering geometry lighting |
| `Renderer` | 8 | 9 | rendering geometry lighting |
| `ResourcePackage` | 4 | 4 | world entities resources |
| `STRING` | 1 | 1 | script types identifiers |
| `SaveSystem` | 5 | 5 | network platform persistence |
| `ScatterSystem` | 11 | 11 | rendering geometry lighting |
| `SceneGraphComponent` | 14 | 19 | animation rig transforms |
| `Script` | 14 | 16 | script types identifiers |
| `ScriptComponent` | 5 | 10 | world entities resources |
| `Sensor` | 27 | 27 | spatial math queries |
| `ShadingEnvironment` | 18 | 18 | rendering geometry lighting |
| `ShadingEnvironmentBlendComponent` | 4 | 9 | rendering geometry lighting |
| `SimulatedTouchPanel` | 0 | 41 | input window |
| `SimulatedTouchPanel1` | 0 | 41 | input window |
| `Speedtree` | 18 | 18 | rendering geometry lighting |
| `Stats` | 5 | 5 | network platform persistence |
| `Steam` | 11 | 11 | network platform persistence |
| `SteamClient` | 1 | 1 | network platform persistence |
| `SteamLobby` | 10 | 10 | network platform persistence |
| `SteamLobbyBrowser` | 10 | 10 | network platform persistence |
| `SteamVoip` | 4 | 5 | network platform persistence |
| `SteamVoipClient` | 5 | 5 | network platform persistence |
| `SteamVoipRoom` | 4 | 4 | network platform persistence |
| `Tablet` | 0 | 37 | input window |
| `TagComponent` | 10 | 10 | world entities resources |
| `Terrain` | 7 | 7 | spatial math queries |
| `TerrainOffset` | 2 | 2 | spatial math queries |
| `TouchPanel` | 0 | 40 | input window |
| `TouchPanel1` | 0 | 40 | input window |
| `TransformComponent` | 16 | 21 | animation rig transforms |
| `UINT` | 1 | 1 | script types identifiers |
| `Unit` | 115 | 117 | animation rig transforms |
| `UnitComponent` | 1 | 6 | animation rig transforms |
| `UnitResource` | 2 | 2 | animation rig transforms |
| `UnitSynchronizer` | 5 | 5 | network platform persistence |
| `Utf8` | 4 | 4 | script types identifiers |
| `Vector2` | 26 | 26 | spatial math queries |
| `Vector2Box` | 5 | 5 | spatial math queries |
| `Vector3` | 39 | 42 | spatial math queries |
| `Vector3Box` | 5 | 5 | spatial math queries |
| `Vector4` | 30 | 30 | spatial math queries |
| `Vector4Box` | 5 | 5 | spatial math queries |
| `VectorField` | 4 | 4 | spatial math queries |
| `VideoPlayer` | 8 | 8 | rendering geometry lighting |
| `Viewport` | 6 | 6 | camera projection |
| `WebSocket` | 5 | 5 | network platform persistence |
| `Window` | 29 | 29 | input window |
| `World` | 69 | 70 | world entities resources |

## First-person relevant target member lists

### Camera

`far_range`, `get_data`, `has_data`, `inside_frustum`, `local_pose`, `local_position`, `local_rotation`, `mode`, `near_range`, `node`, `projection_type`, `screen_to_world`, `set_data`, `set_far_range`, `set_frustum`, `set_frustum_half_angles`, `set_local`, `set_local_pose`, `set_local_position`, `set_local_rotation`, `set_mode`, `set_near_range`, `set_orthographic_view`, `set_post_projection_transform`, `set_projection_type`, `set_vertical_fov`, `vertical_fov`, `world_pose`, `world_position`, `world_rotation`, `world_to_screen`.

### Viewport

`get_data`, `has_data`, `register_render_resource`, `set_data`, `set_output_render_target`, `set_rect`.

### Application

`apply_user_settings`, `argv`, `back_buffer_size`, `build`, `build_identifier`, `can_get`, `clear_resource_override`, `clear_resource_override_flag`, `create_render_target`, `create_viewport`, `destroy_viewport`, `export_mesh_geometry`, `flow_callback_context_entity`, `flow_callback_context_level`, `flow_callback_context_unit`, `flow_callback_context_world`, `get_animation`, `get_data`, `has_data`, `main_world`, `make_hash`, `new_world`, `open_url_in_browser`, `performance_hud_command`, `platform`, `process_id`, `quit`, `refresh_shader_templates`, `release_world`, `render_caps`, `render_config`, `render_setting`, `render_world`, `resolution`, `restart_file_log`, `save_render_target`, `save_user_settings`, `scan_for_windows_ps4_controllers`, `session_id`, `set_background_render`, `set_data`, `set_max_frame_stacking`, `set_render_setting`, `set_resource_override`, `set_resource_override_flag`, `set_time_step_policy`, `set_user_setting`, `settings`, `string_to_id32`, `string_to_id64`, `sysinfo`, `time_since_launch`, `update_raw_buffer`, `update_render_world`, `user_setting`, `user_settings_load_error`, `worlds`.

### World

`__eq`, `animation_event_stream_get`, `are_particles_playing`, `clear_permanent_lines`, `create_line_object`, `create_particles`, `create_screen_gui`, `create_shading_environment`, `create_video_player`, `create_world_gui`, `debug_camera_pose`, `delta_time`, `destroy_all_particles`, `destroy_gui`, `destroy_level`, `destroy_line_object`, `destroy_particles`, `destroy_prefab`, `destroy_shading_environment`, `destroy_unit`, `destroy_video_player`, `dismember_unit`, `entities`, `find_particles_variable`, `get_data`, `has_data`, `is_camera_shake_playing`, `levels`, `link_camera_shake`, `link_particles`, `link_unit`, `load_level`, `load_prefab`, `mend_unit`, `move_camera_shake`, `move_particles`, `num_levels`, `num_units`, `play_camera_local_shake`, `play_camera_shake`, `process_async_operations`, `replay`, `scatter_system`, `set_camera_shake_properties`, `set_data`, `set_flow_callback_object`, `set_particle_observer`, `set_particles_variable`, `set_particles_visible`, `set_prefab_seed`, `set_shader_pass_flag`, `set_shading_environment`, `spawn_unit`, `stop_camera_shake`, `stop_spawning_particles`, `time`, `trigger_particle_emit`, `unit_by_id`, `unit_by_name`, `units`, `units_by_resource`, `unlink_unit`, `update`, `update_animations`, `update_animations_with_callback`, `update_scene`, `update_unit`, `update_with_callback`, `vector_field`.

### Unit

`add_new_trigger`, `alive`, `animation_bone_mode`, `animation_event`, `animation_find_constraint_target`, `animation_find_variable`, `animation_get_constraint_target`, `animation_get_curve_value`, `animation_get_seeds`, `animation_get_state`, `animation_get_variable`, `animation_has_constraint_target`, `animation_has_variable`, `animation_layer_info`, `animation_root_mode`, `animation_set_constraint_target`, `animation_set_seeds`, `animation_set_state`, `animation_set_variable`, `animation_wanted_root_pose`, `apply_initial_actor_velocities`, `bones`, `box`, `camera`, `copy_scene_graph_local_from`, `crossfade_animation`, `crossfade_animation_set_speed`, `crossfade_animation_set_time`, `debug_draw_all_constraint_bones`, `debug_draw_all_jiggle_bones`, `debug_draw_jiggle_bone`, `debug_name`, `delta_pose`, `delta_position`, `delta_rotation`, `disable_animation_state_machine`, `disable_physics`, `enable_animation_state_machine`, `enable_physics`, `flow_event`, `flow_variable`, `get_data`, `get_property`, `has_animation_blender`, `has_animation_event`, `has_animation_state_machine`, `has_camera`, `has_data`, `has_light`, `has_lod_object`, `has_mesh`, `has_node`, `has_terrain`, `has_visibility_group`, `id`, `is_a`, `is_child`, `is_crossfading_animation`, `level`, `light`, `local_pose`, `local_position`, `local_rotation`, `local_scale`, `lod_object`, `material_id`, `mesh`, `name_hash`, `node`, `num_cameras`, `num_lights`, `num_lod_objects`, `num_meshes`, `num_scene_graph_items`, `num_terrains`, `play_simple_animation`, `project_skinned_decal`, `query_material`, `remove_trigger`, `resource_name`, `scene_graph_link`, `scene_graph_parent`, `set_animation_bone_mode`, `set_animation_merge_options`, `set_animation_root_mode`, `set_animation_state_machine`, `set_bones_lod`, `set_data`, `set_flow_variable`, `set_ik_debug_drawing_enabled`, `set_light_material`, `set_local_pose`, `set_local_position`, `set_local_rotation`, `set_local_scale`, `set_material`, `set_mesh_visibility`, `set_moving`, `set_property`, `set_unit_visibility`, `set_visibility`, `stop_simple_animation`, `teleport_local_pose`, `teleport_local_position`, `teleport_local_rotation`, `teleport_local_scale`, `terrain`, `terrain_update_height_field`, `update_jiggle_bone_data`, `update_jiggle_bone_orientation`, `update_trigger_time`, `world`, `world_pose`, `world_position`, `world_rotation`.

### AnimationBlenderComponent

`bone_mode`, `crossfade`, `delta_transform`, `is_crossfading`, `root_mode`, `set_bone_mode`, `set_bones_lod`, `set_root_mode`, `set_speed`, `set_time`.

### AnimationStateMachineComponent

`find_constraint_target`, `find_variable`, `get_constraint_target`, `get_variable`, `has_event`, `set_constraint_target`, `set_variable`, `trigger_event`.

### SceneGraphComponent

`local_pose`, `local_position`, `local_rotation`, `local_scale`, `node_index`, `num_nodes`, `parent`, `set_local_pose`, `set_local_position`, `set_local_rotation`, `set_local_scale`, `world_pose`, `world_position`, `world_rotation`.

### TransformComponent

`children`, `link`, `link_to_scene_graph`, `local_pose`, `local_position`, `local_rotation`, `local_scale`, `parent`, `set_local_pose`, `set_local_position`, `set_local_rotation`, `set_local_scale`, `unlink`, `world_pose`, `world_position`, `world_rotation`.

### Mesh

`bounding_volume`, `bounding_volume_components`, `get_geometry_stats`, `has_material`, `local_pose`, `local_position`, `local_rotation`, `material`, `material_slot_id`, `name`, `node`, `num_materials`, `refresh_instance_hash`, `set_explicit_sort_value`, `set_local_pose`, `set_local_position`, `set_local_rotation`, `set_shader_pass_flag`, `visibility`, `world_pose`, `world_position`, `world_rotation`.

### MeshComponent

`create_with_mesh`, `set_material`.

### RenderDataComponent

`add_instance_to_tags`, `instances_with_tag`, `instances_with_tag_in_entity`.

### Renderer

`begin_stream_captures`, `create_resource`, `destroy_resource`, `end_stream_captures`, `is_d3d12_supported`, `run_resource_generator`, `save_captured_frame`, `update_texture_base64`.

### Window

`clip_cursor`, `close`, `get_dpi_scale`, `get_main_window`, `has_focus`, `id`, `is_closing`, `is_resizable`, `maximize`, `minimize`, `mouse_focus`, `open`, `rect`, `restore`, `set_clip_cursor`, `set_cursor`, `set_cursor_position`, `set_focus`, `set_foreground`, `set_keystroke_enabled`, `set_mouse_focus`, `set_rect`, `set_resizable`, `set_resolution`, `set_show_cursor`, `set_title`, `show_cursor`, `sync_layered_window`, `trigger_resize`.

### Input

`event_queue`.

## Evidence and limits

- Original mapped EXE: `~/Applications/HellForge/mods-retired/localappdata-1019/LocalAppData/LinksLinkToMemory/r6_reload_exe_20261017.bin`; SHA-256 `2b2c496218a5ce7e4f3453951acb8bd756bc4c2f281ce95eadef17b894e39c16`. RVAs refer to the captured mapped representation, not ordinary on-disk PE offsets.
- Historical live census: `~/Documents/ChatGPT/Linux HD2/native-work/war-journalist-live-bindings.tsv`; SHA-256 `0a4a334b1a4df2b4d8aa976dc5be455833ec28955c350eb723b33bd1e9e1575e`. It belongs to a later build and is reference evidence only. Its addresses must not be used on HellForge.
- Archived API-name logs and hashes are in `secondary_sources`. Their text does not carry a current build stamp. They corroborate names, not current invocation semantics.
- Existing Photo Mode `docs/api-census.md` inventories **its own requested paths**, not the whole SDK; it was used only as a pointer to source dumps.
- No live namespace census, player input scan, function-call trial, bone write, render experiment or camera activation was performed in this pass.
- To finish the broad SDK investigation, compare the complete current game-owned `stingray` table/aliases against this ledger, then investigate relevant signatures, native callees, threads, ownership and native FPS/action state. Pure inventory coverage must not be reported as all behavior understood.

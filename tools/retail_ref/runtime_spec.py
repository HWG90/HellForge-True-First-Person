"""What runtime.lua reaches outside itself, written once.

tools/retail_ref/gen_runtime.py reads this to make three things that must agree: the host struct of
src/tfp_runtime_host.h (one function pointer per distinct collaborator method runtime.lua calls),
the scripted Lua fakes of run_runtime.lua, and the scripted C fakes of tests/vectors/runtime_fakes.h.

METHODS, one per line:   [@]object.method[?] in... -> out... [!effect]
  @        a module function (no self), e.g. every constructor
  ?        optional: runtime.lua tests whether the method exists; a NULL pointer is "it does not"
  in       v any value, n a number runtime.lua computed, b a boolean it computed, t one of its own
           texts, tab a table a fake handed out earlier (NULL: nil), tabx the same printed with the
           two fields the transition coordinator is given, ctx the Update scope, vals:KIND a table
           runtime.lua built (its fields as an array), _ a Lua argument the C call does not carry
  out      v[=default] a value; KIND a table of that kind; policies; nothing at all
  effect   what the fake does besides answering (both fakes alike)
GETTERS are fields runtime.lua reads; they are not calls in Lua, so they are not in the trace.
"""

TABLES = {
    "SNAP": "camera_kind safe player_identity identity body world controller_bytes camera viewmodel weapon_unit avatar_owner "
            "avatar_index aiming reason record seated controller_active weapon_ready zoom_identity",
    "STATE": "mode enabled phase player_identity blend cleanup_pending wait_reason",
    "FOV": "phase armed last_native last_applied transition_epoch",
    "TARGET": "phase armed",
    "CAM": "phase armed error head_distance replaced blend transition_epoch",
    "TRANS": "epoch progress",
    "MOTION": "action softened dx dy dz",
    "PLACE": "x y z yaw pitch roll",
    "LOOK": "sequence samples skipped yaw pitch ads_rebases",
    "DOF": "owned",
    "DEPTH": "supported armed zoom_index",
    "ROW": "core phase",
    "LEASE": "origin origin_identity origin_player_identity origin_body origin_world alignment ended kind helper depth_stats "
             "input_stats handle",
    "FIRDSNAP": "safe player_identity",
    "RENDER": "active last_error",
    "REPORT": "observed_idle pending_batches incomplete_after camera_kind reloading shoulder_aim",
    "EXPECTED": "safeNormal4 actualAim reload sprint firing weapon_transition foreign ads snapshot",
    "CAPTURE": "weapon_identity identity weapon_key source_fov projection px py pz rx ry rz rw reference pivot",
    "TARGETST": "neutral",
    "WINFO": "name family",
    "AOBJ": "object",
    "OPAQUE": "",
    # Tables runtime.lua builds and hands over (vals:KIND).
    "PROOF": "fresh safe same_player foreign",
    "EVENT": "policy reason phase cleanup_pending",
    "ACTX": "viewmodel avatar_owner avatar_index",
    "TIMING": "phase tick epoch expected",
    "GATE": "snapshot target lease controller pose fov camera motion lens neutral tick",
    "DEFAULTS": "enabled scale camera_bob_strength camera_sway_strength",
    "FIRDOPT": "native_animation render_driven ready_pose fire_filter camera_frame skin support source retarget children aim "
               "native_ads fov",
    "REC": "time enabled control_mask control_guard safe aiming snapshot_reason weapon_ready entry_block action cam_x cam_y cam_z "
           "cam_fx cam_fy cam_fz head_distance vm_lx vm_ly vm_lz vm_cx vm_cy vm_cz gun_cx gun_cy gun_cz gun_turn softened head_calls "
           "camera_gate head_phase player weapon request camera_kind ads foreign editor phase transition_epoch transition_progress "
           "pinned pin_expected camera_retired view_expected view_visible recovering fov_native fov_applied camera_blend pose_blend "
           "cleanup_pending camera_epoch fov_epoch pose_epoch placement_x placement_y placement_z placement_yaw placement_pitch "
           "placement_roll motion_x motion_y motion_z look_sequence look_samples look_skipped look_yaw look_pitch ads_rebases reason",
}
TABLES = {kind: fields.split() for kind, fields in TABLES.items()}
MAX_FIELDS = 20
POLICY_SLOTS = 4
LEASE_SLOTS = 3

METHODS = """
@backend.new
backend.pin_player_snapshot? -> SNAP
backend.player_snapshot -> SNAP
backend.snapshot -> SNAP v
backend.begin_frame -> !frame_on
backend.end_frame -> !frame_off
backend.ptr n:address -> v=4096
backend.read v:address n:size -> v !read
backend.alignment_context vals:ACTX -> AOBJ
backend.keep_aim_view? tab:snapshot ->
backend.detach_seated_camera? ->
backend.prepare_pose tab:snapshot -> v=T v
backend.pose_status tab:lease -> v
backend.set_pin_first_person v:value ->
backend.set_camera_update_token v:value ->
backend.set_action_placement_revision n:value -> !revision
backend.set_fird_camera_offset v:value ->
helper.depth_stats tab:lease v:handle -> DEPTH v
helper.input_stats tab:lease v:handle -> v v
@controller.new
controller.is_enabled -> v=F
controller.result -> STATE
controller.update ->
controller.stop t:reason -> v=T v
controller.suspend? t:reason -> v=T v
controller.retry_cleanup -> v=T v
controller.shutdown -> v=T v
controller.get_placement -> PLACE
controller.set_placement v:value v:extra -> v=T v
controller.start v:seconds v:mode -> v=T v
controller.is_clean -> v=T
controller.placement_target -> v v
controller.status -> v='c
@editor.new
editor.update ->
editor.is_active -> v=F
editor.cancel t:reason -> v=T v
editor.shutdown -> v=T v
editor.request_open -> v=T v
editor.status -> v
@menu.new
menu.begin_shutdown ->
menu.update ->
menu.shutdown -> v=T v
menu.fird_person_arms? -> v=T
menu.show_torso? -> v=F
menu.near_options? -> v
menu.motion_options? vals:DEFAULTS -> v
menu.native_ads -> v=F
menu.ads_mode -> v
@fov.new
fov.stop -> v=T v
fov.update _ -> v=T v
fov.result -> FOV
fov.is_clean -> v=T
fov.shutdown -> v=T v
fov.preflight -> v=T v
fov.status -> v='f
@blur.new
blur.stop -> v=T v
blur.update _ -> v=T v
blur.result -> TARGET
blur.is_clean -> v=T
blur.shutdown -> v=T v
blur.preflight -> v=T v
blur.status -> v='b
@camera.new
camera.stop -> v=T v
camera.update _ -> v=T v
camera.result -> CAM
camera.is_clean -> v=T
camera.shutdown -> v=T v
camera.preflight -> v=T v
camera.status -> v='m
camera.prepare -> v=T v
camera.is_pinned? tab:snapshot -> v=F
camera.look_stats? -> LOOK
camera.observe_parent -> v v
camera.free_aim n:milliseconds -> v v
camera.profile -> v v
camera.profile_stats -> v v
camera.kick_stats -> v v
camera.parent_stats -> v v
camera.handoff_stats -> v v
camera.free_aim_stats -> v v
camera.set_handoff v:entry_ms v:rotation_ms -> v v
camera.set_cut_hold v:exit v:eye v:anchor v:seated -> v v
@compat.new
compat.update -> v=T v
compat.shutdown -> v=T v
compat.foreign -> v=F
compat.prepare -> v=T v
compat.is_clean -> v=T
compat.stop -> v=T v
compat.result -> OPAQUE
@model_shadows.new
@head.new
head.stop -> v=T v
head.update _ -> v=T v
head.result -> TARGET
head.is_clean -> v=T
head.shutdown -> v=T v
head.sdk_handles? -> v
@movement.new
movement.stop -> v=T v
movement.update _ -> v=T v
movement.result -> TARGET
movement.is_clean -> v=T
movement.shutdown -> v=T v
@arm_pieces.new? -> v=T
arm_pieces.update tab:snapshot b:wanted v:handles ->
arm_pieces.stop -> v=T v
arm_pieces.is_clean -> v=T
arm_pieces.result -> OPAQUE
@grip_hand.new? -> v=T
grip_hand.update tab:snapshot b:wanted v:handles ->
grip_hand.stop -> v=T v
grip_hand.is_clean -> v=T
grip_hand.result -> OPAQUE
@dof.new
dof.update _ _ _ -> v=T v
dof.result -> DOF
dof.stop -> v=T v
dof.is_clean -> v=T
dof.shutdown -> v=T v
dof.status -> v='d
@near.new
near.stop -> v=T v
near.update _ -> v=T v
near.is_clean -> v=T
near.shutdown -> v=T v
near.result -> OPAQUE
near.status -> v='n
@motion.new
motion.reset -> v=T v
motion.capture_neutral b:native_ads -> v=T v
motion.update ->
motion.result -> MOTION
@native_aim.source
@native_aim.new
native_aim.shutdown -> v=T v
native_aim.update _ -> v=T v
native_aim.stop -> v=T v
native_aim.is_clean -> v=T
native_aim.result -> OPAQUE
@aim_markers.source
@aim_markers.new
aim_markers.shutdown -> v=T v
aim_markers.stop -> v=T v
aim_markers.update -> v=T v
aim_markers.is_clean -> v=T
aim_markers.target -> v v
aim_markers.toggle v:key v:identity -> v v
aim_markers.setting -> v v
aim_markers.set_setting v:value -> v v
aim_markers.result -> OPAQUE
@recovery.new
recovery.action t:name n:timestamp -> v='update
recovery.cleanup t:name b:clean n:timestamp ->
recovery.snapshot -> policies
recovery.needs_proof t:name -> v=F
recovery.revalidate t:name vals:PROOF n:timestamp -> v=T
recovery.success t:name n:timestamp b:active ->
recovery.failure t:name tab:state b:clean vals:PROOF n:timestamp v:why -> ROW
recovery.reset ->
recovery.has_policies -> v=F
recovery.blocks_view n:timestamp -> v=F v v
recovery.owner_reset ->
@diagnostics.new n:capacity
diagnostics.event n:tick t:kind vals:EVENT ->
diagnostics.record n:tick vals:REC ->
diagnostics.snapshot -> v
diagnostics.clear ->
@transition.new n:duration n:max_gap
@transition.native_ads tabx:value -> v=F
transition.result -> TRANS
transition.update tabx:value n:now v:enabled v:foreign ->
transition.reset t:reason ->
transition.action v:action n:now -> v v
transition.resume v:identity v:player n:now -> v v
@actions.configure_catalogue _ ->
@actions.resolve _ v:key v:action -> v v
@actions.saved _ v:key v:action v:value v:all_weapons -> v
@actions.default_placement _ v:key v:action -> v v
@actions.weapon_info v:key -> WINFO
@head_positioner.new
head_positioner.get_saved -> v v
head_positioner.save v:value v:key v:identity -> v=T v
head_positioner.placement_target -> v v
head_positioner.is_enabled -> v v
head_positioner.is_clean -> v v
head_positioner.get_placement -> v v
head_positioner.set_placement v:value v:extra -> v v
head_positioner.result -> v v
head_positioner.start v:seconds v:mode -> v v
@fird.new vals:FIRDOPT
@fird.capture _ _ _ v:token -> CAPTURE
fird.is_enabled -> v=F
fird.render_pose -> v=T v
fird.stop -> v=T v
fird.refresh_target -> v=T v
fird.update ->
fird.is_clean -> v=T
fird.start_native -> v=T v
fird.start v:reference -> v=T v
fird.reference_status -> TARGETST
fird.cached_capture -> v v
fird.prepare_native_ads tab:snapshot -> v v
fird.get_placement -> v v
fird.set_placement v:value v:extra -> v v
fird.result -> v v
fird.placement_target -> v v
fird.capture_set_aim v:hip v:aim ->
@fird_aim.new -> v
@fird_native_ads.new -> v
@fird_fov.new -> v
@fird_entry.new
fird_entry.cancel ->
fird_entry.is_enabled -> v=F
fird_entry.update -> v=T v
fird_entry.is_pending -> v=F
fird_entry.uses_fp -> v=F
fird_entry.request -> v=T v
fird_entry.status -> v
@fird_warm.new
fird_warm.is_clean -> v=T
fird_warm.stop -> v=T v
fird_warm.update tab:snapshot -> v v
@fird_render.new
fird_render.stop -> v=T v
fird_render.result -> RENDER
fird_render.start -> v=T v
@ready_pose.new
ready_pose.update ctx -> v=T v
ready_pose.stop -> v=T v
ready_pose.is_enabled -> v=F
ready_pose.filter_owner ctx -> v
ready_pose.is_clean -> v=T
ready_pose.result -> OPAQUE
@held_fire.new
held_fire.stop -> v=T v
held_fire.update v:admission ctx -> v=T v
held_fire.matches v:admission ctx -> v v
held_fire.result -> OPAQUE
@ready_source.new -> v
@ready_timing.new
ready_timing.sample vals:TIMING -> REPORT v
@capture_gate.new
@capture_gate.scene_lens _ tab:snapshot -> v
capture_gate.update vals:GATE -> v=T v
capture_gate.reset ->
capture_gate.token_set_capture v:token v:capture -> !ref_set
@mod.log t:text
@mod.set? t:key v:value -> v=T v
@clock.now? -> v=1 !clock
@mouse.button_index? t:name -> v=1
@mouse.button? n:index -> v=0
"""

# Fields runtime.lua reads. `name -> v` a value, `name -> KIND` a table, `name v -> v` a keyed read.
GETTERS = """
backend.pin_first_person -> v=F
backend.frame_active -> v=F
backend.frame_epoch -> v=1
backend.placement_weapon_key -> v
backend.presentation_pose_epoch -> v
backend.action_placement_revision -> v
backend.camera_ready -> v
backend.warm_lease -> lease
backend.alignment_lease -> lease
backend.presentation_pose_lease -> lease
controller.lease -> lease
camera.retired -> v
aim_markers.fault -> v
actions.actions -> v
actions.observed v -> v
fird_module.native_animation_default -> v=T
fird_module.render_pose_verified -> v
fird.weapon_snapshot -> FIRDSNAP
fird.native_animation -> v
fird.phase -> v
fird.enabled -> v
fird.writes -> v
fird.weapon_key -> v
fird.reference_needed -> v
fird.scene_fov -> v
fird.render_frames -> v
profile.body_sleeves -> v=T
profile.grip_hand -> v=T
profile.seated_head -> v=T
config.table -> v
config.field t -> v
config.action_mapped v -> v
ref.field v t -> v
global.get -> v=0
"""

# The functions runtime.lua hands out: (name, the on_enable stage that creates it, arguments, how
# many values it answers). Arguments: v a value, tab:GROUP a table built from that knob group, ctx.
STAGES = ("NONE BACKEND PRESENT CONTROLLER HEAD DOF NEAR FOREIGN NATIVE_AIM MARKERS_SOURCE MARKERS ENTRY HEADPOS EDITOR MOTION MENU "
          "API HELD READY GATE PREPARE FIRD RENDER WARM FIRDENTRY").split()
ENTRIES = """
on_enable NONE - 1
on_update NONE - 1
on_disable NONE - 2
on_cleanup_poll NONE - 2
log BACKEND v 0
clock CONTROLLER - 1
presentation_transition PRESENT - 2
presentation_action PRESENT v 2
presentation_resume PRESENT v,v 2
camera_before_entry ENTRY - 2
action_placement ENTRY v,v 2
action_mapping ENTRY - 1
foreign_camera FOREIGN - 1
fallback_ready HEAD - 1
pinned_ready HEAD tab:arg_snap 1
full_body HEAD - 1
hide_torso HEAD - 1
show_torso HEAD - 1
seated_head HEAD - 1
active_view DOF - 1
dof_fallback_ready DOF - 1
near_options NEAR - 3
native_aim_enabled NATIVE_AIM - 1
markers_fird MARKERS_SOURCE - 1
markers_source_global_setting MARKERS_SOURCE - 1
markers_settings MARKERS - 1
markers_save MARKERS v 2
markers_global_setting MARKERS - 1
markers_save_global MARKERS v 2
hp_fird_enabled HEADPOS - 1
hp_get HEADPOS - 1
hp_set HEADPOS v 2
pc_is_enabled EDITOR - 2
pc_is_clean EDITOR - 2
pc_get_placement EDITOR - 2
pc_set_placement EDITOR v,v 2
pc_result EDITOR - 2
pc_placement_target EDITOR - 2
pc_start EDITOR v,v 2
store_get EDITOR v,v 2
store_can_start EDITOR - 2
store_action_supported EDITOR v 1
store_get_action EDITOR v,v 2
store_target EDITOR - 2
store_save EDITOR v,v,v,v,v 2
store_select_mode EDITOR v 2
store_reset_all EDITOR - 2
store_fields EDITOR - 8
store_baseline EDITOR v,v 5
motion_options MOTION - 5
start_true_first_person MENU - 2
toggle_pose MENU - 2
stop_pose MENU v 2
start_native MENU v 2
start_free_aim MENU - 2
view_enabled MENU - 1
view_clean MENU - 1
presentation_settings MENU - 1
current_weapon MENU - 5
save_presentation MENU v,v,v,v 2
aiming_markers MENU - 2
toggle_aiming_markers MENU v,v 2
aim_markers_setting MENU - 2
set_aim_markers_setting MENU v 2
dev_status MENU - 1
seamless_version API - 1
presentation_state API - 1
diagnostics API - 2
recovery_status API - 2
clear_diagnostics API - 1
start_pose API v,v 2
start_fird API - 2
probe_fird API v 2
observe_camera_parent API - 2
profile_camera API - 2
camera_profile API - 2
camera_kick_stats API - 2
camera_parent_stats API - 2
camera_handoff API v,v 2
camera_handoff_stats API - 2
camera_cut_hold API v,v,v,v 2
stop_free_aim API - 2
free_aim_stats API - 2
edit_placement API - 2
placement API - 2
depth_stats API - 2
input_stats API - 2
status API - 30
preflight API - 2
held_fire_authorized HELD ctx 1
ready_source_factory READY - 1
ready_filter_valid READY ctx,v 2
ready_log READY v,v 0
native_ready_gate GATE tab:arg_expected,ctx 2
fird_prepare PREPARE tab:arg_snap 2
fird_aim_mode FIRD - 1
fird_editor FIRD - 1
render_fird_pose RENDER - 2
fird_render_active RENDER - 1
fird_tick WARM - 1
fe_running FIRDENTRY - 1
fe_target FIRDENTRY - 1
fe_stop FIRDENTRY - 2
fe_cached FIRDENTRY - 2
fe_initialize FIRDENTRY tab:arg_snap 2
fe_begin_fp FIRDENTRY - 2
fe_sample FIRDENTRY - 2
fe_capture FIRDENTRY v 1
fe_enter FIRDENTRY v 2
"""

# What api.status() answers, flattened: the status text, then these fields of its result table.
STATUS_FIELDS = ("self fird fird_phase fird_enabled fird_writes fird_weapon fird_reference_needed fird_scene_fov fird_render_frames "
                 "fird_pending fird_renderer enabled editor_active editor_status fov arm_pieces grip_hand blur camera motion compat head "
                 "movement dof native_aim near aim_markers ready_pose held_fire").split()

# Knob groups that only serve as arguments of the functions above.
ARG_GROUPS = {"arg_snap": "SNAP", "arg_expected": "EXPECTED"}
# The objects (module-level variables of runtime.lua) shown in the state, in this order.
OBJECTS = ("controller menu editor fov camera motion compat head movement blur api foreign_camera backend dof near "
           "arm_pieces grip_hand fird fird_entry fird_warm fird_render aim_markers native_aim ready_pose ready_timing held_fire recovery "
           "diagnostics presentation runtime_clock presentation_module Actions probe_update").split()


def parse_default(text):
    if text == "T":
        return True
    if text == "F":
        return False
    if text == "~" or text == "":
        return None
    if text.startswith("'"):
        return text[1:]
    return float(text)


class Method:
    def __init__(self, line):
        left, _, right = line.partition("->")
        head = left.split()
        tail = right.split()
        target = head[0]
        self.static = target.startswith("@")
        target = target.lstrip("@")
        self.optional = target.endswith("?")
        target = target.rstrip("?")
        self.obj, self.method = target.split(".")
        self.host = f"{self.obj}_{self.method}"
        self.ins = []           # (type, kind or None, name)
        counts = {}
        for token in head[1:]:
            kind, _, name = token.partition(":")
            sub = None
            if kind == "vals":
                sub, name = name, name.lower()
            if kind == "ctx":
                name = "context"
            if not name:
                counts[kind] = counts.get(kind, 0) + 1
                name = f"{kind}{counts[kind]}"
            self.ins.append((kind, sub, name))
        self.outs = []          # ('v', default) | ('tab', KIND) | ('policies', None)
        self.effect = None
        for token in tail:
            if token.startswith("!"):
                self.effect = token[1:]
            elif token.startswith("v"):
                self.outs.append(("v", parse_default(token[2:]) if "=" in token else None))
            elif token == "policies":
                self.outs.append(("policies", None))
            else:
                assert token in TABLES, token
                self.outs.append(("tab", token))


class Getter:
    def __init__(self, line):
        left, _, right = line.partition("->")
        head = left.split()
        self.obj, self.field = head[0].split(".")
        self.host = f"{self.obj}_{self.field}"
        self.args = head[1:]
        out = right.strip()
        self.default = None
        if out.startswith("v"):
            self.out = "v"
            self.default = parse_default(out[2:]) if "=" in out else None
        else:
            self.out = out      # 'lease' or a KIND


class Entry:
    def __init__(self, line):
        name, stage, args, count = line.split()
        self.name, self.stage, self.nret = name, stage, int(count)
        self.args = [] if args == "-" else args.split(",")


METHOD_LIST = [Method(line) for line in METHODS.strip().splitlines()]
GETTER_LIST = [Getter(line) for line in GETTERS.strip().splitlines()]
ENTRY_LIST = [Entry(line) for line in ENTRIES.strip().splitlines()]


class Knobs:
    """Every scripted answer has a numbered knob; both fakes read the same numbers."""

    def __init__(self):
        self.names, self.defaults, self.index = ["none"], [None], {}
        self.groups = {}        # group name -> (base, KIND)
        self.method = {}        # host -> list of out descriptors
        self.getter = {}
        self.has = {}
        ref = 1000
        for m in METHOD_LIST:
            outs = []
            for i, (kind, extra) in enumerate(m.outs):
                if kind == "v":
                    outs.append(("v", self.add(f"{m.host}#{i + 1}", extra)))
                elif kind == "tab":
                    ref += 1
                    outs.append(("tab", self.group(m.host, extra, ref), extra))
                else:
                    ref += 1
                    base = self.add(f"{m.host}.present", 1.0)
                    self.add(f"{m.host}.ref", float(ref))
                    self.add(f"{m.host}.count", 0.0)
                    for slot in range(POLICY_SLOTS):
                        self.add(f"{m.host}.{slot}.name", None)
                        self.add(f"{m.host}.{slot}.core", None)
                        self.add(f"{m.host}.{slot}.phase", None)
                    outs.append(("policies", base))
            self.method[m.host] = outs
            if m.optional:
                self.has[m.host] = self.add(f"has.{m.host}", 1.0)
        for g in GETTER_LIST:
            if g.out == "v":
                self.getter[g.host] = ("v", self.add(g.host, g.default))
            elif g.out == "lease":
                self.getter[g.host] = ("lease", self.add(g.host, 0.0))
            else:
                ref += 1
                self.getter[g.host] = ("tab", self.group(g.host, g.out, ref), g.out)
        for slot in range(1, LEASE_SLOTS + 1):
            self.group(f"lease{slot}", "LEASE", 100 + slot)
        for name, kind in ARG_GROUPS.items():
            ref += 1
            self.group(name, kind, ref)
        # Scripted extras.
        self.add("clock.step", 0.0)             # added to clock_now#1 after every clock read
        self.add("frame.during", True)          # what begin_frame makes backend.frame_active
        self.add("actions.observed_name", None) # the one action Actions.observed_actions holds ...
        self.add("config.mapped_action", None)  # ... and the one name config.action_state_mapping maps to
        for key in ("aim_markers", "show_aim_markers", "action_state_mapping", "fird_head_camera", "motion_enabled", "motion_scale",
                    "weapon_placements", "action_placements", "placement"):
            self.add(f"config.{key}", None)
        self.count = len(self.names)

    def add(self, name, default):
        self.index[name] = len(self.names)
        self.names.append(name)
        self.defaults.append(default)
        return self.index[name]

    def group(self, name, kind, ref):
        base = self.add(f"{name}.present", 1.0)
        self.add(f"{name}.ref", float(ref))
        for field in TABLES[kind]:
            self.add(f"{name}.{field}", None)
        self.groups[name] = (base, kind)
        return base


KNOBS = Knobs()

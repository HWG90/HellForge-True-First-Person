"""The scripted sessions of the runtime vectors (see gen_runtime.py).

Hand-written sessions walk the situations of play (enable, enter, aim, swap, seated, death and
respawn, recovery, settings, the clock misbehaving, shutdown) and every function runtime.lua hands
out; sessions marked sweep are replayed once per call the marked operations make into the fakes,
with that call raising, refusing, or answering nil. Seeded random sessions mix all of it.
"""
import random
import struct

from runtime_spec import ENTRY_LIST, KNOBS, TABLES, METHOD_LIST, GETTER_LIST

F32 = [0.0, -0.0, 1.0, -1.0, 0.5, 3.25, -7.125, 1e-40, float("inf"), float("nan"), 0.70710678, 123.456, -0.001, 2.5e10, 1e-20, 0.33333334]


def floats(values):
    return struct.pack("<%df" % len(values), *values).decode("latin-1")


def build(Session, Ref):
    out = []

    def session(name, **kw):
        s = Session(name, **kw)
        out.append(s)
        return s

    T = [0.0]

    def frame(s, dt=1 / 60, **kw):
        T[0] += dt
        s.set("clock_now#1", T[0])
        return s.op("on_update", **kw)

    def begin(name, enable=True, sweep=False, tail=()):
        # (sweep and tail are kept for reading only: the failing calls are swept in the short sessions further down)
        T[0] = 1.0
        s = session(name)
        s.set("clock_now#1", T[0])
        if enable:
            s.op("on_enable")
        return s

    def lease(s, slot=1, player="P1", identity="W1", body=11, world=22, kind="pose", **more):
        fields = dict(origin=True, origin_identity=identity, origin_player_identity=player, origin_body=body, origin_world=world,
                      alignment=True, kind=kind, helper=True, depth_stats=True, input_stats=True, handle=Ref(70 + slot))
        fields.update(more)
        s.group(f"lease{slot}", **fields)
        s.set("controller_lease", slot).set("backend_alignment_lease", slot).set("backend_presentation_pose_lease", slot)
        s.set("backend_presentation_pose_epoch", 3)

    def snapshot(s, **fields):
        base = dict(camera_kind=6, safe=True, player_identity="P1", identity="W1", body=11, world=22, weapon_ready=True)
        base.update(fields)
        for group in ("backend_snapshot", "backend_player_snapshot", "backend_pin_player_snapshot", "arg_snap"):
            s.group(group, **base)

    def live(s, **fields):
        """The knobs of a running first-person view."""
        s.set("controller_is_enabled#1", True).set("controller_is_clean#1", False)
        s.group("controller_result", mode="pose_offset", enabled=True, phase="running", player_identity="P1", blend=1.0)
        s.group("camera_result", phase="active", armed=True, error=0, head_distance=0.25, replaced=4, blend=0.5, transition_epoch=2)
        s.group("fov_result", phase="active", armed=True, last_native=70.5, last_applied=85.25, transition_epoch=2)
        for name in ("blur", "head", "movement"):
            s.group(f"{name}_result", phase="active", armed=True)
        s.group("transition_result", epoch=2, progress=0.75)
        s.group("motion_result", action="idle", softened=False, dx=0.001, dy=-0.002, dz=0.0)
        s.group("controller_get_placement", x=0.15, y=0.0, z=-0.12, yaw=1.5, pitch=-0.5, roll=0.25)
        s.group("camera_look_stats", sequence=9, samples=120, skipped=1, yaw=0.125, pitch=-0.0625, ads_rebases=2)
        s.group("dof_result", owned=True)
        s.set("backend_placement_weapon_key", "weapon_ar")
        lease(s)
        snapshot(s, **fields)

    def idle(s):
        s.set("controller_is_enabled#1", False).set("controller_is_clean#1", True).set("controller_lease", 0)
        s.group("controller_result", mode="pose_offset", enabled=False, phase="idle")
        s.group("dof_result", owned=False)

    def memory(s, values=None):
        s.set("backend_read#1", floats(values or F32)).set("backend_ptr#1", 0x7FF612340000)
        s.group("backend_alignment_context", object=0x21000000)
        for group in ("backend_snapshot", "backend_player_snapshot", "backend_pin_player_snapshot", "arg_snap"):
            s.set(f"{group}.camera", 0x30000000).set(f"{group}.viewmodel", 77).set(f"{group}.weapon_unit", 78)
            s.set(f"{group}.avatar_owner", 5).set(f"{group}.avatar_index", 2)

    TAIL = (("on_update", ()), ("on_disable", ()), ("on_cleanup_poll", ()), ("on_enable", ()), ("on_update", ()))

    # ---------------------------------------------------------------- enable, idle, disable
    s = begin("enable, idle frames, disable", enable=False, sweep=True, tail=TAIL)
    s.op("on_update").op("on_disable").op("on_cleanup_poll")
    s.op("on_enable", mark=True)
    frame(s, mark=True)
    frame(s)
    idle(s)
    frame(s, mark=True)
    frame(s, mark=True)
    s.op("on_enable")
    s.op("on_disable", mark=True)
    frame(s)
    s.op("on_enable")
    frame(s)
    s.op("on_cleanup_poll", mark=True)
    s.op("on_cleanup_poll")

    s = begin("enable refused: global owned, clock missing", enable=False)
    s.set("global_get", 2).op("on_enable").op("on_update")
    s.set("global_get", 0).set("has.clock_now", 0).op("on_enable").op("on_update").op("on_disable")
    s.set("has.clock_now", 1).op("on_enable").op("on_enable")
    frame(s)
    s.set("global_get", 2).op("on_disable").op("seamless_version")
    s.set("has.arm_pieces_new", 0).set("grip_hand_new#1", False).op("on_enable")
    live(s)
    frame(s)
    s.op("status").op("stop_pose")
    s.op("on_disable")
    s.set("actions_resolve#1", Ref(5)).set("controller_set_placement#1", False).set("controller_set_placement#2", "bad placement").op("on_enable")
    s.op("on_disable")
    s.set("controller_set_placement#2", None).op("on_enable").op("on_cleanup_poll")
    s.set("controller_set_placement#2", 12).op("on_enable").op("on_cleanup_poll")

    # ---------------------------------------------------------------- enter, play, exit
    s = begin("toggle on, frames, toggle off", sweep=True, tail=TAIL)
    idle(s)
    frame(s)
    s.op("toggle_pose", mark=True)
    live(s)
    memory(s)
    frame(s, mark=True)
    frame(s)
    s.set("backend_snapshot.aiming", True).set("backend_player_snapshot.aiming", True)
    s.set("backend_player_snapshot.controller_active", True).set("backend_player_snapshot.controller_bytes", "\x00\x00\x00\x00\x07\x09")
    s.set("mouse_button#1", 1)
    frame(s, mark=True)
    s.set("mouse_button#1", 0)
    frame(s, mark=True)
    s.op("toggle_pose", mark=True)
    idle(s)
    frame(s)
    frame(s)
    s.op("on_disable")

    s = begin("aim, sights, zoom identity, mouse edges")
    live(s)
    memory(s)
    s.group("helper_depth_stats", supported=True, armed=True, zoom_index=2)
    frame(s)
    for zoom in (0, 31, 32, -1, 1.5, float("inf"), "2", None, True):
        s.set("helper_depth_stats.zoom_index", zoom)
        frame(s)
    s.set("helper_depth_stats.zoom_index", 3)
    for knob, value in (("helper_depth_stats.supported", False), ("helper_depth_stats.armed", None), ("helper_depth_stats.present", 0),
                        ("lease1.depth_stats", None), ("lease1.kind", "native"), ("lease1.ended", True), ("lease1.origin_identity", "W2"),
                        ("lease1.origin_player_identity", "P9"), ("lease1.origin", None), ("backend_alignment_lease", 2),
                        ("lease1.alignment", None), ("controller_lease", 0)):
        s.set(knob, value)
        frame(s)
        s.set(knob, KNOBS.defaults[KNOBS.index[knob]])
        lease(s)
    s.set("lease1.helper", None)
    frame(s)
    lease(s)
    s.set("has.backend_pin_player_snapshot", 0).set("backend_snapshot.present", 0)
    frame(s)
    s.set("has.backend_pin_player_snapshot", 1)
    frame(s)
    s.set("backend_pin_first_person", True)
    frame(s)
    s.set("backend_pin_player_snapshot.present", 0)
    frame(s)
    s.set("backend_pin_first_person", False).set("backend_snapshot.present", 1)
    for held, mode in ((1, 0), (1, 0), (0, 0), (0, 1), (0, 1), (0, 0), (1, 1), (0, 0)):
        s.set("mouse_button#1", held if not mode else 1)
        frame(s)
    for phase, enabled in (("waiting", True), ("running", False), ("running", True)):
        s.set("controller_result.phase", phase).set("controller_result.enabled", enabled).set("mouse_button#1", 1)
        frame(s)
        s.set("mouse_button#1", 0)
        frame(s)
    s.set("has.backend_keep_aim_view", 0).set("mouse_button#1", 1)
    frame(s)
    s.set("has.mouse_button", 0)
    frame(s)
    s.set("has.mouse_button", 1).set("mouse_button#1", "down")
    frame(s)
    s.op("on_disable")
    T[0] = 1.0
    for index, name in (("x", "right index refused"), (None, "no index"), (0, "index zero")):
        s = begin("mouse: " + name)
        live(s)
        s.set("mouse_button_index#1", index).set("mouse_button#1", 1)
        if index is None:
            s.set("has.mouse_button_index", 0)
        frame(s)
        s.set("mouse_button#1", 0)
        frame(s)
        frame(s, faults=[(1, 1)])

    s = begin("memory reads for the record")
    live(s)
    memory(s)
    frame(s)
    for knob, value in (("backend_read#1", floats(F32)[:47]), ("backend_read#1", floats(F32)[:63]), ("backend_read#1", floats(F32)[:11]),
                        ("backend_read#1", None), ("backend_read#1", 12), ("backend_ptr#1", None), ("backend_ptr#1", "p"),
                        ("backend_alignment_context.object", None), ("backend_alignment_context.present", 0),
                        ("backend_player_snapshot.viewmodel", None), ("backend_player_snapshot.weapon_unit", "w"),
                        ("backend_player_snapshot.camera", None), ("backend_read#1", floats(list(reversed(F32)))),
                        ("backend_read#1", floats([float("nan")] * 16)), ("backend_read#1", floats([2.0, 0.0] * 8))):
        s.set(knob, value)
        frame(s)
        memory(s)
    for bytes_ in ("\x01\x02\x03\x04\x05\x06", "\xff\xff\xff\xff\x00\x80", "\x00\x00\x00\x00\x00", "1234567", 6, None):
        s.set("backend_player_snapshot.controller_bytes", bytes_)
        frame(s)
    s.op("on_disable")

    # ---------------------------------------------------------------- views, swap, seated
    s = begin("view states: camera kinds, safe, fallback, pinned", sweep=True)
    live(s)
    VIEW = [("dof_update", 1, "active_view", ()), ("dof_update", 1, "dof_fallback_ready", ()), ("head_update", 1, "fallback_ready", ()),
            ("head_update", 1, "pinned_ready", (1,)), ("head_update", 1, "full_body", ()), ("head_update", 1, "show_torso", ()),
            ("head_update", 1, "seated_head", ()), ("head_update", 1, "hide_torso", ()), ("near_update", 1, "near_options", ()),
            ("native_aim_update", 1, "native_aim_enabled", ()), ("aim_markers_update", 1, "markers_settings", ()),
            ("controller_update", 1, "presentation_transition", ()), ("controller_update", 1, "presentation_action", ("ads",)),
            ("controller_update", 1, "presentation_resume", ("W1", "P1")), ("controller_update", 1, "camera_before_entry", ()),
            ("controller_update", 1, "action_placement", ("weapon_ar", "aim")), ("controller_update", 1, "action_mapping", ()),
            ("controller_update", 1, "foreign_camera", ()), ("motion_update", 1, "motion_options", ()), ("motion_update", 1, "clock", ()),
            ("menu_update", 1, "view_enabled", ()), ("menu_update", 1, "view_clean", ()), ("menu_update", 1, "dev_status", ()),
            ("menu_update", 1, "current_weapon", ())]
    frame(s, cbs=VIEW, mark=True)
    for kind, safe in ((4, True), (4, False), (6, False), (5, True), (None, True), ("6", True)):
        snapshot(s, camera_kind=kind, safe=safe)
        frame(s, cbs=VIEW, mark=(kind == 4))
        s.set("camera_result.armed", False)
        frame(s, cbs=VIEW)
        s.set("camera_result.armed", True).set("camera_result.error", 2)
        frame(s, cbs=VIEW)
        s.set("camera_result.error", 0).set("camera_result.phase", "prepared")
        frame(s, cbs=VIEW)
        s.set("camera_result.phase", "active")
    snapshot(s, camera_kind=4, safe=False)
    for knob, value in (("camera_is_pinned#1", True), ("has.camera_is_pinned", 0), ("controller_result.mode", "native"),
                        ("controller_is_enabled#1", False), ("backend_player_snapshot.player_identity", "P2"),
                        ("backend_player_snapshot.present", 0), ("compat_foreign#1", True), ("compat_foreign#1", None),
                        ("head_result.phase", "idle"), ("controller_result.present", 0), ("camera_result.present", 0),
                        ("head_result.present", 0), ("lease1.origin", None), ("controller_lease", 0), ("camera_retired", True),
                        ("has.camera_look_stats", 0), ("camera_look_stats.present", 0)):
        s.set(knob, value)
        frame(s, cbs=VIEW)
        frame(s, cbs=VIEW)
        s.set(knob, KNOBS.defaults[KNOBS.index[knob]])
        live(s, camera_kind=4, safe=False)
    snapshot(s, camera_kind=6, safe=True)
    frame(s, cbs=VIEW)
    s.set("camera_is_pinned#1", True)
    frame(s, cbs=VIEW, mark=True)
    s.op("on_disable")

    s = begin("weapon swap and seated interaction")
    live(s)
    memory(s)
    frame(s)
    snapshot(s, camera_kind=4, safe=False, reason="weapon swap", weapon_ready=False)
    s.set("controller_result.phase", "waiting").set("controller_result.wait_reason", "swap")
    for _ in range(3):
        frame(s, cbs=[("dof_update", 1, "active_view", ())])
    s.set("backend_snapshot.present", 0).set("backend_snapshot#2", "no weapon")
    frame(s)
    lease(s, slot=2, identity="W2")
    snapshot(s, identity="W2")
    s.set("controller_result.phase", "running").set("backend_placement_weapon_key", "weapon_smg")
    frame(s)
    for seated, kind in ((True, 4), (True, 4), (True, 6), (True, 4), (False, 4), (True, 4)):
        snapshot(s, identity="W2", seated=seated, camera_kind=kind)
        frame(s)
    s.set("has.backend_detach_seated_camera", 0)
    snapshot(s, identity="W2", seated=False, camera_kind=4)
    frame(s)
    snapshot(s, identity="W2", seated=True, camera_kind=4)
    frame(s)
    s.set("has.backend_detach_seated_camera", 1).set("editor_is_active#1", True)
    frame(s)
    s.set("editor_is_active#1", False).set("compat_foreign#1", True)
    frame(s)
    s.set("compat_foreign#1", False)
    frame(s, faults=[])
    s.op("on_disable")

    # ---------------------------------------------------------------- arms, menu options, profile
    s = begin("arm pieces, grip hand and the options around them")
    live(s)
    s.set("backend_player_snapshot.controller_bytes", "\x00\x00\x00\x00\x00\x00").set("head_sdk_handles#1", Ref(44))
    frame(s)
    for knob, value in (("menu_fird_person_arms#1", False), ("has.menu_fird_person_arms", 0), ("profile_body_sleeves", False),
                        ("profile_grip_hand", None), ("has.head_sdk_handles", 0), ("backend_player_snapshot.controller_bytes", "\x01\x00\x00\x00\x00\x00"),
                        ("backend_player_snapshot.camera_kind", 4), ("backend_player_snapshot.safe", False), ("compat_foreign#1", True),
                        ("controller_result.phase", "waiting"), ("menu_fird_person_arms#1", "yes")):
        s.set(knob, value)
        frame(s)
        s.set(knob, KNOBS.defaults[KNOBS.index[knob]])
        live(s)
        s.set("backend_player_snapshot.controller_bytes", "\x00\x00\x00\x00\x00\x00")
    s.op("on_disable")

    # ---------------------------------------------------------------- idle shortcut, foreign camera, editor
    s = begin("idle shortcut, foreign camera, editor", sweep=True)
    idle(s)
    frame(s)
    frame(s, mark=True)
    frame(s)
    s.set("compat_foreign#1", True)
    frame(s, mark=True)
    s.set("compat_foreign#1", False)
    frame(s)
    for knob, value in (("near_is_clean#1", False), ("near_is_clean#1", None), ("dof_result.owned", True), ("dof_result.present", 0),
                        ("fov_update#1", False), ("blur_update#1", None), ("camera_update#1", "yes"), ("head_update#1", False),
                        ("movement_update#1", False), ("dof_update#1", False), ("editor_is_active#1", True)):
        s.set(knob, value)
        frame(s)
        frame(s)
        s.set(knob, KNOBS.defaults[KNOBS.index[knob]])
        idle(s)
        frame(s)
        frame(s)
    s.set("compat_update#1", False)
    frame(s, mark=True)
    frame(s)
    s.set("compat_update#1", True).set("editor_is_active#1", True)
    frame(s, mark=True)
    s.set("movement_stop#1", False)
    frame(s)
    s.op("on_disable")

    # ---------------------------------------------------------------- recovery
    def policies(s, *rows):
        s.set("recovery_has_policies#1", bool(rows)).set("recovery_snapshot.count", len(rows))
        for i, (name, core, phase) in enumerate(rows):
            s.set(f"recovery_snapshot.{i}.name", name).set(f"recovery_snapshot.{i}.core", core).set(f"recovery_snapshot.{i}.phase", phase)

    s = begin("recovery: each answer of the ledger", sweep=True)
    live(s)
    frame(s)
    for action in ("cleanup", "retry", "wait", 7, None, True):
        s.set("recovery_action#1", action)
        frame(s, mark=(action == "cleanup"))
        policies(s, ("fov", True, "fatal"), ("camera", False, "fatal"), ("head", True, "cooldown"))
        frame(s, mark=(action == "cleanup"))
        s.set("fov_stop#1", False).set("camera_is_clean#1", False)
        frame(s)
        s.set("fov_stop#1", True).set("camera_is_clean#1", True)
        policies(s)
    s.set("recovery_action#1", "update").set("recovery_needs_proof#1", True)
    frame(s, mark=True)
    s.set("recovery_revalidate#1", False)
    frame(s, mark=True)
    policies(s, ("blur", True, "retry"), ("movement", None, "retry"))
    frame(s, mark=True)
    s.set("recovery_revalidate#1", True).set("recovery_needs_proof#1", False)
    frame(s)
    policies(s)
    for target in ("fov", "blur", "camera", "head", "movement"):
        for core, phase in ((True, "fatal"), (True, "retry"), (False, "fatal"), (None, None)):
            s.set(f"{target}_update#1", False).set(f"{target}_update#2", f"{target} refused")
            s.group("recovery_failure", core=core, phase=phase)
            frame(s, mark=(target == "fov" and phase == "retry"))
            s.set("has.controller_suspend", 0)
            frame(s)
            s.set("has.controller_suspend", 1).set(f"{target}_stop#1", False)
            frame(s)
            s.set(f"{target}_stop#1", True).set(f"{target}_is_clean#1", False)
            frame(s)
            s.set(f"{target}_is_clean#1", True).set(f"{target}_update#1", True).set(f"{target}_update#2", None)
        s.set(f"{target}_result.phase", "idle")
        frame(s)
        s.set(f"{target}_result.phase", "active").set(f"{target}_result.armed", False)
        frame(s)
        s.set(f"{target}_result.armed", None)
        frame(s)
        s.set(f"{target}_result.armed", True).set(f"{target}_result.present", 0)
        frame(s)
        s.set(f"{target}_result.present", 1)
    s.set("recovery_failure.present", 0).set("fov_update#1", False)
    frame(s)
    s.set("recovery_failure.present", 1).set("fov_update#1", True)
    s.set("recovery_blocks_view#1", True).set("recovery_blocks_view#2", "camera").set("recovery_blocks_view#3", "cooldown")
    frame(s, cbs=[("menu_update", 1, "dev_status", ())], mark=True)
    s.set("controller_lease", 0)
    frame(s)
    s.set("recovery_blocks_view#1", False)
    lease(s)
    s.set("controller_is_enabled#1", False)
    frame(s)
    s.set("controller_is_enabled#1", "on")
    frame(s)
    frame(s)
    s.set("controller_is_enabled#1", True)
    frame(s)
    s.op("on_disable")

    s = begin("death and respawn: the recovery owner changes", sweep=True)
    live(s)
    frame(s)
    frame(s)
    policies(s, ("camera", True, "retry"))
    frame(s)
    snapshot(s, player_identity="P2", body=31, world=22)
    frame(s, mark=True)
    frame(s, mark=True)
    frame(s)
    policies(s)
    frame(s)
    s.set("controller_result.player_identity", "P2")
    lease(s, player="P2", body=31)
    frame(s)
    policies(s, ("fov", False, "retry"))
    snapshot(s, player_identity="P3", body=41, world=23)
    frame(s)
    for knob, value in (("fov_stop#1", False), ("near_is_clean#1", False), ("grip_hand_is_clean#1", None), ("compat_foreign#1", True),
                        ("backend_snapshot.safe", False), ("backend_snapshot.present", 0), ("editor_is_active#1", True),
                        ("has.controller_suspend", 0)):
        s.set(knob, value)
        frame(s)
        frame(s)
        s.set(knob, KNOBS.defaults[KNOBS.index[knob]])
        if knob.startswith("backend_snapshot"):
            snapshot(s, player_identity="P3", body=41, world=23)
    s.set("controller_lease", 0)
    frame(s)
    snapshot(s, player_identity="P4", body=51, world=23)
    frame(s)
    frame(s)
    frame(s)
    frame(s)
    s.set("controller_result.player_identity", None)
    lease(s, player="P4", body=51, world=23)
    s.set("controller_lease", 0)
    policies(s)
    frame(s)
    lease(s, player="P4", body=51, world=23)
    frame(s)
    policies(s, ("head", True, "retry"))
    for player, body, world in (("P4", 52, 23), (5, 61, 23), ("P5", "b", 23), ("P5", 61, None), ("P5", 51, 23), ("P6", 51, 24)):
        snapshot(s, player_identity=player, body=body, world=world)
        frame(s)
        frame(s)
    s.set("controller_is_enabled#1", False)
    frame(s)
    s.op("on_disable")

    # ---------------------------------------------------------------- the clock
    s = begin("the clock: backwards, invalid, moving inside a frame", sweep=False)
    live(s)
    frame(s)
    for value in (0.5, -1.0, float("nan"), float("inf"), "1", None, True, 0.0):
        s.set("clock_now#1", value)
        s.op("on_update")
        s.op("clock")
        frame(s)
    s.set("clock.step", 0.001)
    frame(s)
    frame(s)
    s.set("clock.step", -0.01)
    frame(s)
    T[0] += 5
    frame(s)
    s.set("clock.step", 0.03)
    for _ in range(6):
        frame(s, dt=0.01)
    s.set("clock.step", 0)
    s.op("probe_fird", 1).op("dev_status").op("on_disable").op("clock").op("dev_status")
    s = begin("a frame already owned")
    live(s)
    s.set("backend_frame_active", True).op("on_update").op("on_cleanup_poll")
    s.set("backend_frame_active", False).set("frame.during", False)
    frame(s, cbs=[("controller_update", 1, "presentation_transition", ())])
    s.set("frame.during", True)
    frame(s, cbs=[("controller_update", 1, "presentation_transition", ()), ("controller_update", 1, "on_update", ()),
                  ("controller_update", 1, "on_cleanup_poll", ())])
    s.op("on_cleanup_poll", cbs=[("menu_shutdown", 1, "on_update", ()), ("controller_shutdown", 1, "presentation_transition", ())])

    # ---------------------------------------------------------------- every function, in each state
    BATTERY = [("log", ("hello",)), ("log", (None,)), ("log", (12.5,)), ("clock", ()), ("presentation_transition", ()),
               ("presentation_action", ("aim",)), ("presentation_resume", ("W1", "P1")), ("camera_before_entry", ()),
               ("action_placement", ("weapon_ar", "aim")), ("action_placement", (None, None)), ("action_placement", ("k", False)),
               ("action_mapping", ()), ("foreign_camera", ()), ("fallback_ready", ()), ("pinned_ready", (1,)), ("pinned_ready", (None,)),
               ("full_body", ()), ("hide_torso", ()), ("show_torso", ()), ("seated_head", ()), ("active_view", ()),
               ("dof_fallback_ready", ()), ("near_options", ()), ("native_aim_enabled", ()), ("markers_fird", ()),
               ("markers_source_global_setting", ()), ("markers_settings", ()), ("markers_save", (Ref(31),)),
               ("markers_global_setting", ()), ("markers_save_global", (True,)), ("hp_fird_enabled", ()), ("hp_get", ()),
               ("hp_set", (Ref(32),)), ("pc_is_enabled", ()), ("pc_is_clean", ()), ("pc_get_placement", ()),
               ("pc_set_placement", (Ref(33), "extra")), ("pc_result", ()), ("pc_placement_target", ()), ("pc_start", (2, "pose_offset")),
               ("store_get", ("weapon_ar", "aim")), ("store_can_start", ()), ("store_action_supported", ("idle",)),
               ("store_action_supported", ("camera",)), ("store_action_supported", ("reload",)), ("store_action_supported", ("vault",)),
               ("store_action_supported", ("mapped",)), ("store_action_supported", ("unused",)), ("store_action_supported", (None,)),
               ("store_action_supported", ("seen",)), ("store_get_action", ("weapon_ar", None)), ("store_target", ()),
               ("store_save", (Ref(34), "weapon_ar", "W1", True, "aim")), ("store_save", (Ref(34), None, "W1", False, None)),
               ("store_save", (Ref(34), "weapon_ar", "W1", None, None)), ("store_fields", ()), ("store_baseline", ("weapon_ar", "aim")),
               ("store_reset_all", ()), ("motion_options", ()), ("view_enabled", ()), ("view_clean", ()), ("presentation_settings", ()),
               ("current_weapon", ()), ("save_presentation", ("other", True, None, None)),
               ("save_presentation", ("family_placement_defaults", 1, None, None)),
               ("save_presentation", ("near_clip_profiles", True, None, None)),
               ("save_presentation", ("family_placement_defaults", True, None, None)),
               ("save_presentation", ("near_clip_profiles", Ref(35), "weapon_ar", "W1")),
               ("save_presentation", ("family_placement_defaults", False, "weapon_ar", "W1")), ("aiming_markers", ()),
               ("toggle_aiming_markers", ("weapon_ar", "W1")), ("aim_markers_setting", ()), ("set_aim_markers_setting", (Ref(36),)),
               ("dev_status", ()), ("seamless_version", ()), ("presentation_state", ()), ("diagnostics", ()), ("recovery_status", ()),
               ("clear_diagnostics", ()), ("observe_camera_parent", ()), ("profile_camera", ()), ("camera_profile", ()),
               ("camera_kick_stats", ()), ("camera_parent_stats", ()), ("camera_handoff", (120, 80)), ("camera_handoff_stats", ()),
               ("camera_cut_hold", (1, 2, 3, 4)), ("stop_free_aim", ()), ("free_aim_stats", ()), ("edit_placement", ()), ("placement", ()),
               ("depth_stats", ()), ("input_stats", ()), ("status", ()), ("preflight", ()), ("start_free_aim", ()), ("start_native", (3,)),
               ("start_pose", (2, None)), ("start_true_first_person", ()), ("toggle_pose", ()), ("stop_pose", (None,)),
               ("stop_pose", (True,)), ("held_fire_authorized", (1,)), ("ready_source_factory", ()), ("ready_filter_valid", (1, "adm")),
               ("ready_log", ("reason", "phase")), ("native_ready_gate", (1, 1)), ("fird_prepare", (1,)), ("fird_aim_mode", ()),
               ("fird_editor", ()), ("render_fird_pose", ()), ("fird_render_active", ()), ("fird_tick", ()), ("fe_running", ()),
               ("fe_target", ()), ("fe_stop", ()), ("fe_cached", ()), ("fe_initialize", (1,)), ("fe_begin_fp", ()), ("fe_sample", ()),
               ("fe_capture", (Ref(50),)), ("fe_enter", (Ref(51),))]

    def answers(s):
        """Distinct answers for the pass-through calls, so a swapped pair shows."""
        n = [200]
        for m in METHOD_LIST:
            for i, (kind, default) in enumerate(m.outs):
                if kind == "v" and default is None and m.host not in ("backend_snapshot", "backend_read", "ready_timing_sample") \
                        and not m.host.endswith("_new") and m.obj not in ("fird", "fird_entry", "fird_warm", "fird_render", "ready_pose",
                                                                             "held_fire", "capture_gate", "ready_source"):
                    n[0] += 1
                    s.set(f"{m.host}#{i + 1}", Ref(n[0]) if i == 0 else f"{m.host} second")
        s.set("controller_placement_target#1", "weapon_ar").set("controller_placement_target#2", "W1")
        s.set("head_positioner_placement_target#1", "camera_key").set("head_positioner_placement_target#2", "W1")
        s.set("editor_status#1", "closed").set("menu_ads_mode#1", None).set("menu_near_options#1", Ref(61)).set("menu_motion_options#1", Ref(62))
        s.set("actions_observed", True).set("actions.observed_name", "seen").set("config.mapped_action", "mapped")
        s.set("config.action_state_mapping", Ref(63)).set("config.aim_markers", Ref(64)).set("config.show_aim_markers", False)
        s.set("config.fird_head_camera", Ref(65)).set("config.motion_scale", 0.75).set("config.weapon_placements", Ref(66))
        s.set("config.placement", Ref(67)).set("head_sdk_handles#1", Ref(68)).set("backend_action_placement_revision", 4)
        s.set("mod_set#1", True).set("mod_set#2", None).set("aim_markers_fault", None)
        s.group("actions_weapon_info", name="Liberator", family="rifle")
        s.group("helper_depth_stats", supported=True, armed=True, zoom_index=1)

    def battery(s, mark=False):
        for name, args in BATTERY:
            s.op(name, *args, mark=mark)

    s = begin("every function before enable", enable=False)
    battery(s)
    s = begin("every function while idle", sweep=True)
    idle(s)
    answers(s)
    battery(s, mark=True)
    s.op("on_disable")
    battery(s)
    s = begin("every function while running", sweep=True)
    live(s)
    memory(s)
    answers(s)
    frame(s)
    battery(s, mark=True)
    frame(s)
    s.op("on_disable")
    battery(s)
    s.op("on_enable")
    battery(s)

    s = begin("settings changed in the middle of a session")
    live(s)
    answers(s)
    frame(s)
    SETTINGS = [("near_options", ()), ("motion_options", ()), ("native_aim_enabled", ()), ("show_torso", ()), ("seated_head", ()),
                ("markers_settings", ()), ("markers_global_setting", ()), ("markers_source_global_setting", ()), ("hp_get", ()),
                ("action_mapping", ()), ("presentation_settings", ()), ("store_action_supported", ("mapped",)),
                ("store_action_supported", ("seen",)), ("store_reset_all", ()), ("markers_save", (1,)), ("hp_set", (2,)),
                ("save_presentation", ("family_placement_defaults", True, None, None)), ("store_save", (Ref(34), "weapon_ar", "W1", True, "aim")),
                ("dev_status", ()), ("fird_aim_mode", ())]
    for knob, value in (("has.menu_near_options", 0), ("has.menu_motion_options", 0), ("has.menu_show_torso", 0), ("menu_show_torso#1", True),
                        ("menu_native_ads#1", True), ("menu_native_ads#1", "on"), ("profile_seated_head", False), ("config_table", None),
                        ("config.motion_enabled", False), ("config.motion_enabled", None), ("config.motion_scale", None),
                        ("config.motion_scale", False), ("config.action_state_mapping", None), ("config.mapped_action", None),
                        ("actions_observed", None), ("actions.observed_name", None), ("has.mod_set", 0), ("mod_set#1", False),
                        ("mod_set#1", None), ("backend_action_placement_revision", None), ("backend_action_placement_revision", False),
                        ("config.weapon_placements", None), ("config.action_placements", Ref(69)), ("aim_markers_fault", "broken"),
                        ("aim_markers_fault", 7), ("recovery_blocks_view#1", True), ("menu_near_options#1", None),
                        ("menu_motion_options#1", None)):
        s.set(knob, value)
        if knob == "recovery_blocks_view#1":
            s.set("recovery_blocks_view#2", "fov").set("recovery_blocks_view#3", 3)
        for name, args in SETTINGS:
            s.op(name, *args)
        frame(s)
        s.set(knob, KNOBS.defaults[KNOBS.index[knob]])
        answers(s)
    s.set("mod_set#1", False).set("mod_set#2", "disk full")
    for n in (1, 2, 3):
        s.op("store_reset_all", faults=[])
    s.op("on_disable")

    s = begin("the placement store and its two modes", sweep=True)
    idle(s)
    answers(s)
    STORE = [("store_fields", ()), ("store_baseline", ("weapon_ar", "aim")), ("store_get", ("weapon_ar", "aim")),
             ("store_get_action", ("weapon_ar", "ads")), ("store_target", ()), ("store_can_start", ()),
             ("store_save", (Ref(34), "weapon_ar", "W1", True, "aim")), ("store_save", (Ref(34), "camera_key", "W1", True, "aim")),
             ("store_reset_all", ()), ("pc_is_enabled", ()), ("pc_is_clean", ()), ("pc_get_placement", ()),
             ("pc_set_placement", (Ref(33), None)), ("pc_result", ()), ("pc_placement_target", ()), ("pc_start", (0, "pose_offset")),
             ("placement", ())]
    for mode in ("weapon", "camera", "other", None, "weapon", "camera"):
        s.op("store_select_mode", mode, mark=True)
        for name, args in STORE:
            s.op(name, *args, mark=(mode == "camera"))
    s.set("camera_retired", True).op("store_select_mode", "weapon").op("store_select_mode", "camera").op("store_fields")
    s.set("camera_retired", None)
    for knob, value in (("backend_snapshot.identity", "W9"), ("backend_snapshot.present", 0), ("controller_placement_target#1", "other"),
                        ("controller_placement_target#2", "W9"), ("mod_set#1", False), ("mod_set#1", "yes")):
        s.set(knob, value)
        s.op("store_save", Ref(34), "weapon_ar", "W1", True, "aim")
        s.set(knob, KNOBS.defaults[KNOBS.index[knob]])
        answers(s)
        snapshot(s)
    for knob in ("fov_is_clean#1", "blur_is_clean#1", "camera_is_clean#1", "head_is_clean#1", "movement_is_clean#1", "dof_is_clean#1",
                 "compat_is_clean#1", "compat_foreign#1", "controller_is_clean#1", "aim_markers_is_clean#1", "editor_is_active#1",
                 "native_aim_is_clean#1"):
        value = knob == "compat_foreign#1" or knob == "editor_is_active#1"
        s.set(knob, value)
        for name, args in (("store_can_start", ()), ("start_pose", (0, None)), ("start_native", (0,)), ("preflight", ()),
                           ("start_free_aim", ()), ("view_clean", ()), ("probe_fird", (2,))):
            s.op(name, *args)
        s.set("controller_is_enabled#1", True)
        s.op("store_can_start").op("start_free_aim").op("view_enabled")
        s.set("controller_is_enabled#1", False)
        s.set(knob, not value)
    s.op("on_disable")

    s = begin("start and stop: each refusal", sweep=True)
    idle(s)
    answers(s)
    s.op("start_pose", 0, None, mark=True)
    s.set("controller_is_enabled#1", True)
    s.op("start_pose", 0, None, mark=True)
    s.op("stop_pose", None, mark=True)
    s.set("controller_is_enabled#1", False)
    for knob, value in (("controller_set_placement#1", False), ("controller_set_placement#1", "ok"), ("controller_start#1", False),
                        ("controller_start#1", None), ("controller_start#1", "started"), ("fov_update#1", False), ("blur_update#1", False),
                        ("compat_foreign#1", True), ("actions_resolve#1", None)):
        s.set(knob, value)
        if knob.endswith("#1") and not knob.startswith("compat"):
            s.set(knob[:-1] + "2", knob + " why")
        s.op("start_pose", 0, None).op("start_true_first_person").op("toggle_pose").op("start_native", 5)
        s.set("controller_is_enabled#1", True)
        s.op("start_pose", 0, None)
        s.set("controller_is_enabled#1", False)
        s.set(knob, KNOBS.defaults[KNOBS.index[knob]])
        answers(s)
    for knob in ("held_fire_stop#1", "native_aim_stop#1", "aim_markers_stop#1", "fird_stop#1", "fird_warm_stop#1", "controller_stop#1",
                 "editor_cancel#1", "fov_stop#1", "blur_stop#1", "camera_stop#1", "head_stop#1", "movement_stop#1", "dof_stop#1",
                 "near_stop#1", "compat_stop#1", "arm_pieces_stop#1", "motion_reset#1"):
        s.set(knob, False).set(knob[:-1] + "2", knob + " why").set("editor_is_active#1", True)
        s.op("stop_pose", None).op("stop_pose", True)
        s.set("controller_is_enabled#1", True).op("toggle_pose").set("controller_is_enabled#1", False)
        s.set(knob, True).set(knob[:-1] + "2", None).set("editor_is_active#1", False)
    for knob in ("fov_preflight#1", "blur_preflight#1", "camera_preflight#1", "backend_prepare_pose#1"):
        for value in (False, None, "maybe"):
            s.set(knob, value).set(knob[:-1] + "2", knob + " why")
            s.op("preflight")
        s.set(knob, True).set(knob[:-1] + "2", None)
    s.set("backend_snapshot.present", 0).set("backend_snapshot#2", "no snapshot").op("preflight")
    snapshot(s)
    s.op("preflight", mark=True)
    for knob, value in (("lease1.alignment", None), ("lease1.helper", None), ("lease1.kind", "native"), ("lease1.depth_stats", None),
                        ("lease1.input_stats", None), ("controller_lease", 0), ("helper_depth_stats.present", 0)):
        lease(s)
        s.set(knob, value)
        s.op("depth_stats", mark=(knob == "helper_depth_stats.present")).op("input_stats")
    lease(s)
    s.op("depth_stats", mark=True).op("input_stats", mark=True)
    s.op("on_disable", mark=True)

    s = begin("shutdown: each part refusing, cleanup poll until clean", sweep=False)
    live(s)
    frame(s)
    for knob in ("native_aim_shutdown#1", "aim_markers_shutdown#1", "editor_shutdown#1", "controller_shutdown#1", "fov_shutdown#1",
                 "blur_shutdown#1", "camera_shutdown#1", "head_shutdown#1", "movement_shutdown#1", "dof_shutdown#1", "near_shutdown#1",
                 "compat_shutdown#1", "menu_shutdown#1", "motion_reset#1"):
        for value in (False, None, "pending"):
            s.set(knob, value).set(knob[:-1] + "2", knob + " why")
            s.op("on_disable").op("on_cleanup_poll").op("on_update").op("on_enable")
        s.set(knob, True).set(knob[:-1] + "2", None)
    s.op("on_cleanup_poll").op("on_cleanup_poll").op("on_enable")
    frame(s)
    s.op("on_disable").op("status").op("stop_pose", None).op("view_enabled")

    # ---------------------------------------------------------------- Fird Person (parked in retail)
    def capture(s, weapon="W1", fov=70.0, position=(0.1, 0.2, 0.3), rotation=(0.0, 0.0, 0.0, 1.0), ref=None, **more):
        fields = dict(weapon_identity=weapon, identity="I1", weapon_key="weapon_ar", source_fov=fov, projection=True, px=position[0],
                      py=position[1], pz=position[2], rx=rotation[0], ry=rotation[1], rz=rotation[2], rw=rotation[3], reference=Ref(81),
                      pivot=Ref(82))
        fields.update(more)
        s.group("fird_capture", **fields)
        if ref:
            s.set("fird_capture.ref", ref)

    FIRD_CALLS = [("fird_aim_mode", ()), ("fird_editor", ()), ("fird_prepare", (1,)), ("render_fird_pose", ()), ("fird_render_active", ()),
                  ("fird_tick", ()), ("fe_running", ()), ("fe_target", ()), ("fe_cached", ()), ("fe_initialize", (1,)), ("markers_fird", ()),
                  ("full_body", ()), ("native_aim_enabled", ()), ("pc_is_enabled", ()), ("pc_result", ()), ("pc_get_placement", ()),
                  ("pc_set_placement", (Ref(33), None)), ("pc_start", (Ref(51), None)), ("pc_placement_target", ()), ("pc_is_clean", ()),
                  ("store_can_start", ()), ("view_enabled", ()), ("view_clean", ()), ("status", ()), ("active_view", ()),
                  ("held_fire_authorized", (1,)), ("ready_source_factory", ()), ("ready_filter_valid", (1, "adm")),
                  ("ready_log", ("why", "restore")), ("native_ready_gate", (1, 1)), ("start_pose", (0, None)), ("start_pose", (0, True)),
                  ("start_free_aim", ()), ("toggle_pose", ())]
    READY = [("ready_pose_update", 1, "held_fire_authorized", (1,)), ("ready_pose_update", 1, "held_fire_authorized", (2,)),
             ("ready_pose_update", 1, "held_fire_authorized", (None,)), ("ready_pose_update", 1, "ready_filter_valid", (1, "adm")),
             ("ready_pose_update", 1, "native_ready_gate", (1, 1)), ("ready_pose_update", 1, "native_ready_gate", (1, 2)),
             ("ready_pose_update", 1, "native_ready_gate", (None, 1)), ("ready_pose_update", 1, "native_ready_gate", (1, None)),
             ("ready_pose_update", 1, "ready_log", ("gate", "apply")), ("ready_pose_update", 1, "ready_source_factory", ())]

    s = begin("Fird Person, natural: ready stance and held fire", sweep=True)
    idle(s)
    answers(s)
    snapshot(s)
    s.group("arg_expected", safeNormal4=True, snapshot=Ref(90))
    s.group("ready_timing_sample", observed_idle=True, pending_batches=0, incomplete_after=0, camera_kind=4)
    s.op("start_fird", mark=True)
    s.op("ready_source_factory")
    frame(s, cbs=READY, mark=True)
    s.set("fird_is_enabled#1", True).set("ready_pose_is_enabled#1", True)
    frame(s, cbs=READY, mark=True)
    for name, args in FIRD_CALLS:
        s.op(name, *args, mark=True)
    for field in ("actualAim", "reload", "sprint", "firing", "weapon_transition", "foreign", "ads"):
        s.set(f"arg_expected.{field}", True)
        frame(s, cbs=READY)
        s.set(f"arg_expected.{field}", None)
    s.set("arg_expected.safeNormal4", "yes")
    frame(s, cbs=READY)
    s.set("arg_expected.safeNormal4", True)
    for knob, value in (("ready_timing_sample.observed_idle", False), ("ready_timing_sample.pending_batches", 1),
                        ("ready_timing_sample.incomplete_after", None), ("ready_timing_sample.camera_kind", 6),
                        ("ready_timing_sample.reloading", True), ("ready_timing_sample.shoulder_aim", 1),
                        ("ready_timing_sample.present", 0), ("frame.during", False), ("ready_pose_update#1", False),
                        ("ready_pose_is_enabled#1", False), ("held_fire_update#1", False), ("held_fire_update#1", None),
                        ("fird_refresh_target#1", False), ("fird_is_enabled#1", False), ("fird_is_clean#1", False), ("fird_stop#1", False),
                        ("fird_weapon_snapshot.safe", True), ("fird_weapon_snapshot.player_identity", "P1"),
                        ("fird_weapon_snapshot.present", 0), ("fird_native_animation", True), ("ready_pose_is_clean#1", False)):
        s.set(knob, value)
        if knob == "ready_timing_sample.present":
            s.set("ready_timing_sample#2", "timing refused")
        if knob.startswith("fird_weapon_snapshot"):
            s.set("fird_weapon_snapshot.safe", True)
        frame(s, cbs=READY + [("dof_update", 1, "active_view", ()), ("editor_update", 1, "store_can_start", ())])
        s.op("active_view").op("store_can_start").op("status")
        if knob in ("fird_is_clean#1", "ready_pose_is_clean#1"):
            s.op("start_pose", 0, None).op("start_free_aim").op("toggle_pose").op("view_clean").op("on_cleanup_poll").op("on_disable")
            s.op("on_enable").op("start_fird")
        s.set(knob, KNOBS.defaults[KNOBS.index[knob]])
        s.set("fird_is_enabled#1", True).set("ready_pose_is_enabled#1", True)
        s.group("ready_timing_sample", observed_idle=True, pending_batches=0, incomplete_after=0, camera_kind=4)
    s.set("fird_is_enabled#1", False)
    for knob, value in (("fird_start_native#1", False), ("fird_start_native#1", None), ("controller_stop#1", False),
                        ("camera_retired", True), ("editor_is_active#1", True)):
        s.set(knob, value)
        s.op("start_fird", mark=(knob == "fird_start_native#1" and value is False))
        s.set(knob, KNOBS.defaults[KNOBS.index[knob]])
    s.set("fird_is_enabled#1", True)
    s.op("start_fird").op("probe_fird", 2).op("probe_fird", 0.5).op("probe_fird", 11).op("probe_fird", "2").op("probe_fird", float("nan"))
    s.set("fird_is_enabled#1", False).set("controller_is_clean#1", True)
    s.op("probe_fird", 2, mark=True)
    frame(s, dt=1.0)
    frame(s, dt=1.5, mark=True)
    frame(s)
    s.op("on_cleanup_poll", cbs=[("ready_pose_update", 1, "held_fire_authorized", (1,))], mark=True)
    s.op("on_update").op("status")
    for name, args in FIRD_CALLS:
        s.op(name, *args)

    s = begin("Fird Person, render driven: capture sequence", sweep=True)
    idle(s)
    answers(s)
    snapshot(s)
    lease(s)
    s.set("fird_module_native_animation_default", False)
    s.op("start_fird")
    s.set("fird_module_render_pose_verified", True).set("fird_render_start#1", False).set("fird_render_start#2", "no renderer")
    s.op("start_fird")
    s.set("fird_render_start#1", True).set("backend_camera_ready", True).set("fird_aim_new#1", Ref(91)).set("fird_native_ads_new#1", Ref(92))
    s.set("fird_fov_new#1", Ref(93)).set("capture_gate_update#2", Ref(94)).set("capture_gate_scene_lens#1", 68.5)
    s.set("backend_pose_status#1", Ref(95))
    s.group("fird_reference_status", neutral=Ref(96))
    s.op("start_fird", mark=True)
    for name, args in FIRD_CALLS:
        s.op(name, *args, mark=True)
    s.set("fird_entry_is_enabled#1", True)
    s.op("start_fird")
    frame(s, mark=True)
    s.set("fird_entry_is_pending#1", True)
    frame(s, mark=True)
    s.set("fird_warm_is_clean#1", False)
    frame(s, mark=True)
    s.op("status").op("start_pose", 0, None).op("start_native", 0).op("store_can_start").op("view_clean")
    s.set("fird_entry_uses_fp#1", True)
    s.op("fe_begin_fp", mark=True)
    s.set("controller_is_enabled#1", True)
    frame(s, mark=True)
    capture(s, ref=301)
    s.op("fe_sample", mark=True)
    T[0] += 0.02
    s.set("clock_now#1", T[0]).op("fe_sample")
    T[0] += 0.04
    s.set("clock_now#1", T[0]).op("fe_sample", mark=True)
    frame(s)
    capture(s, ref=302, position=(0.1, 0.2, 0.3))
    s.op("fe_sample")
    capture(s, ref=303, position=(0.1, 0.21, 0.3))
    T[0] += 0.1
    s.set("clock_now#1", T[0]).op("fe_sample", mark=True)
    s.op("fe_capture", Ref(94), mark=True).op("fe_capture", Ref(99)).op("fe_capture", None).op("fe_capture", "token")
    s.op("fe_enter", Ref(51), mark=True)
    s.op("fe_stop", mark=True)
    # each way the two captures can differ
    for change in (dict(weapon="W2"), dict(identity="I2"), dict(weapon_key="weapon_smg"), dict(fov=70.00011), dict(fov=70.00009),
                   dict(position=(0.1, 0.2, 0.3201)), dict(position=(0.1, 0.2, 0.3199)), dict(rotation=(0.0175, 0.0, 0.0, 0.99985)),
                   dict(rotation=(0.0174, 0.0, 0.0, 0.99985)), dict(rotation=(0.0, 0.0, 0.0, -1.0)), dict(position=(0.1141, 0.2141, 0.3))):
        s.op("fe_begin_fp")
        capture(s, ref=311)
        s.op("fe_sample")
        fields = {}
        kw = {}
        for key, value in change.items():
            if key in ("weapon", "fov", "position", "rotation"):
                kw[key] = value
            else:
                fields[key] = value
        capture(s, ref=312, **kw, **fields)
        T[0] += 0.06
        s.set("clock_now#1", T[0]).op("fe_sample")
        T[0] += 0.06
        s.set("clock_now#1", T[0]).op("fe_sample")
    # hip then a different weapon for ADS
    s.op("fe_begin_fp")
    capture(s, ref=321)
    s.op("fe_sample")
    T[0] += 0.06
    s.set("clock_now#1", T[0]).op("fe_sample")
    capture(s, ref=322, weapon="W7")
    s.op("fe_sample")
    T[0] += 0.06
    s.set("clock_now#1", T[0]).op("fe_sample").op("fe_sample")
    for knob, value in (("fov_update#1", False), ("camera_update#1", False), ("camera_update#1", None), ("capture_gate_update#1", False),
                        ("capture_gate_update#2", None), ("fird_capture.present", 0), ("fird_reference_status.present", 0),
                        ("controller_lease", 0), ("backend_pose_status#1", False), ("backend_snapshot.present", 0)):
        s.op("fe_begin_fp")
        capture(s, ref=331)
        s.op("fe_sample")
        s.set(knob, value)
        if knob in ("fov_update#1", "camera_update#1"):
            s.set(knob[:-1] + "2", "owner gone" if value is False else None)
        T[0] += 0.06
        s.set("clock_now#1", T[0]).op("fe_sample").op("fe_begin_fp")
        T[0] += 0.06
        s.set("clock_now#1", T[0]).op("fe_sample")
        s.set(knob, KNOBS.defaults[KNOBS.index[knob]])
        answers(s)
        s.set("capture_gate_update#2", Ref(94)).set("backend_pose_status#1", Ref(95))
        snapshot(s)
        lease(s)
    for knob, value in (("fird_start#1", False), ("fird_start#1", None), ("compat_foreign#1", True), ("fird_start#2", "why not")):
        s.set(knob, value)
        s.op("fe_enter", Ref(51), faults=[])
        s.set(knob, KNOBS.defaults[KNOBS.index[knob]])
    s.op("fe_enter", Ref(51), faults=[(3, 1)])
    s.set("fird_is_enabled#1", True)
    s.group("fird_render_result", active=True)
    frame(s, mark=True)
    s.group("fird_render_result", active=False, last_error="render lost")
    frame(s, mark=True)
    s.group("fird_render_result", active=None)
    frame(s)
    s.group("fird_render_result", active=True)
    s.set("fird_refresh_target#1", False).set("fird_refresh_target#2", "target changed")
    frame(s, mark=True)
    s.set("fird_refresh_target#1", True)
    for name, args in FIRD_CALLS:
        s.op(name, *args)
    s.set("backend_frame_active", True).op("render_fird_pose").op("fird_render_active")
    s.set("backend_frame_active", False).set("fird_render_pose#1", False).set("fird_render_pose#2", "no pose")
    s.op("render_fird_pose", mark=True)
    s.set("fird_is_enabled#1", False).set("fird_is_clean#1", False)
    frame(s, mark=True)
    s.set("fird_stop#1", False)
    frame(s)
    s.set("fird_stop#1", True).set("fird_is_clean#1", True).set("fird_entry_is_enabled#1", False).set("fird_entry_is_pending#1", False)
    s.set("fird_capture_phase_unused" if False else "fird_entry_uses_fp#1", True)
    frame(s)
    s.set("motion_capture_neutral#1", False).set("motion_capture_neutral#2", "not ready")
    frame(s, mark=True)
    s.set("backend_warm_lease", 1).set("fird_entry_is_enabled#1", True)
    snapshot(s, camera_kind=6)
    s.op("active_view")
    snapshot(s, camera_kind=4)
    s.op("active_view")
    s.set("fird_entry_uses_fp#1", False)
    s.op("probe_fird", 3)
    s.set("fird_entry_is_enabled#1", False).set("controller_is_enabled#1", False).set("fird_warm_is_clean#1", True)
    s.op("probe_fird", 3)
    frame(s, dt=4)
    s.op("on_cleanup_poll", mark=True)
    for name, args in FIRD_CALLS:
        s.op(name, *args)
    s.op("on_enable")
    s.set("backend_camera_ready", None)
    s.op("start_fird")
    for name, args in FIRD_CALLS:
        s.op(name, *args)
    s.op("on_disable")

    # ---------------------------------------------------------------- one failing call at a time
    SHORT = (("on_update", ()),)

    def sweep(name, prefix, ops, tail=SHORT, enable=True):
        T[0] = 1.0
        s = session("sweep: " + name, sweep=True, tail=tail)
        s.set("clock_now#1", T[0])
        if enable:
            s.op("on_enable")
        if prefix:
            prefix(s)
        for item in ops:
            entry, args = item[0], item[1]
            cbs = item[2] if len(item) > 2 else ()
            if entry == "on_update":
                frame(s, cbs=cbs, mark=True)
            else:
                s.op(entry, *args, cbs=cbs, mark=True)
        return s

    def running(s):
        live(s)
        memory(s)
        answers(s)

    def resting(s):
        idle(s)
        answers(s)
        snapshot(s)

    sweep("enable", None, [("on_enable", ())], tail=TAIL, enable=False)
    sweep("idle frames", resting, [("on_update", ()), ("on_update", ()), ("on_update", ())])
    sweep("running frames", running, [("on_update", (), VIEW), ("on_update", ())])
    sweep("unsafe normal view", lambda s: (running(s), snapshot(s, camera_kind=4, safe=False), frame(s)), [("on_update", (), VIEW)])
    sweep("pinned view", lambda s: (running(s), s.set("camera_is_pinned#1", True)), [("on_update", (), VIEW)])
    sweep("foreign camera", lambda s: (running(s), s.set("compat_foreign#1", True)), [("on_update", (), VIEW)])
    sweep("editor open", lambda s: (running(s), s.set("editor_is_active#1", True)), [("on_update", ())])
    sweep("zoom identity", lambda s: (running(s), s.group("helper_depth_stats", supported=True, armed=True, zoom_index=2)), [("on_update", ())])
    sweep("aim released and seated", lambda s: (running(s), snapshot(s, camera_kind=4, seated=True), s.set("mouse_button#1", 1)),
          [("on_update", ()), ("on_update", ())])
    sweep("recovery cleanup", lambda s: (running(s), s.set("recovery_action#1", "cleanup"),
                                         policies(s, ("fov", True, "fatal"), ("camera", False, "fatal"))), [("on_update", ())])
    sweep("recovery proof", lambda s: (running(s), s.set("recovery_needs_proof#1", True), policies(s, ("blur", True, "retry"))),
          [("on_update", ())])
    sweep("recovery proof refused", lambda s: (running(s), s.set("recovery_needs_proof#1", True), s.set("recovery_revalidate#1", False)),
          [("on_update", ())])
    sweep("recovery waiting", lambda s: (running(s), s.set("recovery_action#1", "wait")), [("on_update", ())])
    for core, phase, suspend in ((True, "retry", 1), (True, "fatal", 1), (True, "retry", 0), (False, "retry", 1)):
        sweep(f"policy failure {core} {phase} {suspend}",
              lambda s: (running(s), s.set("camera_update#1", False), s.set("camera_update#2", "camera refused"),
                         s.group("recovery_failure", core=core, phase=phase), s.set("has.controller_suspend", suspend)), [("on_update", ())])
    sweep("view blocked", lambda s: (running(s), s.set("recovery_blocks_view#1", True), s.set("recovery_blocks_view#2", "camera"),
                                     s.set("recovery_blocks_view#3", "cooldown")), [("on_update", (), [("menu_update", 1, "dev_status", ())])])
    sweep("respawn", lambda s: (running(s), frame(s), policies(s, ("camera", True, "retry")), snapshot(s, player_identity="P2", body=31)),
          [("on_update", ()), ("on_update", ()), ("on_update", ())])
    sweep("disable", lambda s: (running(s), frame(s)), [("on_disable", ())], tail=(("on_cleanup_poll", ()), ("on_enable", ()), ("on_update", ())))
    sweep("cleanup poll", lambda s: (running(s), frame(s)), [("on_cleanup_poll", ())], tail=(("on_cleanup_poll", ()), ("on_enable", ())))
    for name, args in BATTERY:
        if name.startswith(("fe_", "fird_", "render_", "held_", "ready_", "native_ready")):
            continue
        sweep(f"{name} {args} while running", running, [(name, args)])
    for name, args in (("start_pose", (0, None)), ("start_true_first_person", ()), ("toggle_pose", ()), ("start_native", (1,)),
                       ("preflight", ()), ("store_can_start", ()), ("start_free_aim", ()), ("probe_fird", (2,)), ("view_clean", ()),
                       ("active_view", ()), ("status", ())):
        sweep(f"{name} while idle", resting, [(name, args)])
    for name, args in STORE:
        sweep(f"{name} in camera mode", lambda s: (running(s), s.op("store_select_mode", "camera")), [(name, args)])
    sweep("select mode", running, [("store_select_mode", ("camera",)), ("store_select_mode", ("weapon",))])

    def natural(s):
        resting(s)
        s.group("arg_expected", safeNormal4=True, snapshot=Ref(90))
        s.group("ready_timing_sample", observed_idle=True, pending_batches=0, incomplete_after=0, camera_kind=4)

    def natural_on(s):
        natural(s)
        s.op("start_fird").op("ready_source_factory")
        s.set("fird_is_enabled#1", True).set("ready_pose_is_enabled#1", True)

    sweep("natural Fird Person start", natural, [("start_fird", ())])
    sweep("natural Fird Person frames", natural_on, [("on_update", (), READY), ("on_update", ())])
    sweep("natural Fird Person frame, not enabled", lambda s: (natural_on(s), s.set("fird_is_enabled#1", False), s.set("fird_is_clean#1", False)),
          [("on_update", (), READY)])
    sweep("natural Fird Person shutdown", natural_on, [("on_cleanup_poll", (), [("ready_pose_update", 1, "held_fire_authorized", (1,))])],
          tail=(("on_cleanup_poll", ()),))
    for name, args in FIRD_CALLS:
        sweep(f"natural Fird Person {name} {args}", natural_on, [(name, args)])

    def rendered(s):
        resting(s)
        lease(s)
        s.set("fird_module_native_animation_default", False).set("fird_module_render_pose_verified", True).set("backend_camera_ready", True)
        s.set("fird_aim_new#1", Ref(91)).set("fird_native_ads_new#1", Ref(92)).set("fird_fov_new#1", Ref(93))
        s.set("capture_gate_update#2", Ref(94)).set("capture_gate_scene_lens#1", 68.5).set("backend_pose_status#1", Ref(95))
        s.group("fird_reference_status", neutral=Ref(96))

    def rendered_on(s):
        rendered(s)
        s.op("start_fird")
        s.set("fird_entry_is_enabled#1", True)

    def sampled(s, steps):
        rendered_on(s)
        s.op("fe_begin_fp")
        capture(s, ref=301)
        for _ in range(steps):
            s.op("fe_sample")
            T[0] += 0.06
            s.set("clock_now#1", T[0])

    sweep("render Fird Person start", rendered, [("start_fird", ())])
    sweep("render Fird Person pending frames", lambda s: (rendered_on(s), s.set("fird_entry_is_pending#1", True), s.set("fird_warm_is_clean#1", False)),
          [("on_update", ())])
    sweep("render Fird Person calibration frame", lambda s: (rendered_on(s), s.set("fird_entry_uses_fp#1", True), s.set("controller_is_enabled#1", True)),
          [("on_update", ())])
    sweep("render Fird Person begin", rendered_on, [("fe_begin_fp", ())])
    for steps in (0, 1, 2, 3, 4):
        sweep(f"render Fird Person sample {steps}", lambda s: sampled(s, steps), [("fe_sample", ())])
    sweep("render Fird Person capture and enter", lambda s: sampled(s, 5), [("fe_capture", (Ref(94),)), ("fe_enter", (Ref(51),)), ("fe_stop", ())])
    sweep("render Fird Person running frame", lambda s: (rendered_on(s), s.set("fird_is_enabled#1", True), s.group("fird_render_result", active=True)),
          [("on_update", ()), ("render_fird_pose", ())])
    sweep("render Fird Person lost renderer", lambda s: (rendered_on(s), s.set("fird_is_enabled#1", True), s.group("fird_render_result", active=False)),
          [("on_update", ())])
    sweep("render Fird Person target changed", lambda s: (rendered_on(s), s.set("fird_is_enabled#1", True), s.group("fird_render_result", active=True),
                                                         s.set("fird_refresh_target#1", False)), [("on_update", ())])
    sweep("render Fird Person shutdown", lambda s: (rendered_on(s), s.set("fird_is_enabled#1", True)), [("on_cleanup_poll", ())],
          tail=(("on_cleanup_poll", ()),))
    for name, args in FIRD_CALLS:
        sweep(f"render Fird Person {name} {args}", lambda s: (rendered_on(s), s.set("fird_is_enabled#1", True)), [(name, args)])

    # ---------------------------------------------------------------- random sessions
    for seed, count, rate in ((1, 1200, 0.0), (2, 1200, 0.02), (3, 600, 0.05), (4, 500, 0.15), (5, 500, 0.02), (6, 500, 0.0)):
        random_session(begin(f"random {seed}", enable=False), seed, count, rate, T, Ref, frame, live, idle, answers, snapshot, lease, memory,
                       BATTERY, capture)
    return out


# Values a scripted answer may take in the random sessions, by what runtime.lua does with it.
BOOLISH = [True, True, True, False, None, "yes", 1.0]
TEXTS = ["active", "running", "idle", "waiting", "fatal", "retry", "pose_offset", "native", "cleanup", "update", None]
FIELD_DOMAIN = {
    "camera_kind": [6, 6, 4, 4, 5, None], "safe": [True, True, True, False, None], "player_identity": ["P1", "P1", "P1", "P2", None],
    "identity": ["W1", "W1", "W2", None], "body": [11, 11, 12, None], "world": [22, 22, 23, None], "seated": [None, None, True, False],
    "aiming": [True, False, None], "controller_active": [True, False, None], "weapon_ready": [True, False, None],
    "reason": [None, "blocked", 3], "record": [None, 17], "mode": ["pose_offset", "pose_offset", "native", None],
    "enabled": [True, True, False, None], "phase": ["running", "running", "active", "waiting", "idle", "fatal", None],
    "armed": [True, True, False, None], "error": [0, 0, 1, None], "owned": [False, False, True, None],
    "core": [True, False, None], "supported": [True, False], "zoom_index": [0, 1, 2.5, 40, None],
    "origin": [True, True, True, None], "alignment": [True, True, None], "helper": [True], "depth_stats": [True, True, None],
    "input_stats": [True, None], "ended": [None, None, True], "kind": ["pose", "pose", "native"],
    "origin_identity": ["W1", "W1", "W2"], "origin_player_identity": ["P1", "P1", "P2", None], "origin_body": [11, 12], "origin_world": [22, 23],
    "controller_bytes": ["\x00\x00\x00\x00\x00\x00", "\x01\x00\x00\x00\x00\x02", None], "camera": [0x30000000, None],
    "viewmodel": [77, None], "weapon_unit": [78, None], "object": [0x21000000, None], "active": [True, True, False],
    "last_error": [None, "render lost"], "neutral": [None],
}
NUMERIC = {"blend", "head_distance", "replaced", "transition_epoch", "last_native", "last_applied", "epoch", "progress", "dx", "dy", "dz",
           "x", "y", "z", "yaw", "pitch", "roll", "sequence", "samples", "skipped", "ads_rebases", "softened", "cleanup_pending",
           "wait_reason", "action", "avatar_owner", "avatar_index", "handle", "zoom_identity"}
# Answers whose odd types make runtime.lua raise LuaJIT's own arithmetic or concatenation errors
# with a wording the C does not copy; they keep their type here (see the note).
FIXED = {"clock_now#1", "backend_ptr#1", "backend_read#1", "controller_status#1", "fov_status#1", "blur_status#1", "camera_status#1",
         "dof_status#1", "near_status#1", "mouse_button_index#1", "mouse_button#1", "capture_gate_update#2", "recovery_blocks_view#2",
         "recovery_blocks_view#3", "recovery_action#1", "arm_pieces_new#1", "grip_hand_new#1", "fird_aim_new#1", "fird_native_ads_new#1",
         "fird_fov_new#1", "ready_source_new#1"}


def random_session(s, seed, count, fault_rate, T, Ref, frame, live, idle, answers, snapshot, lease, memory, BATTERY, capture):
    r = random.Random(seed)
    knobs = []
    for name in KNOBS.names[1:]:
        if name in FIXED or name.endswith(".ref") or name.startswith(("clock.", "frame.", "global", "config_table", "actions_actions", "arg_")):
            continue
        if name.startswith(("fird_capture.", "recovery_snapshot", "has.clock_now", "fird_module", "backend_camera_ready", "config.",
                            "backend_frame_active")):
            continue
        knobs.append(name)
    entries = [e.name for e in ENTRY_LIST]

    def tweak():
        name = r.choice(knobs)
        if name.startswith("has."):
            value = r.choice([1, 1, 1, 0])
        elif name.endswith(".present"):
            value = r.choice([1, 1, 1, 1, 0])
        elif "#" in name:
            default = KNOBS.defaults[KNOBS.index[name]]
            value = r.choice([default, default, default] + BOOLISH) if name.endswith("#1") else r.choice([None, None, "why", 3.0])
        elif name in ("controller_lease", "backend_alignment_lease", "backend_presentation_pose_lease", "backend_warm_lease"):
            value = r.choice([0, 1, 1, 1, 2, 3])
        elif "." in name:
            field = name.rsplit(".", 1)[1]
            if field in FIELD_DOMAIN:
                value = r.choice(FIELD_DOMAIN[field])
            elif field in NUMERIC:
                value = r.choice([None, r.uniform(-2, 2), float(r.randint(0, 9))])
            else:
                value = r.choice(BOOLISH)
        else:
            value = r.choice(BOOLISH + [None, None])
        s.set(name, value)
        if name.startswith("has.") or name.endswith(".present") or "#" in name:
            restore.append((step[0] + r.randint(1, 4), name))   # an odd answer lasts a few operations

    s.set("clock_now#1", 1.0)
    T[0] = 1.0
    s.op("on_enable")
    live(s)
    memory(s)
    answers(s)
    restore, step = [], [0]
    for number in range(count):
        step[0] = number
        for due, name in [item for item in restore if item[0] <= number]:
            s.set(name, KNOBS.defaults[KNOBS.index[name]])
        restore[:] = [item for item in restore if item[0] > number]
        if number % 40 == 39:
            answers(s)
        for _ in range(r.choice([0, 0, 1, 1, 2, 4])):
            tweak()
        roll = r.random()
        faults = [(r.randint(1, 70), r.choice([1, 2, 3]))] if r.random() < fault_rate else []
        if roll < 0.02:
            live(s)
            answers(s)
        elif roll < 0.04:
            idle(s)
        elif roll < 0.05:
            memory(s, [r.choice([r.uniform(-3, 3), 0.0, 1.0, float("nan"), float("inf"), 1e-41]) for _ in range(16)])
        elif roll < 0.07:
            snapshot(s, player_identity=r.choice(["P1", "P2", "P3"]), body=r.choice([11, 12]), world=r.choice([22, 23]))
            n = r.randint(0, 2)
            s.set("recovery_has_policies#1", n > 0).set("recovery_snapshot.count", n)
            for i, name in enumerate(r.sample(["fov", "blur", "camera", "head", "movement"], n)):
                s.set(f"recovery_snapshot.{i}.name", name).set(f"recovery_snapshot.{i}.core", r.choice([True, False]))
                s.set(f"recovery_snapshot.{i}.phase", r.choice(["fatal", "retry"]))
        if roll < 0.70:
            if r.random() < 0.1:
                s.set("mouse_button#1", r.choice([0, 1]))
            cbs = []
            if r.random() < 0.3:
                host = r.choice(["dof_update", "head_update", "controller_update", "menu_update", "editor_update", "motion_update",
                                 "aim_markers_update", "compat_update"])
                name, args = r.choice(BATTERY)
                if name not in ("on_enable",):
                    cbs.append((host, 1, name, args))
            frame(s, dt=r.choice([1 / 60, 1 / 144, 1 / 30, 0.25]), faults=faults, cbs=cbs)
        elif roll < 0.715:
            s.op(r.choice(["on_disable", "on_cleanup_poll"]), faults=faults)
            for _ in range(r.choice([0, 0, 1, 3])):     # some frames while it is down
                frame(s)
            if r.random() < 0.8:                        # and up again: everything lets go, then a new enable
                for m in METHOD_LIST:
                    if m.method in ("shutdown", "stop", "is_clean") and m.outs and m.outs[0][0] == "v":
                        s.set(f"{m.host}#1", True)
                s.set("global_get", 0).set("has.clock_now", 1).set("controller_set_placement#1", True)
                s.op("on_cleanup_poll").op("on_enable")
                live(s)
                answers(s)
        elif roll < 0.73:
            s.set("global_get", r.choice([0, 0, 0, 2]) if r.random() < 0.2 else 0)
            s.op("on_enable", faults=faults)
            s.set("clock_now#1", T[0])
        elif roll < 0.78:
            s.op("store_select_mode", r.choice(["weapon", "camera"]))
        elif roll < 0.79:
            s.set("fird_module_native_animation_default", r.choice([True, False])).set("fird_module_render_pose_verified", True)
            s.set("backend_camera_ready", r.choice([True, None]))
            s.op("start_fird", faults=faults)
        elif roll < 0.80:
            capture(s, ref=400 + r.randint(0, 3), weapon=r.choice(["W1", "W1", "W2"]), position=(0.1, r.choice([0.2, 0.2, 0.25]), 0.3))
            T[0] += r.choice([0.01, 0.06])
            s.set("clock_now#1", T[0])
            s.op(r.choice(["fe_sample", "fe_sample", "fe_begin_fp"]), faults=faults)
        else:
            name, args = r.choice(BATTERY)
            s.op(name, *args, faults=faults)
    s.op("on_cleanup_poll").op("on_disable").op("on_update")

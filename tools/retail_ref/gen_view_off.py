#!/usr/bin/env python3
"""The reference run behind tests/wire_off_test.c: what retail's runtime.lua does in a session in
which every module the view build stubs off (src/wire_off.c) answers what that stub answers.

It reuses the machinery of gen_runtime.py unchanged (the unmodified retail file under LuaJIT, every
collaborator a scripted fake, the same knobs, the same vector format) with one session of its own:
enable, idle, F11 from inside menu:update(), the rig view, aiming with the mouse edges, a weapon
swap, a seat, the debug entries that reach a stubbed module, F11 off, idle, disable. The fakes of
the wired modules (backend, controller, camera, FOV, blur, near, recovery, transition) answer the
values of ordinary play; the fakes of the stubbed modules are set to the answers of wire_off.c, and
the C test checks call by call that wire_off.c answers exactly that.

  python3 tools/retail_ref/gen_view_off.py --retail "$HOME/Worktrees/goose-gadgets/tfp-smooth/Goose Gadget Kit" --out tests/vectors
"""
import argparse
import hashlib
import struct
import subprocess
import sys
from pathlib import Path

import gen_runtime as G

OFF_EDITOR = "The placement editor is not part of the view build"
OFF_MARKERS = "Aiming markers are not part of the view build"
OFF_CAMERA_DEBUG = "Retired in the camera helper"


def off(s):
    """The knobs of the stubbed modules: src/wire_off.c."""
    for name in ("menu_fird_person_arms", "menu_show_torso", "menu_near_options", "menu_motion_options", "arm_pieces_new",
                 "grip_hand_new", "head_sdk_handles", "camera_is_pinned", "camera_look_stats", "mod_set"):
        s.set(f"has.{name}", 0)
    s.set("actions_resolve#1", G.Ref(5)).set("actions_default_placement#1", G.Ref(5))
    s.group("head_result", phase="idle").group("movement_result", phase="idle").group("dof_result", owned=False)
    for name in ("motion_result", "compat_result", "native_aim_result", "aim_markers_result"):
        s.group(name)
    s.group("backend_alignment_context", present_=False)
    s.set("dof_status#1", "off")
    s.set("editor_request_open#1", False).set("editor_request_open#2", OFF_EDITOR)
    s.set("fird_module_native_animation_default", False).set("fird_module_render_pose_verified", False)
    s.set("profile_body_sleeves", False).set("profile_grip_hand", False).set("profile_seated_head", True)
    s.set("config_table", G.Ref(9))
    s.set("camera_free_aim#1", False).set("camera_free_aim#2", OFF_CAMERA_DEBUG)
    for name in ("aim_markers_toggle", "aim_markers_set_setting"):
        s.set(f"{name}#1", False).set(f"{name}#2", OFF_MARKERS)


def build(Session, Ref):
    s = Session("view build: every stubbed module answers off")
    T = [1.0]

    def frame(**kw):
        T[0] += 1 / 60
        s.set("clock_now#1", T[0])
        return s.op("on_update", **kw)

    def snapshot(**fields):
        base = dict(camera_kind=6, safe=True, player_identity="P1", identity="W1", body=11, world=22, weapon_ready=True,
                    camera=0x30000000, viewmodel=77, weapon_unit=78, avatar_owner=5, avatar_index=2, controller_bytes="\0" * 6,
                    record=0x40000200, aiming=False, seated=False, controller_active=False)
        base.update(fields)
        for group in ("backend_snapshot", "backend_player_snapshot", "backend_pin_player_snapshot", "arg_snap"):
            s.group(group, **base)

    def idle():
        s.set("controller_is_enabled#1", False).set("controller_is_clean#1", True).set("controller_lease", 0)
        s.set("backend_alignment_lease", 0).set("backend_presentation_pose_lease", 0)
        s.group("controller_result", mode="pose_offset", enabled=False, phase="idle")
        for name in ("camera", "fov", "blur"):
            s.set(f"{name}_result.phase", "idle").set(f"{name}_result.armed", None)
        snapshot(camera_kind=4)

    def live(**fields):
        s.set("controller_is_enabled#1", True).set("controller_is_clean#1", False)
        s.group("controller_result", mode="pose_offset", enabled=True, phase="running", player_identity="P1")
        s.group("camera_result", phase="active", armed=True, error=0, head_distance=0.25, replaced=4, blend=0.5)
        s.group("fov_result", phase="active", armed=True, last_native=1.0471975, last_applied=1.134464, transition_epoch=2)
        s.group("blur_result", phase="active", armed=True)
        s.group("transition_result", epoch=2, progress=0.75)
        s.group("controller_get_placement", x=0.15, y=0.0, z=-0.12, yaw=0.0, pitch=0.0, roll=0.0)
        s.set("backend_placement_weapon_key", "5663fbf6d81eee27")
        # The lease as wire_rt.c flattens it: a pose lease whose helper has no depth_stats / input_stats.
        s.group("lease1", origin=True, origin_identity="W1", origin_player_identity="P1", origin_body=11, origin_world=22, alignment=True,
                ended=False, kind="pose", helper=True, handle=Ref(71))
        s.set("controller_lease", 1).set("backend_alignment_lease", 1).set("backend_presentation_pose_lease", 1)
        s.set("backend_presentation_pose_epoch", 2)
        snapshot(**fields)

    def waiting(**fields):
        s.group("controller_result", mode="pose_offset", enabled=True, phase="waiting", player_identity="P1",
                wait_reason="Gameplay action or camera owns the view")
        s.set("controller_lease", 0).set("backend_alignment_lease", 0).set("backend_presentation_pose_lease", 0)
        snapshot(**fields)

    s.set("clock_now#1", T[0])
    off(s)
    s.set("backend_read#1", struct.pack("<16f", 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 10.5, -3.25, 1.75, 1).decode("latin-1"))
    s.set("backend_ptr#1", 0x7FF612340000)
    s.op("on_enable")
    idle()
    frame()
    frame()
    frame()
    # F11, where retail polls it: inside menu:update(), the last call of the frame.
    TOGGLE = [("menu_update", 1, "toggle_pose", ())]
    VIEW = [("near_update", 1, "near_options", ()), ("controller_update", 1, "presentation_transition", ()),
            ("controller_update", 1, "camera_before_entry", ()), ("controller_update", 1, "action_placement", ("5663fbf6d81eee27", None)),
            ("controller_update", 1, "foreign_camera", ()), ("controller_update", 1, "clock", ())]
    frame(cbs=TOGGLE)
    live()
    frame(cbs=VIEW)
    frame()
    frame()
    # Aim in with the right button, hold, release: the release edge is the keeper's frame.
    s.set("mouse_button#1", 1)
    live(controller_active=True, aiming=True, controller_bytes="\0\0\0\0\1\0")
    frame(cbs=VIEW)
    frame()
    s.set("mouse_button#1", 0)
    frame()
    live()
    frame()
    # A weapon swap: the mask byte, the view let go, the head camera in between, then the next weapon.
    waiting(camera_kind=4, safe=False, controller_bytes="\2\0\0\0\0\0", weapon_ready=None)
    frame()
    frame()
    waiting(camera_kind=4, identity="W2", weapon_unit=79)
    frame()
    live(identity="W2", weapon_unit=79)
    s.set("lease1.origin_identity", "W2")
    frame(cbs=VIEW)
    frame()
    # A seat: kind 4, seated, once.
    waiting(camera_kind=4, safe=False, seated=True)
    frame()
    frame()
    live(identity="W2", weapon_unit=79)
    frame()
    # The debug entries that reach a stubbed module.
    for entry in ("status", "preflight", "start_fird", "edit_placement", "aiming_markers", "aim_markers_setting", "start_free_aim",
                  "dev_status", "view_enabled", "view_clean", "current_weapon", "presentation_settings", "camera_profile", "placement",
                  "camera_kick_stats", "camera_parent_stats", "camera_handoff_stats", "free_aim_stats", "observe_camera_parent",
                  "depth_stats", "input_stats", "seamless_version", "presentation_state", "recovery_status"):
        s.op(entry)
    s.op("toggle_aiming_markers", "5663fbf6d81eee27", "W2")
    s.op("set_aim_markers_setting", True)
    s.op("probe_fird", 2)
    frame()
    # F11 again.
    frame(cbs=TOGGLE)
    idle()
    frame()
    frame()
    frame()
    s.op("on_disable")
    s.op("on_cleanup_poll")
    s.op("on_update")
    return [s]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--retail", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--lua", default="luajit")
    args = parser.parse_args()
    commit = subprocess.check_output(["git", "-C", str(args.retail), "rev-parse", "--short", "HEAD"], text=True).strip()
    if commit != G.FROZEN:
        sys.exit(f"Retail is at {commit}; this project is frozen at {G.FROZEN}.")
    digest = hashlib.sha256((args.retail / "src/first_person/runtime.lua").read_bytes()).hexdigest()
    if digest != G.SHA256:
        sys.exit("runtime.lua is not the frozen file")
    sessions = build(G.Session, G.Ref)
    answers, counts = G.run(args.lua.split(), args.retail, sessions)
    raised = sum(a.startswith("!") for a in answers)
    summary = f"{len(answers)} operations in {len(sessions)} session, {sum(counts)} calls into the fakes, {raised} operations raised"
    print("view off: " + summary)
    stamp = (f"/* Generated by tools/retail_ref/gen_view_off.py. Do not edit.\n * Retail file: src/first_person/runtime.lua at commit {commit}, "
             f"sha256 {digest}.\n * {summary}.\n * The session of the view build: every module src/wire_off.c stubs answers what that stub answers. */\n"
             f'#define RT_VECTOR_COMMIT "{commit}"\n#define RT_VECTOR_SHA256 "{digest}"')
    (args.out / "view_off_vectors.h").write_text(G.vectors_header(sessions, answers, stamp))
    return 0


if __name__ == "__main__":
    sys.exit(main())

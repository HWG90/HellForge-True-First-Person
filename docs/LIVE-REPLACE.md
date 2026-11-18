# Replacing the mod while the game runs (0.6.0-feel.5)

From feel.5 the feel build declares `"live": true` (HellForge SDK 16) and exports `hellforge_mod_unload`, so HellForge
may take it out of a running game and set a new build up in its place. HellForge's side is described in the SDK's
`docs/modding/live.md` and `docs/mods.md` ("Changing mods while you play"). Nothing here has run in the game yet.

## What you do

1. Once: switch on HellForge Settings > Experimental > Change mods without restarting (`liveChanges` in
   `launcher-settings.json`; `HellForge.Cli settings --live-changes yes`). It is read when a session starts, so it
   applies from the next session. It was off on 2026-11-17.
2. Be on the ship (or the title screen), solo, with the view **off** (F11).
3. Put the new build's files in `mods/true-first-person`, then switch the mod off and on again on the launcher's Mods
   page (the SDK docs say saving a change to a mod's files also goes to the running game).
4. Read the session's `mod-goose.true-first-person.log`, `modloader.log` and `backend/live-mods.log`: they say whether
   the change was made or refused, and why.

A refused change is not an error: HellForge keeps the old build for the session and uses the new one from the next.

## What the mod does when it is taken out

`hellforge_mod_unload` (src/mod_view.c) runs on HellForge's change thread. It makes no game call, no engine call and no
helper call. It:

1. sets `closing`, which makes every later tick return at once;
2. waits up to 2 s for a tick that is in flight to finish (`in_tick`); if it does not, it refuses;
3. asks `tfp_wire_feel_held` (src/wire_feel.c) whether the mod still holds anything in the game, and refuses with one
   line naming the first thing it finds:

   | Held | Read from |
   |---|---|
   | the view is on (F11 on, running or waiting) | `controller.enabled` |
   | the view's cleanup is pending, or it has not finished switching off | `controller.phase` |
   | a native-view lease | `tfp_wire_held` |
   | a recovery row (a failed part being cleaned up or retried) | `tfp_wire_recovery_busy` |
   | the pose helper's lease | `tfp_wire_pose_held` bit 0 (`tfp_pose_adapter_owned_handle`) |
   | a look-gain value published or pending, or a private preset row in use | `tfp_wire_pose_held` bit 1 (`tfp_pose_input_held`) |
   | head visibility rows, linked arm pieces, grip hand writes | `tfp_wire_engine_held` (`tfp_head_is_clean`, `tfp_arm_pieces_is_clean`, `tfp_grip_hand_is_clean`) |
   | depth of field, vignette, aim markers, movement focus byte, native aim option | each module's `is_clean` |

4. when nothing is held: puts the dispatch slots back exactly as at the session's end (`tfp_win_restore_slots`,
   `tfp_win_pose_restore_slots`: a compare-and-swap that only acts on a slot still holding this build's hook), logs
   `Live replace: the mod held nothing in the game; N dispatch slot(s) put back`, and returns 0. `closing` stays set:
   the old DLL stays in memory (HellForge never unloads it) and does nothing more.

When it refuses it clears `closing`, logs `Refused: live replace: <what>. This build stays for the session.` and
returns 1, and the mod carries on as before.

HellForge itself removes what the mod made through its API: the game-thread callback, the session-end listener and the
two `lua51.dll` hooks of the engine bridge.

The new build's `hellforge_mod_init` does the same set-up as at boot and says which it was (`Set up live, while the
game runs` / `Set up as the game started`). Its settings are read from `settings.json` beside the new DLL.

## Not carried over

- The view's on/off state: after a replace the view is off (it had to be off to replace).
- The old build's in-memory state. The private preset rows it allocated for the look gain (64 KiB pages near the
  game's preset table) stay allocated and unused; the new build allocates its own.

## Tests

`tests/wire_feel_test.c` (run by `tests/run-wire-feel.sh`, part of `tests/run-native.sh`): nothing held for a mod that
never entered and after `on_disable`; the right sentence for the view on, waiting, cleanup pending, a lease, each
engine module and each effect; the view comes first. The export itself (the two flags, the wait, the slot restore) is
Windows code and has no native test.

## Unproven until it runs in the game

1. That HellForge accepts a **prebuilt** mod as live. The SDK docs neither say nor exclude it (they describe source
   mods being built first); the manifest has both `"live": true` and `"prebuilt": true`.
2. How a new build reaches a running session in practice. `docs/mods.md` says a session's mod files are held read-only
   while it runs and also that "saving a change to its files" goes to the running game, and that HellForge loads each
   build from its own path. Whether copying files into `mods/true-first-person` during a session is allowed and
   noticed, and whether the launcher window must be open (sessions here start from the Steam shortcut), has to be
   tried.
3. That HellForge's change thread is not holding the game thread when it calls `hellforge_mod_unload`. If it is, and a
   tick is in flight, the 2 s wait runs out and the change is refused; it cannot hang longer than that.
4. Set-up while the game runs: the engine bridge catching the Lua state at its next script call from the change
   thread's hooks, and the first tick's pin check (`read_code`) with the old build's slots already put back.
5. That nothing calls into the old build's slot hooks after they are put back (a call already inside one returns into
   the old DLL, which stays loaded).

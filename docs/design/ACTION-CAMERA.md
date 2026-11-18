# The camera during actions

Written 2026-11-18 for the request: *"Certain actions cause the camera to snap. Can we make it so actions like picking up weapons, using stims, etc use the proper camera, keep the angle I'm looking, prevent snaps, and don't allow the arms or shoulders to clip."*

Short answer: retail (Goose Gadget Kit 0.5.41-smooth.66, `1d0d453`) already has a camera for most of these actions, and all of it is translated and bound in this build. It does not cover every action, and it has cuts of its own. One part was bound differently from retail here and is fixed on this branch (the aim button). What happens for each action on build 25480438 cannot be read from the session logs written so far, because they did not record what decided each drop; this branch adds those lines.

Retail paths below are relative to the kit's `src/first_person/` unless they start with `native/`.

## 1. What retail does

There are two first-person cameras.

- **The native view** (camera request kind 6). The game's own firearm camera, sitting on the first-person rig. The mod asks for it and holds it as a lease. Arms and weapon are the first-person rig's.
- **The head camera** (camera request kind 4, the game's ordinary camera, with its position replaced). When the native view is not held, a helper replaces the *position* of every ordinary camera frame with a point at the head (`forward=.18, up=.04`, `profile.lua:72`) and leaves the game's *rotation* as it is (`native/first_person/native_ads_camera.cpp:747-1026`, here `src/camera_helper.c` `tfp_camera_setter_from`). The body is the third-person body, with head, torso and shoulder passes hidden.

The controller leaves the native view whenever the snapshot is not `safe` (`controller.lua:394-396`: "Gameplay action or camera owns the view"), and takes it again when it is. `safe` is false while the aim controller's action mask is not 0, while its guard byte is set, while seated, and while the animation state is not the verified on-foot lifecycle (`backend.lua:173`, `:215`, `:271-283`).

The head camera has a narrower gate of its own (`native_ads_camera.cpp:292-305`, here `src/camera_helper.c` `fallback_safe`):

| The aim controller says | Head camera |
|---|---|
| mask 0 | replaces the position |
| mask 2 (weapon swap) | replaces the position |
| mask 0x20 (vault) | replaces it, if a stable camera lease already existed |
| seated, mask 0 (turning a valve) | stays, with `seated_head` (cut-hold bit 8): direction glides from the view the interaction began with |
| guard byte alone, mask 0 | tolerated for a short grace time after the last first-person frame |
| **any other mask** | **forwards the game's own third-person camera** |

So, action by action:

| Action | Retail shows | Evidence |
|---|---|---|
| Weapon swap | Native view is released; head camera for the swap; the eye is held where the native eye was for up to 2.5 s (`eye_hold`), and glides to the incoming weapon's remembered eye | `backend.lua:219-220` (mask 2), `camera.lua:189`, `profile.lua:80`, helper "Kept through a weapon swap (mask 2) for at most 2500 ms" |
| Stim | The native view **stays**: mask 64 makes the snapshot unavailable, so the lease cannot be retired, and retail deliberately does not force the exit | `backend.lua:1432-1436` ("forcing the exit showed the whole stim in third person, live") |
| Reload | Native view stays (seen in this build's logs: `action=reload` with the view running); the reload placement applies | action placement `reload` |
| Aim release | The game would drop the native view for two frames; the aim-view keeper clears the controller's active byte once, on the frame the button goes up, so the request simply stays. If it is dropped anyway, `ads_anchor` pins the camera to the third-person weapon for 50 ms | `runtime.lua:535-538`, `backend.lua:1340-1362`, `profile.lua:82-89` |
| Vault | Head camera (mask 0x20), if the view was stable before | `backend.lua:221-224` |
| Dive, prone transitions, ragdoll | Native view released (not the on-foot lifecycle); head camera while the mask is 0 | `runtime.lua:55-58` |
| Turning a valve, seated interactions | Head camera held; the camera is detached from the rig once | `runtime.lua:539-542`, `backend.lua:1367-1372` |
| Expendable launcher firing (guard byte) | Head camera tolerated through the guard | `native_ads_camera.cpp:300-305` |
| Grenade, melee, stratagem ball (no firearm in hand) | No native view (it is a firearm camera); head camera while the mask allows | `controller.lua:449`, `:171` |
| **Picking up a weapon, and any action with another mask** | **The game's third-person camera.** Retail has nothing for these | the gate above |

Between the two cameras retail has **cuts, softened, not blends**:

- `cut_hold`: repeats the last native view while the camera is still attached to the rig on the way out (`profile.lua:78`).
- `eye_hold`: above.
- `ads_anchor`: above.
- `handoff` (an eased entry and an eased rotation): **switched off in retail**, `handoff={entry_ms=0,rotation_ms=0}`, with the note "neither worked live on build 26568081" (`profile.lua:74-76`). The code is there and translated (`NADSC_SetHandoff49`, `tfp_camera_set_handoff`).

### The four wishes against retail

| Wish | Retail |
|---|---|
| Use the proper camera | Yes for swap, stim, reload, vault, dive, valve, launcher. **No** for a weapon pickup and any action whose mask is not 0, 2 or 0x20 |
| Keep the angle I am looking | Yes: the head camera keeps the game's rotation, which is the aim direction; in the native view the look is the game's own |
| Prevent snaps | Partly: the position is cut, with the three holds above. The eases exist and are off because they did not work live |
| No arms or shoulders clipping | Head, torso and shoulder passes are hidden while the head camera is applied (`head_visibility.lua:283-300`), the near clip follows the weapon's profile, and in the native view the sleeves (`arm_pieces.lua`) follow the chest and collar and the hand grips place the hands (`runtime.lua:546-550`) |

## 2. What this build does

Everything in section 1 is translated and bound (`src/camera_helper.c`, `src/camera_adapter.c` with retail's profile values, `src/controller.c`, `src/backend_ops.c`; `docs/wiring/WIRING-VIEW.md`). Three things differ or are unknown.

### 2.1 The aim button came from Windows (fixed on this branch)

Retail reads the aim button from the engine, `stingray.Mouse.button` (`runtime.lua:395-404`). The view build read it from Windows (`GetAsyncKeyState`, `src/wire_win.c`), sampled at another moment than the game's own input. The keeper only works if it sees the release before the game acts on it.

Session `b4a88652` (0.6.0-feel.5, 2026-11-18): 18 aim releases, 10 kept. The other 8 show `State: left the native view: Native camera request or firearm changed` on the very frame of `State: aim out`, re-entered 10 to 20 ms later, with camera jumps of 0.24 to 0.94 m in that second.

Now: `src/wire_mouse.c` asks the engine's `Mouse.button(Mouse.button_index('right'))`, as retail does, once both functions are proven to be this build's own (`exe+0x4830c0`, `exe+0x482480`, from the live census `docs/history/research-0.5.34/live-sdk-census-g1-9472b065.tsv`). Until then, and whenever a call fails, it answers from Windows and the log says so (`Aim button: …`). No new write: the keeper's two writes are retail's and were already bound.

Not proven: that the engine's level, read in HellForge's game-thread callback (which runs before the game's update), changes on the same frame as retail sees it in the script update. The new `Aim: release not kept (aim button from engine): …` line counts the misses.

### 2.2 Sleeves and hand grips refuse at start-up

`Refused: arm pieces: value maths … largest difference 0.00425 (Quaternion.axis_angle)`. Without them the native view shows the game's own first-person arms. Not touched here; it is the open item for arm clipping in the native view.

### 2.3 Which actions drop to third person here is not known

The session logs say *that* the view was left (35 times "Gameplay action or camera owns the view", 12 times "Native camera request or firearm changed" in session `b4a88652`) and how far the camera moved in that second (typically 0.2 to 0.6 m, up to 2.2 m), but not the action mask, the guard byte, the animation layers, or whether the head camera was replacing the position. The masks in section 1 (2, 0x20, 64) are retail's; nothing has confirmed them on build 25480438, and if a stim uses another mask here, or leaves the snapshot readable, it is shown in third person.

This branch logs it (`src/wire_log.c`, filled in `src/wire_rt.c` `tfp_wire_frame`):

```
State: left the native view: Gameplay action or camera owns the view
Drop: kind=4 safe=0 engine_safe=0 local=1 blocked=1 mask=00000040 active=0 byte5=0 seated=0 swap=0 headcam=0 aiming=0 shoulder=0 reloading=0 weapon=0 reason=- | layers=1:12 3:44 … | cam=active armed=1 error=0 seen=… replaced=… forwarded=… native6=… resets=… head=0.180 blend=1.00
Wait: …            each change of the gate while waiting (7 a stretch, then one line saying more follow)
Return: …          with "State: entered the native view"
Cut: largest camera jump 0.312 m in the 0.30 s after leaving the native view      (always written, small or not)
Aim: release not kept (aim button from engine): …
```

Reading it: `mask` is the action; `replaced` rising between `Drop` and `Return` means the head camera held first person, `forwarded` rising alone means the game's third-person camera was shown; `Cut` is the size of the snap at that edge.

## 3. Proposed, not built

Each of these changes what the player sees in a way retail does not, so none is built until a session with the lines above shows what is needed. None needs a new kind of write into game memory.

1. **More action masks for the head camera.** Let `fallback_safe` accept the masks the log shows for pickups and whatever else drops to third person. What it does: the head camera replaces the position during those actions instead of forwarding the third-person camera. Writes: none new (the same replaced camera argument). Undone by: the mask leaving the list. How it could go wrong: an action that moves the head violently (ragdoll, being thrown) gives a head camera that tumbles; the head can pass through geometry or the player's own raised arm. Each mask would be added on its own, with a name, after it is seen in a log.
2. **Keeping the native view through more actions**, as retail does for the stim, where the game does not itself exit the request. Needs the log to show which actions keep kind 6 and are left only because the controller's cleanup retires the lease.
3. **The eases.** `camera_handoff(entry_ms, rotation_ms)` is bound and off, as in retail. It can be switched on for an experiment without new code; retail's note says it did not work live there, so it is an experiment, not a fix.
4. **Hiding the third-person arms when they cross the head camera.** Retail hides head, torso and shoulders only. If a pickup animation brings an arm through the head camera, the choices are a larger near clip for that action or hiding more passes; both need to be seen first.

## 4. What the next session should do

With the view on: aim and release ten times; swap weapons; use a stim; pick a weapon up from the ground; throw a grenade; vault; dive; use a terminal. Then the log answers, per action: mask, which camera, size of the cut.

## 5. What the session showed, and 0.6.0-feel.9

Session `76bf7718` (0.6.0-feel.8, 2026-11-18, about seven minutes in a mission): 49 drops of the native view. Read from the `Drop` / `Wait` / `Return` / `Cut` lines.

| What happened | Times | Seen | Camera in the gap | Jump leaving | Jump returning |
|---|---|---|---|---|---|
| Picking something up | 5 | mask 0x22, then 0x20, then 2 | **the game's third-person camera** while 0x22 (0.2 to 0.5 s), then the head camera | **2.73 to 2.84 m** | 0.24 to 0.40 m |
| Stim | 6 or 7 | mask 2 (0.4 s), 0 (0.65 s), 2 (0.3 s): always 1.46 s | head camera | under 0.02 m | **0.42 to 0.58 m** |
| Grenade | 5 | mask 2, 0 for as long as it is held, 2 | head camera | under 0.02 m | 0.28 to 0.56 m |
| Weapon swap, support weapon picked up | 5 | mask 2 | head camera | under 0.25 m | 0.25 to 0.39 m |
| Vault-like action ending crouched | 4 | mask 0x20 | head camera | under 0.06 m | 0.23 to 0.29 m |
| Leaving full aim | 6 | mask 0; the game's request changes | one frame without the native view | 0.30 to 0.37 m | 0.31 to 0.37 m |
| Dive, going prone | 5 | mask 0; the game's request changes | one frame | under 0.01 m | 0.16 to 0.82 m |
| Killed | 2 or more | camera kinds 11 and 10 follow | the game's | 1.5 to 1.7 m | not ours |

The jump is the largest change of the eye's frame-to-frame step in the 0.3 s after the edge. Which action a mask belongs to is read from timing and head height, not from a name in the game: 0x22 as "picking up" and the 1.46 s sequence as "stim" are the best reading, not a proven one.

Three things differ from retail's build.

1. **Mask 0x22 is not in retail's list** (0, 2, 0x20), so the third-person camera was forwarded. This is proposal 1 of section 3, for the one mask the log shows.
2. **A stim is not mask 64 here.** Retail's native view stays through a stim; here the stim is a put-away and a draw with empty hands between, and retail's held eye tolerates only 300 ms of empty hands. The eye let go mid-stim (a 0.45 m glide to the head) and the native view came back with a cut.
3. **The aim-view keeper never saw a release in time.** With the engine's `Mouse.button` read in HellForge's tick, all 6 releases were logged on the frame the game had already dropped the view; and `State: aim kept` appeared on 5 *presses*, on the very frame full aim began: the change of the button arrived after the game had acted on it, in both directions. 0.6.0-feel.5 (Windows' level) kept 10 of 18. So the engine's level in the tick is a frame behind the game's own input on this build; section 2.1's fix made it worse.

### What feel.9 changes

All in `src/wire_action.h` (values and the aim watch), `src/camera_helper.c` (`tfp_camera_set_action_tuning`: three opt-in values whose defaults are retail's behaviour, so the comparison with retail's source still holds call for call), `src/wire_rt.c`, `src/engine.c` (a probe at `lua_pcall`), `src/mod_view.c`.

- **Pick-up in first person.** The head camera accepts mask 0x22 as it accepts a vault's 0x20, and head, torso and shoulders hide with it. The direction stays the game's own, which is where the player looks. The head camera follows the head, so the view dips as the body bends down.
- **Eye held through a stim.** The tolerance for empty hands is 900 ms instead of 300, so the eye stays where the native eye was for the whole 1.46 s.
- **Glide back before the weapon returns.** When the eye has let go (a grenade held longer than 900 ms) and a weapon whose eye is remembered is being drawn (mask 2), the eye glides to that remembered eye (70 ms time constant, retail's swap glide), so the native view returns to a camera that is already near it.
- **Aim button.** Windows' level first again. Read once more when the engine's script update begins (the first `lua_pcall` on the engine's state after the tick, at most three times a frame, only while full aim is on in the native view): if the button changed there, the keeper's own two writes are made at once, on the tick's snapshot, after comparing the controller's bytes with it. And no keeping in the first 150 ms of full aim: a change seen then is the press that raised the weapon.
- **Log.** `State: aim kept (n; m of them at the script call, k late presses ignored)`; `Entry: the eye moved … (level, up)` after each return, from the helper's own record of the first native frames.

No new kind of write: the keeper's two (retail's), from one more place in the frame.

### Not verified, and what is left

- None of this has run in the game. Whether the script-call read sees the button before the game acts is the open question; the `aim kept` line counts it.
- **The cut on return is reduced, not removed.** Even a plain swap, where the eye is held the whole time, returned with 0.25 to 0.39 m. The held eye follows the third-person head; the native eye sits on the first-person rig; they differ by the head's own movement in the animation. The `Entry` line gives its direction. Removing it needs either the eased entry (proposal 3; retail found it did not work live on its build) or holding the eye relative to the body instead of the head.
- **A vault-like action and a dive** return with 0.2 to 0.8 m: the body's height changed in between. Not touched.
- **Arms.** In the gaps the arms are the third-person body's, seen from the eye. The sleeves and hand grips that cover the native view's arms still refuse at start-up (section 2.2). Moving the third-person arms into view was tried in retail on 2026-11-12 and judged bad (arms sweep through the lens during a swap).

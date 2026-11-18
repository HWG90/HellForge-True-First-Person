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

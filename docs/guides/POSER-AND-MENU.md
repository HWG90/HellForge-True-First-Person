# The settings menu, saving, and the placement editor (0.6.0-feel.7)

Three things retail has that the first HellForge builds lacked. All three are built as of 0.6.0-feel.7 (the editor's eye-camera mode is not). The menu and saving ran in no game yet, nor has the editor: none of it has run in the game yet.

| Part | State in 0.6.0-feel.7 |
|---|---|
| Saving settings | Built. `src/wire_settings.c` (when), `src/mod_view.c` (the file) |
| Settings menu | Built. DBF-MCM when it is loaded, else the mod's own panel (round 15). `src/wire_menu.c` (the one list), `src/mod_menu.c`, `src/wire_panel.c` |
| Placement editor | Built for the weapon and arms. `src/placement_editor.c` (retail's editor), `src/wire_editor.c` (binder), `src/mod_editor.c` (the panel) |

## Saving

- **Where:** `%LOCALAPPDATA%\Goose\HellForge\TrueFirstPerson\settings.json` (placements and menu values) and `locked_view.json` beside it (the Locked view tab's options, since 0.6.0-feel.25). HellForge holds a session's mod files read-only, so the mod cannot write beside its DLL.
- **Reading:** that file is read first. Until it exists, the `settings.json` shipped beside the DLL is used (the placements and menu values imported from the retail mod, `import/`).
- **When:** only when something changed. The store counts every change (`mod.set`, a menu control); the tick compares that count with what was last written and writes once per change. Nothing is written per frame or on a timer.
- **How:** the whole file to `settings.json.new`, flushed, then renamed over `settings.json`; `locked_view.json` the same way, and only when one of its options changed. Game thread only.
- **Why two files:** 0.6.0-feel.19 and older refuse a `settings.json` with a member they do not know and then save their defaults over it. So `settings.json` only ever holds `first_person`, `format` and `menu`, and anything newer goes into a file of its own. In `locked_view.json` a name the build does not know is skipped with one log line, and so is a member beside `locked_view`; such a file was written by a later build, and before this build first writes it the file as it is is copied to `locked_view.json.newer` (`.newer-2` to `-9`).
- **Conversion:** a `settings.json` that holds `locked_view` (written by 0.6.0-feel.20 to feel.24) is converted when the mod starts, not at the next change: `locked_view.json` is written from it, the file is copied to `settings.json.before-feel25` (or to the first free of `.before-feel25-2` to `-9` when that name is taken; a taken name is never written over, and all nine taken refuses the conversion until some are moved away), and `settings.json` is written again without the member. When `locked_view.json` is already there, an older build has written the member since: the member is the newer one, the options it names take its values, and where that changes a value the `locked_view.json` as it was is copied to `locked_view.json.replaced` (`.replaced-2` to `-9`) before it is written again. A step that fails logs one `Refused:` line; the session uses the loaded values and the next start tries again. While all nine names are taken, no save of that `settings.json` goes through either.
- **A refused file is kept:** when a file could not be read (the log says `Refused: settings: …`), the first save of the session first copies it to `settings.json.refused` (`.refused-2` … `.refused-9` when the name is taken; `locked_view.json.refused` for the other file). If the copy cannot be made the save fails.
- **A file that did not open is kept:** when a file is there but could not be opened at the start (another program holds it), the mod runs on the file beside the DLL or the defaults, and the first save of the session first copies the file to `settings.json.unread` (`.unread-2` … `.unread-9`; `locked_view.json.unread` for the other file). If the copy cannot be made the save fails. With no file there nothing is copied.
- **One copy per file:** for every numbered copy above, a copy that already holds the same bytes is the one kept and no new name is taken. A save that keeps failing therefore leaves one copy, however often the mod is started, and the nine names are only used up by nine different files.
- **A failed write** is tried once per change, not once a frame. The log says `Refused: settings were not saved: …`, the session keeps its values, and a live replace is refused while a change is unsaved.
- **Consequence:** once your own file exists, a newer build's shipped `settings.json` no longer applies. Delete your file to take the shipped one again.

## The settings menu: DBF-MCM first, the mod's own panel without it (round 15)

Built after 0.6.0-feel.50, not yet run in the game. One list of every option, `src/wire_menu.c` (`TfpMenuControl`), is the single source of both menus, so both show the same three pages with the same options: **First person**, **Bob and sway** and **Locked view**. Tests: `tests/run-wire-settings.sh` (the list, the MCM definition through MCM's own schema parser) `tests/run-wire-panel.sh` (the panel, and which menu a session gets) and `tests/run-mod-menu.sh` (the glue to MCM: while it holds the menu, the once-a-second re-ask and the per-update menu frame call nothing in MCM).

### Which menu you get

| Situation | Menu | F10 |
|---|---|---|
| DBF-MCM (`goose.mcm`) loaded and ready within 15 s of the mod's start | DBF-MCM, page "True First Person" (DEL opens MCM as always). No panel of the mod's own is made | Opens MCM on this mod's first page; closes MCM when it is open (not while MCM is taking a key or text) |
| DBF-MCM not loaded, not ready within 15 s, or refuses the registration | The mod's own panel through NEST (Goose Overlay Host, API 6 or newer), with its own 15 s search | Opens and closes the panel (the host's hotkey; also in the host's panel list, Page Down) |
| `panel_on.txt` beside the DLL (test file) | The mod's own panel at once; DBF-MCM is not tried | As above |
| Neither DBF-MCM nor NEST | None. One log line says so and that `settings.json` and `locked_view.json` in `%LOCALAPPDATA%\Goose\HellForge\TrueFirstPerson` can be edited by hand while the game is closed | Nothing |

- The search: DBF-MCM is asked once a second (its API answers only after its first update, so it may come after this mod). Without an answer after 15 s, one line (`Menu: DBF-MCM (goose.mcm) did not answer within 15 seconds ...`) and the panel's search begins. An MCM that comes later than that changes nothing for the session.
- `mcm_on.txt`, the switch of builds up to 0.6.0-feel.50, is still read and changes nothing (one `Test:` line): MCM is the default now.
- While MCM holds the menu it is asked again once a second; that is what makes a registration released for a refused live replace come back. If MCM then refuses it, the panel takes over. While the registration stands, the re-ask is answered inside the mod (`tfp_mod_menu_try` returns at once): MCM is not called, so its page, focus and scroll stay as the player left them.
- In MCM every choice (Camera style, Motion preset) is a selector, `< Stable >`: one click on an arrow, or Left/Right on the focused row, changes it. There is no drop-down.

### Where a value lives

Only in this mod's two files: `settings.json` (the `menu` member, and `first_person` for Show aim markers and Use weapon-family placement defaults, which the runtime saves there as retail does) and `locked_view.json`. A change in either menu goes through the same setters (`tfp_wire_settings_menu_set`, `tfp_wire_settings_locked_set`, or the runtime for the two `first_person` ones), on the game thread, takes effect live, and is written by the next save frame, once per change.

- **DBF-MCM keeps no file for this mod.** The registration says `"storage": true`; its load answers "no values", so every control starts at the definition's default, which is the store's value, and its save does nothing (the store saves). MCM's reset therefore goes back to the value the menu was registered with, not to retail's factory default.
- **The MCM id is `goose_tfp`.** Not `goose_true_first_person` (an old Lua experiment's page, whose saved values MCM may still hold), not `true_first_person` (the id builds up to feel.50 used behind `mcm_on.txt`, with two pages) and not `first_person` (retail's). With `"storage": true` MCM reads no `<id>.ini` and imports none from LLL's old folder anyway; the new id also keeps any old registration apart.
- **The panel** is the menu's host as MCM is: what the user does is queued by the host's render thread and applied by the game thread at its next update. A slider is taken when it is let go.

### Every option

Defaults are the store's defaults (retail's, and the locked view's); on a first run the shipped `settings.json` holds your imported retail values instead.

| Page | Option | Kind | Range / choices | Default | Stored in |
|---|---|---|---|---|---|
| First person | Enable True First Person | button |  |  | - |
| First person | Turn first-person view off | button |  |  | - |
| First person | Show aim markers | checkbox |  | as saved | settings.json first_person.show_aim_markers (through the runtime) |
| First person | Edit arms and weapon placement | button |  |  | - |
| First person | Open / close placement editor | key |  | Page Up (stored 119, retail's F8) | settings.json menu.editor_key |
| First person | Toggle True First Person | key |  | U (a stored 122, F11, reads as U) | settings.json menu.toggle_key |
| First person | True First Person: native ADS | checkbox |  | off | settings.json menu.true_native_ads |
| First person | True First Person: show torso | checkbox |  | off | settings.json menu.show_torso |
| First person | First Person Arms | checkbox |  | on | settings.json menu.fird_person_arms |
| First person | Use weapon-family placement defaults | checkbox |  | as saved | settings.json first_person.family_placement_defaults (through the runtime) |
| First person | Motion preset | choice | Custom (keep existing sliders) / Comfort / Natural | Custom (keep existing sliders) | settings.json menu.motion_preset |
| First person | Weapon movement | toggle |  | on | settings.json menu.motion_enabled |
| First person | Weapon movement strength | slider | 0 to 200, step 5 | 100 | settings.json menu.motion_strength |
| First person | Weapon movement: walking bob | checkbox |  | on | settings.json menu.motion_bob_enabled |
| First person | Weapon movement: sway | checkbox |  | on | settings.json menu.motion_sway_enabled |
| First person | Weapon movement: sprint pose | checkbox |  | on | settings.json menu.motion_sprint_enabled |
| First person | Near clip distance (cm) | slider | 5 to 40, step 1 | 15 | settings.json menu.near_clip_cm |
| First person | Near clip while aiming | toggle |  | off | settings.json menu.near_clip_aim |
| First person | Weapon: no active firearm | status line |  |  | - |
| First person | This weapon: hip clearance (cm) | slider | 5 to 40, step 1 | 15 | settings.json menu.weapon_near_hip_cm |
| First person | This weapon: normal-aim clearance (cm) | slider | 5 to 40, step 1 | 5 | settings.json menu.weapon_near_aim_cm |
| First person | Save clipping for this weapon | button |  |  | - |
| First person | Use global clipping for this weapon | button |  |  | - |
| First person | Status: First person off | status line |  |  | - |
| Bob and sway | Weapon bob strength | slider | 0 to 300, step 5 | 100 | settings.json menu.weapon_bob_strength |
| Bob and sway | Weapon bob speed | slider | 25 to 200, step 5 | 100 | settings.json menu.weapon_bob_speed |
| Bob and sway | Weapon sway strength | slider | 0 to 300, step 5 | 100 | settings.json menu.weapon_sway_strength |
| Bob and sway | Weapon sway response speed | slider | 25 to 200, step 5 | 100 | settings.json menu.weapon_sway_speed |
| Bob and sway | Sprint weapon movement | slider | 0 to 200, step 5 | 100 | settings.json menu.sprint_motion_strength |
| Locked view | Camera lock | toggle |  | on | locked_view.json camera_lock |
| Locked view | Field of view lock | toggle |  | on | locked_view.json fov_lock |
| Locked view | Field of view (degrees) | slider | 50 to 120, step 1 | 80 | locked_view.json fov_degrees |
| Locked view | Aim field of view | choice | The game's zoom / Your locked view / A chosen value | The game's zoom | locked_view.json fov_aim (1, 2, 3); fov_zoom_when_aiming is kept in step for older builds |
| Locked view | Aim field of view (degrees) | slider | 20 to 90, step 1 | 50 | locked_view.json fov_aim_degrees; greyed unless A chosen value |
| Locked view | Sight distance in aim (cm) | slider | 0 to 30, step 1 | 0 | locked_view.json ads_sight_cm; 0 (the default since round 20c) is the game's own seat, as feel.51 |
| Locked view | Camera style | choice | Stable / Bodycam | Stable | locked_view.json bodycam |
| Locked view | View kick strength (%) | slider | 0 to 100, step 5 | 35 | locked_view.json kick_strength |
| Locked view | View kick limit (degrees) | slider | 0 to 20, step 1 | 8 | locked_view.json kick_max_degrees |
| Locked view | View kick when thrown (%) | slider | 0 to 100, step 5 | 70 | locked_view.json thrown_kick_strength |
| Locked view | Camera damping (%) | slider | 0 to 100, step 5 | 60 | locked_view.json camera_damping |
| Locked view | Animated arms | toggle |  | on | locked_view.json animated_arms |
| Locked view | One-handed pistols | toggle |  | on | locked_view.json one_handed_pistols |
| Locked view | Weapon put-away | toggle |  | on | locked_view.json put_away |
| Locked view | Smooth eye | toggle |  | on | locked_view.json smooth_eye |
| Locked view | See through the exosuit cockpit | toggle |  | on | locked_view.json see_through_cockpit |

Notes on the list:

- **First Person Arms** (retail's label was the typo "Fird Person Arms"; the stored key keeps retail's name `fird_person_arms`) is shown disabled, with the line "Sleeves and hand grips are refused on this build." in the panel and one log line, when sleeves and hand grips refused at start-up (they do on current builds).
- **Open / close placement editor** and **Edit arms and weapon placement** are disabled when NEST is not loaded (the editor needs it). The editor key's default is Page Up: F8 is HellForge's own editor, and a stored 119 is read as Page Up. The panel's key row does not take F8 for it.
- **Toggle True First Person**: the default is U. It was F11 (retail's default, stored as 122); HellForge 0.5's Armor LUT editor now opens on F11, so a stored 122 is read as U: a player who never changed the key gets U with no file rewritten, and a key you chose is kept. F11 cannot be chosen for it: the panel's key row says so and keeps asking, and in DBF-MCM the choice is put back with one `Menu:` line in the log. The log's `Keys:` line at the start names the key in use.
- **Camera style** is the Boolean `bodycam` of `locked_view.json` (Stable is off). While it is Stable, View kick strength, View kick limit and Camera damping are disabled (Bodycam only); the panel says so under the row, MCM at the end of the description.
- **One-handed pistols** is stored and nothing acts on it: always disabled, with its note.
- **Animated arms**, **Weapon put-away** and **Smooth eye** switch a feature while the game runs (`src/wire_options.c`): the change is made at the next update, and waits while a lease is held through an action. Test files beside the DLL beat the option: `arms_live.txt` / `hold_lease.txt` force on, `arms_off.txt` / `hold_off.txt` / `smooth_off.txt` force off (off wins); the `Option:` lines say which decided.
- **Show aim markers** and **Use weapon-family placement defaults** go through the runtime (retail's aim-marker setting and `save_presentation`) at the next tick; a refusal is logged and the control is put back.
- **This weapon: hip / normal-aim clearance** are drafts that follow the weapon in your hands; **Save clipping for this weapon** writes them to `near_clip_profiles`, **Use global clipping for this weapon** removes the weapon's row. They are disabled without a firearm.
- **The two status lines** are text in the panel and always-disabled buttons in MCM (MCM relabels only a control it can find by id).
- **Buttons answer in the log** (`Menu: "<label>": <why>`), not in MCM's footer: they are done at the mod's next tick.
- **Not offered:** retail's "Free aim (30 seconds)" and "Compare native ADS camera (30 seconds)", test tools of modes this build does not wire.

Keys of the two files that are no option (`tfp_wire_menu_not_offered`; the test checks that every key of both files is a control's or one of these, once):

| File | Key | Why it is no option |
|---|---|---|
| settings.json first_person | `placement` | the global weapon placement; written by the placement editor |
| settings.json first_person | `weapon_placements` | older per-weapon placements; read as a fallback, written by no control (the editor writes action_placements) |
| settings.json first_person | `action_placements` | per-weapon, per-action placements; written by the placement editor |
| settings.json first_person | `action_state_mapping` | which game state is which action (idle, aim, sprint, ...); data the runtime reads, not a choice |
| settings.json first_person | `fird_head_camera` | the eye-camera placement; its editor (retail's Shift+F8) is not in this build |
| settings.json first_person | `motion_enabled` | retail's runtime default for Weapon movement; the control's own value always wins, so it changes nothing |
| settings.json first_person | `motion_scale` | retail's fallback for Weapon movement strength, used only while the stored strength is outside 0 to 200 |
| settings.json first_person | `camera_kick_enabled` | retail's own camera kick switch; no unit of this build reads it (the locked view's kick sliders replace it) |
| settings.json first_person | `aim_markers` | per-weapon aim-marker choices, saved by the runtime; Show aim markers is the control |
| settings.json first_person | `near_clip_profiles` | per-weapon near clip, written by Save clipping for this weapon / Use global clipping for this weapon |
| settings.json first_person | `grab_offset` | kept from older files; no unit of retail or of this build reads it |
| settings.json first_person | `grab_offsets` | kept from older files; no unit of retail or of this build reads it |
| settings.json menu | `ads_mode` | retail's control is commented out (Fird Person ADS behaviour); only its free-aim test tool, not in this build, reads it |

### The panel itself

- One NEST window, "True First Person", a tab per page; colours from the host's theme tokens only (`ov->theme()`, HD2-UI-Style/DESIGN.md). The first button, Enable True First Person, is the window's one main button.
- **Keys:** the host has no key-binding widget. A key row is its label, the key as a key cap, and **Change**, which takes the next key pressed (Esc keeps the old one, Delete clears it; after ten seconds the question goes away).
- A greyed row still shows its description as a tooltip (an unseen item of its rectangle carries it).
- **The placement editor** closes the settings menu, panel or MCM, before it takes the cursor.
- **Live replace:** the panel stays registered with the host, closed and drawing nothing (the host cannot take a panel out). Its id is `goose.true-first-person/settings` at every game start, so the host keeps its place and size and `hotkey.goose.true-first-person/settings` in `overlay-host.ini` moves its key; the build that replaces one registers as `goose.true-first-person/settings@<process>-<tick>` for the rest of that session (one log line).

### Only the game can show

1. That DBF-MCM takes the definition in the game (natively it passes MCM's own schema parser), with `"storage": true`, and that its load and save callbacks are called as its source says.
2. That F10 reaches the mod while MCM's window is open (MCM may hold the keyboard), and that `focus_page` opens on the First person page.
3. That the Bodycam-only sliders grey and un-grey in MCM when the camera style changes (`set_disabled` from the mod's tick).
4. The panel on the host, as before: sizes, the three tabs, the key rows.
5. A live replace while MCM holds the menu: the registration taken out on the game thread and made again by the new build under the same id.

## Live replace

- The registration is taken out of DBF-MCM on the game thread (MCM refuses it from any other). `hellforge_mod_unload` asks, the next tick checks that nothing is held, unregisters and answers; with no tick within two seconds a registered menu refuses the replace.
- DBF-MCM's header says it does not support live mods. That is about a registration it cannot take out; this build takes its own out first. Unproven until tried.

## The placement editor

Retail's editor (`src/first_person/placement_editor.lua`) translated to C and compared with the unmodified Lua (`tests/run-vectors-placement-editor.sh`): results, state, the order of every call it makes and every item it draws. All 20,094 recorded operations (103,351 calls) are compared. Page Up and Page Down are not depth keys in the mod (round 17, below); the comparison builds the editor with retail's two depth keys and help line put back (`TFP_ED_RETAIL_PAGE_KEYS`, set only by that script), and `tests/run-editor-keys.sh` checks the shipped editor has neither. The panel, the cursor and the mouse come from Goose Overlay Host; the editor needs it loaded.

**Open and close:** Page Up (the menu's "Open / close placement editor" key), or the menu's "Edit arms and weapon placement" button. Opening closes the settings menu first and switches first person on if it was off. F8 is HellForge's own editor and is not used.

**What it edits:** the placement of weapon and arms for the weapon in your hands and one action at a time.

**In full aim (round 20):** opened while aiming, the editor begins on the ADS action and the log says so. What you drag
and scroll then moves what full aim draws, live: the ADS placement, on top of the Sight distance in aim if one is set
(the wheel moves the sight toward you or away). Saving keeps it for this weapon's ADS action; cancel puts the saved
one back at once; the hip placement is left alone. Before round 20 the editor opened on idle and its preview went to
the hip placement, which full aim does not draw, so nothing moved while aiming. Whether the game keeps the aim while
the editor has the cursor is not yet known; the `Aim:` line says whether the editor was open in an aim
(`editor open on "ads"`). See `docs/design/specs/2026-11-22-locked-view-design.md` "Round 20" and "Round 20c", and
`docs/guides/AIM-TEST.md` for the game run that is to settle it.

**Sight distance in aim (round 20c):** 0 by default, which is the game's own seat (feel.51). Set it (10 to 15 cm is
the range tried offline) and full aim pulls the gun toward the eye until the sight the game sets the near clip to is
that far away; the near clip moves in with it. At a zoom-level change the pull gives way only as fast as the game's
own depth moves, so the gun does not jump. How the game moves its sight between zoom levels has not been logged yet,
which is why it is off.

**Test file `zoom_steady.txt` (round 20c):** beside the DLL, read at the start. It switches on the zoom-shake
corrections in full aim: the rig's depth held at full strength, and with Aim field of view "The game's zoom" and the
lock on, the field of view held at the optic's own zoom while the unzoom gate flips. Without it full aim is feel.51's.
A `Test:` line says when it is on. The `Aim:` line is the same either way.

| Input | Effect |
|---|---|
| `<` `>` buttons | The action being edited (idle, aim, ADS, sprint, …). Switching discards unsaved edits of the action you leave |
| Left drag (outside the panel) | Right / left and up / down |
| Right drag | Yaw and pitch |
| Middle drag | Roll |
| Wheel | Forward / back (depth) |
| Arrow keys | Right / left and up / down |
| Shift | Fine steps (a tenth) |
| Save weapon / Enter | Saves this action for this weapon and closes |
| Save all | Saves this action for all weapons and closes |
| Cancel / Esc / Page Up | Closes and puts the placement back |
| Reset | Previews the default for this weapon and action; save to keep it |
| Reset all | Clears every saved placement the editor manages (retail's rule), at once |

Limits are retail's: 50 cm on each axis, 90 degrees on each angle. A weapon swap, the first-person view ending, the settings menu opening or the game losing focus cancels the edit and puts the placement back.

**Saving:** a save goes through the same store as the menu, so the file is written once, on the save.

**How rotation is shown (fixed in round 16, after 0.6.0-feel.50):** moving and rotating take different roads. A move goes to the controller and the pose helper, which take x, y, z only. Yaw, pitch and roll are drawn by the motion runtime, as in retail: while the editor is open it asks the editor for the draft's rotation and hands it to the pose helper as the motion rotation, on the rig node that carries the arms and the gun (`src/arm_graft.h`, `tfp_graft_view_node`). In play it adds the saved placement's yaw, pitch and roll to its own bob and sway. Up to feel.50 the motion runtime was never given the editor (`src/wire_motion.c` had both editor hooks empty). A right or middle drag changed the numbers on the panel, but nothing on screen moved until the placement was saved and used in play. Now `src/editor_motion.h` hands the editor over, and `tests/run-editor-rotation.sh` drags each axis through the real editor and motion runtime and checks that the drawn rotation changes during the drag, that the save keeps it, and that it is drawn in play. As in retail, bob and sway pause while the editor is open: only the edited rotation is drawn.

**How it is drawn:** the editor lays out its own panel (retail's geometry, top left of the screen) and hit-tests its own buttons. `src/mod_editor.c` draws those rectangles and texts with the overlay host's theme tokens (`ov->theme()`), shaped per `HD2-UI-Style/DESIGN.md` (panel rounding 10 with a 1 px line border, buttons rounding 6, hover fill under the pointer). It holds no placement logic.

**While it is open** the overlay host has the game's keyboard and mouse, as retail's captured input did: you cannot move or look around.

### Not built

- **The eye-camera editor** (retail's Shift+F8). It needs the head positioner, which this build leaves off; the editor only opens in its weapon mode.

### Live replace and the panel

- A live replace is refused while the editor is open, opening or closing.
- Overlay Host cannot take a panel out and refuses a second one with the same id. Each load of the mod therefore registers its panel under an id of its own, and a replaced build closes its panel and draws nothing from then on. Its entry stays in the host's panel list (F7) as a dead "True First Person: placement" until the game restarts.

### Decisions you may want different

- **Page Up and Page Down are not depth keys** (round 17, at the user's request). In retail, holding either moved the weapon forward or back while an action other than idle was edited, and the help line read "Wheel: depth   Arrows: move   PgUp/PgDn: depth". Here Page Up is the editor's own open/close key and Page Down opens the overlay host's panel list, so both clashed. Depth is the wheel's alone (Shift for fine steps); the help line reads "Wheel: depth   Arrows: move", and the editor no longer reads either key (`src/placement_editor.c`, `src/mod_editor.c`). `tests/run-editor-keys.sh` holds each for a second during an action edit and checks the placement does not move, and that the wheel and the arrows still do. The retail comparison still checks every recorded call: it compiles the two keys back in for the test only (`TFP_ED_RETAIL_PAGE_KEYS`), and `tests/run-editor-keys.sh` also checks that the shipped source binds neither and that nothing but the comparison sets the switch; retail's vector file is unchanged.
- The panel sits where retail puts it and is not movable.

## Unproven until it runs in the game

1. That `%LOCALAPPDATA%` is writable from the game under HellForge's sandbox, and that the rename replaces the file there.
2. That DBF-MCM accepts the definition in game. Natively it passes MCM's own schema parser (`tests/run-wire-settings.sh`, when the MCM source is beside this repository).
3. That this mod's tick and MCM's update are the same thread (MCM refuses calls from any other).
4. `set_label` and `set_disabled` on the status rows, and the per-weapon drafts following the weapon.
5. Unregistering during a live replace.
6. The editor in the game, all of it: that the overlay host's window gives the cursor and the mouse as the panel expects (buttons and wheel are read in the host's draw callback on the render thread and handed to the game thread), that the first frame arrives before the editor's first input sample, that `Window.mouse_focus()` / `show_cursor()` answer true / false when it opens, text sizes and positions against the panel, and the key poll while the host holds the keyboard.
7. That the editor's yaw, pitch and roll turn the drawn gun and arms in the game, live while dragging, in the direction you drag. The test above follows the rotation only up to what the pose helper is handed. Whether the rig turns the way the drag suggests, and what the arm drive's put-back after each draw does to it, can only be seen in the game.
8. `%+.1f` number texts are formatted by the C library of the build (zig's), compared natively with glibc only.

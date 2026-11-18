# The settings menu, saving, and the placement editor (0.6.0-feel.7)

Three things retail has that the first HellForge builds lacked. All three are built as of 0.6.0-feel.7 (the editor's eye-camera mode is not). The menu and saving ran in no game yet, nor has the editor: none of it has run in the game yet.

| Part | State in 0.6.0-feel.7 |
|---|---|
| Saving settings | Built. `src/wire_settings.c` (when), `src/mod_view.c` (the file) |
| Settings menu | Built, on DBF-MCM for HellForge. `src/wire_menu.c`, `src/mod_menu.c` |
| Placement editor | Built for the weapon and arms. `src/placement_editor.c` (retail's editor), `src/wire_editor.c` (binder), `src/mod_editor.c` (the panel) |

## Saving

- **Where:** `%LOCALAPPDATA%\Goose\HellForge\TrueFirstPerson\settings.json`. HellForge holds a session's mod files read-only, so the mod cannot write beside its DLL.
- **Reading:** that file is read first. Until it exists, the `settings.json` shipped beside the DLL is used (the placements and menu values imported from the retail mod, `import/`).
- **When:** only when something changed. The store counts every change (`mod.set`, a menu control); the tick compares that count with what was last written and writes once per change. Nothing is written per frame or on a timer.
- **How:** the whole file to `settings.json.new`, flushed, then renamed over `settings.json`. Game thread only.
- **A failed write** is tried once per change, not once a frame. The log says `Refused: settings were not saved: …`, the session keeps its values, and a live replace is refused while a change is unsaved.
- **Consequence:** once your own file exists, a newer build's shipped `settings.json` no longer applies. Delete your file to take the shipped one again.

## The menu

It needs DBF-MCM for HellForge (`goose.mcm`, opened with DEL). The mod looks for it once a second for 15 seconds after it starts; without it the log says so once and the saved settings apply.

Controls, labels, ranges, steps and descriptions are retail's (`src/first_person/menu.lua:343-418`), on two pages: **First person** and **Bob and sway**.

| Control | What it does here |
|---|---|
| Enable True First Person / Turn first-person view off | The same as the toggle key |
| Show aim markers | Retail's global aim-marker setting; refused and put back if the markers module is off |
| Edit arms and weapon placement, Open / close placement editor (key) | Opens and closes the editor (below). **Disabled** when Goose Overlay Host is not loaded |
| Toggle True First Person (key) | The key the mod listens for. Default F11; unbound means no key |
| True First Person: native ADS, show torso | Stored and read by the translated units as in retail |
| Fird Person Arms | **Disabled when sleeves and hand grips refused at start-up** (they do on current builds); the log says so |
| Use weapon-family placement defaults | Saved through retail's `save_presentation` |
| Motion preset, Weapon movement and its strength, bob, sway, sprint pose | Stored; the motion unit reads them as retail's `menu:motion_options()` |
| Near clip distance, Near clip while aiming | Stored; read as retail's `menu:near_options()` |
| This weapon: hip / normal-aim clearance, Save clipping, Use global clipping | Drafts per weapon, saved to `near_clip_profiles` through `save_presentation` with retail's weapon check |
| Weapon: …, Status: … | Two always-disabled buttons whose label changes (see differences) |
| Bob and sway page | The five strength and speed sliders, step 5 |

### Differences from retail

- **The editor key's default is Page Up**, not F8: F8 opens HellForge's own editor on this install. A stored 119 (retail's default) is read as Page Up.
- **Not offered:** "Free aim (30 seconds)" and "Compare native ADS camera (30 seconds)". They are retail's test tools for modes this build does not wire.
- **The two status rows are disabled buttons**, not text: DBF-MCM for HellForge can relabel only a control it can find by id, and a text block is not one.
- **Defaults:** each control's default is the value in `settings.json` when the menu registers (on a first run, your retail values). MCM's "reset" goes back to that, not to retail's factory default.
- **Buttons answer in the log**, not in MCM's footer. MCM calls back from its own update, outside this mod's tick; anything that reaches the runtime (the buttons, the two config toggles) is kept and done at the mod's next tick, and a refusal is logged as `Menu: "<label>": <why>`. Plain values (sliders, checkboxes backed by the store) apply at once.

### Where a value lives

MCM keeps each control's value in its own file once you change it. After registering, the mod reads every value back from MCM into its store, and every later change goes into the store too, so the two agree and the mod's own file holds the same values.

## Live replace

- The registration is taken out of DBF-MCM on the game thread (MCM refuses it from any other). `hellforge_mod_unload` asks, the next tick checks that nothing is held, unregisters and answers; with no tick within two seconds a registered menu refuses the replace.
- DBF-MCM's header says it does not support live mods. That is about a registration it cannot take out; this build takes its own out first. Unproven until tried.

## The placement editor

Retail's editor (`src/first_person/placement_editor.lua`) translated to C and compared with the unmodified Lua on 20,094 operations (`tests/run-vectors-placement-editor.sh`): results, state, the order of every call it makes and every item it draws. The panel, the cursor and the mouse come from Goose Overlay Host; the editor needs it loaded.

**Open and close:** Page Up (the menu's "Open / close placement editor" key), or the menu's "Edit arms and weapon placement" button. Opening closes the settings menu first and switches first person on if it was off. F8 is HellForge's own editor and is not used.

**What it edits:** the placement of weapon and arms for the weapon in your hands and one action at a time.

| Input | Effect |
|---|---|
| `<` `>` buttons | The action being edited (idle, aim, ADS, sprint, …). Switching discards unsaved edits of the action you leave |
| Left drag (outside the panel) | Right / left and up / down |
| Right drag | Yaw and pitch |
| Middle drag | Roll |
| Wheel | Forward / back (depth) |
| Arrow keys, Page Down | Move; Page Down pulls back. Page Up would push forward in retail, but it is the editor's own key here, so use the wheel |
| Shift | Fine steps (a tenth) |
| Save weapon / Enter | Saves this action for this weapon and closes |
| Save all | Saves this action for all weapons and closes |
| Cancel / Esc / Page Up | Closes and puts the placement back |
| Reset | Previews the default for this weapon and action; save to keep it |
| Reset all | Clears every saved placement the editor manages (retail's rule), at once |

Limits are retail's: 50 cm on each axis, 90 degrees on each angle. A weapon swap, the first-person view ending, the settings menu opening or the game losing focus cancels the edit and puts the placement back.

**Saving:** a save goes through the same store as the menu, so the file is written once, on the save.

**How it is drawn:** the editor lays out its own panel (retail's geometry, top left of the screen) and hit-tests its own buttons. `src/mod_editor.c` draws those rectangles and texts with the overlay host's theme tokens (`ov->theme()`), shaped per `HD2-UI-Style/DESIGN.md` (panel rounding 10 with a 1 px line border, buttons rounding 6, hover fill under the pointer). It holds no placement logic.

**While it is open** the overlay host has the game's keyboard and mouse, as retail's captured input did: you cannot move or look around.

### Not built

- **The eye-camera editor** (retail's Shift+F8). It needs the head positioner, which this build leaves off; the editor only opens in its weapon mode.

### Live replace and the panel

- A live replace is refused while the editor is open, opening or closing.
- Overlay Host cannot take a panel out and refuses a second one with the same id. Each load of the mod therefore registers its panel under an id of its own, and a replaced build closes its panel and draws nothing from then on. Its entry stays in the host's panel list (F7) as a dead "True First Person: placement" until the game restarts.

### Decisions you may want different

- **Page Up** both opens the editor and, in retail, moves the weapon forward. Here it only opens and closes; rebind the editor key in the menu to get Page Up back for depth.
- The panel sits where retail puts it and is not movable.

## Unproven until it runs in the game

1. That `%LOCALAPPDATA%` is writable from the game under HellForge's sandbox, and that the rename replaces the file there.
2. That DBF-MCM accepts the definition in game. Natively it passes MCM's own schema parser (`tests/run-wire-settings.sh`, when the MCM source is beside this repository).
3. That this mod's tick and MCM's update are the same thread (MCM refuses calls from any other).
4. `set_label` and `set_disabled` on the status rows, and the per-weapon drafts following the weapon.
5. Unregistering during a live replace.
6. The editor in the game, all of it: that the overlay host's window gives the cursor and the mouse as the panel expects (buttons and wheel are read in the host's draw callback on the render thread and handed to the game thread), that the first frame arrives before the editor's first input sample, that `Window.mouse_focus()` / `show_cursor()` answer true / false when it opens, text sizes and positions against the panel, and the key poll while the host holds the keyboard.
7. `%+.1f` number texts are formatted by the C library of the build (zig's), compared natively with glibc only.

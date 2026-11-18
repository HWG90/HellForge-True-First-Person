# The settings menu, saving, and the placement editor (0.6.0-feel.6)

Three things retail has that the first HellForge builds lacked. Two are built in 0.6.0-feel.6; the placement editor ("poser") is not. None of it has run in the game yet.

| Part | State in 0.6.0-feel.6 |
|---|---|
| Saving settings | Built. `src/wire_settings.c` (when), `src/mod_view.c` (the file) |
| Settings menu | Built, on DBF-MCM for HellForge. `src/wire_menu.c`, `src/mod_menu.c` |
| Placement editor | Not built. Its menu button and key are shown disabled |

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
| Edit arms and weapon placement, Open / close placement editor (key) | **Disabled**: the editor is not built |
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

## The placement editor: what is planned

Retail's editor is `src/first_person/placement_editor.lua` (397 lines): a draft placement per weapon and per action, previewed live through the controller, saved for this weapon or all weapons, reset to the default, cancelled on weapon change. The runtime translation already carries what it needs: the `pc_*` entries (the controller as the editor sees it), the `store_*` entries (get, get_action, target, save, baseline, reset_all, select_mode) and seven `editor_*` host pointers that still answer "off".

Plan: translate the editor's logic to C with vectors from the unmodified Lua, replace its drawn panel with a Dear ImGui panel through Goose Overlay Host (colours from `ov->theme()`, layout per `HD2-UI-Style/DESIGN.md`), keep its mouse drags (left: move, right: yaw and pitch, middle: roll, wheel: depth) and keys, open it with Page Up and from the menu button. `tfp_wire_feel_held` already refuses a live replace while an editor is open (`editor`), and the binder has a slot for it (`TFP_WIRE_EXT_EDITOR`).

Known constraints found while preparing it:

- Overlay Host has no way to unregister a panel and refuses a second panel with the same id. A replaced build's panel stays registered, so the panel must be opened by this mod's own key poll (not a host hotkey) and a retired build's panel must draw nothing.
- The eye-camera editor (retail's Shift+F8) depends on the head positioner, which is still off.

## Unproven until it runs in the game

1. That `%LOCALAPPDATA%` is writable from the game under HellForge's sandbox, and that the rename replaces the file there.
2. That DBF-MCM accepts the definition in game. Natively it passes MCM's own schema parser (`tests/run-wire-settings.sh`, when the MCM source is beside this repository).
3. That this mod's tick and MCM's update are the same thread (MCM refuses calls from any other).
4. `set_label` and `set_disabled` on the status rows, and the per-weapon drafts following the weapon.
5. Unregistering during a live replace.

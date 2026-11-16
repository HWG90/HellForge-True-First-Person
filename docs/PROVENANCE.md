# Where the starting files came from

Seeded 2026-11-16.

## The 0.5.34 C port ("donor")

Copied once from `/home/goose/Documents/Codex/2026-11-07/tr/work/true-first-person-hellforge-dev5` (True First Person 0.5.34-hellforge.dev.5, built 2026-11-09, not under git). Copied: `src/`, `tests/`, `tools/`, `mod/`, `build.sh`, `LICENSE`, `NOTICE.md`, and its `docs/research/` as `docs/research-0.5.34/`. Not copied: `dist/`, `work/`, its README and validation records. At seeding time `bash build.sh` built and `bash tests/run-native.sh` passed here unchanged.

That port was run in the game by the user and cancelled on 2026-11-10 (weapons slow to reappear after a swap, scope masking). It is retail behaviour as of kit 0.5.34. Its files fall in two groups:

- **Behaviour, not trusted, to be replaced by translations of retail smooth.66:** `camera_policy.c`, `pose_policy.c`, `fov_policy.c`, `near_policy.c`, `blur_policy.c`, `lifecycle.c`, `runtime.c`, `visibility_policy.c`, `models.c`, `effects.c`, `markers.c`, `markers_native.c`, `motion.c`, `placement.c`, `action.c`, `correction.c`, `lens.c`, `depth.c`, `presentation.c`, `preferences.c`, `scene.c`, and their headers and tests.
- **Plumbing, to be re-read against the retail helper it serves before it is used:** `backend.c` (guarded reads), `dispatch_lease.c`, `slot_platform.c`, `slot_atomic.c`, `engine.c` (calls to the engine's Lua-registered functions), `native_call.c`, `settings.c`, `ui.c`, `mod.c`, `sdk_inventory.c`, `profile.h`, `native_contract.h`, `feature_contract.h`.

SHA-256 (first 16 hex digits) of each copied file at seeding:

| file | sha256 |
|---|---|
| `src/action.c` | 485508b5f421aa14 |
| `src/action.h` | 60f9bcf65473f410 |
| `src/backend.c` | ac7f28f2472cfab6 |
| `src/backend.h` | 5a083a7c7b05bf9c |
| `src/blur_policy.c` | ae0abf821aebeaa7 |
| `src/blur_policy.h` | c1b2b21c43383b9a |
| `src/camera_policy.c` | 580bd504cfb5d341 |
| `src/camera_policy.h` | 4ae6b5ab3a5bf509 |
| `src/correction.c` | fb3d9fa8e7875478 |
| `src/correction.h` | 2690842e666fe32f |
| `src/depth.c` | 7fc43ef9678c431d |
| `src/depth.h` | 9b2a94ffb03a7977 |
| `src/dispatch_lease.c` | 09a7f32dc2c5269f |
| `src/dispatch_lease.h` | bdfdb63e658ede75 |
| `src/effects.c` | 8a28bbc3af8286d5 |
| `src/effects.h` | fc25a2fd3b0197be |
| `src/engine.c` | bca44e64d23280a0 |
| `src/engine.h` | 8a3e0d0e058a12ed |
| `src/feature_contract.h` | b23783c0a1540650 |
| `src/fov_policy.c` | 9656e465259c6ec0 |
| `src/fov_policy.h` | b18223d63ca26ba7 |
| `src/goose_overlay.h` | e0ed55ee4f37d42a |
| `src/lens.c` | 230c5c3afaef0d29 |
| `src/lens.h` | 591971a4d6f0dd6a |
| `src/lifecycle.c` | 39f543d78d2fd6fd |
| `src/lifecycle.h` | 7ac4f4886317de3c |
| `src/markers.c` | c1be1338cf0b4e70 |
| `src/markers.h` | b39fb5026c6eb2b6 |
| `src/markers_native.c` | ff63b6174ae18014 |
| `src/mod.c` | 7547a5b18aec2344 |
| `src/models.c` | e2d9cc63fe82decc |
| `src/models.h` | 94e117f2598c4606 |
| `src/motion.c` | bbf0e4e1dbe14e19 |
| `src/motion.h` | d253f93a733a37af |
| `src/native_call.c` | 9cb1b661571a88af |
| `src/native_contract.h` | 086c2a4fbadebc51 |
| `src/native_observer.c` | 5598a9cfcdbb2823 |
| `src/native_observer.h` | d788f531dfe07c5f |
| `src/near_policy.c` | 3ed878188b44f830 |
| `src/near_policy.h` | ff1105087546d096 |
| `src/placement.c` | ec28c4f09f829892 |
| `src/placement.h` | 84931bf9656f3c7b |
| `src/policy_owner.h` | 0c6b4d3e03332ad7 |
| `src/pose_policy.c` | a02cdadd47740ed9 |
| `src/pose_policy.h` | 6ea9a3954a2c2427 |
| `src/preferences.c` | fe91e8611a0ad350 |
| `src/preferences.h` | d1ec1efe9bd886e7 |
| `src/presentation.c` | 3b876703f71862b3 |
| `src/presentation.h` | e8f28bb77f181ea5 |
| `src/private_byte.c` | d52fd7bd25d7fea9 |
| `src/probe.c` | 53184f6e282d22cb |
| `src/profile.h` | b63d2610685d602b |
| `src/runtime.c` | 79fc3d4d5a82896b |
| `src/runtime.h` | 483f310ddf4844ed |
| `src/scene.c` | e928675761e1d346 |
| `src/scene.h` | 978184aa54f92742 |
| `src/sdk_inventory.c` | a008efd52a03e131 |
| `src/sdk_inventory.h` | fe148c7f9514ce34 |
| `src/settings.c` | 21877ed770ff66f6 |
| `src/settings.h` | 44ecfde9b5c46eb6 |
| `src/slot_atomic.c` | c6b9048f47373dcb |
| `src/slot_platform.c` | d70753d18c64ab15 |
| `src/slot_platform.h` | 85f695d61d11e704 |
| `src/ui.c` | 70c07fdd07693bbf |
| `src/ui.h` | 383a8c53f1c3766a |
| `src/visibility_policy.c` | 64cb078513f1a477 |
| `src/visibility_policy.h` | d84a06a98aff3de2 |
| `tests/action_test.c` | b4f95ba689134b72 |
| `tests/attachment_test.c` | ea2558e8a4e62478 |
| `tests/backend_test.c` | d1bf58973b4a70cc |
| `tests/blur_policy_test.c` | 5ae53bcbb14c9c91 |
| `tests/build-windows.sh` | 585641090bb1e11f |
| `tests/camera_policy_test.c` | 60368100db97d091 |
| `tests/correction_test.c` | d70cad923c39ffb1 |
| `tests/depth_test.c` | 83679d038b971f5c |
| `tests/dispatch_test.c` | c030a26783730a76 |
| `tests/dll_smoke.c` | 0b97dcaead444100 |
| `tests/effects_test.c` | 1a6a3e3b2f07ab2c |
| `tests/engine_admission_test.c` | e731bc710fa1198f |
| `tests/engine_capture_test.c` | 9e6453a7fd1289d1 |
| `tests/engine_result_test.c` | 85c66947117375e1 |
| `tests/engine_sdk_census_test.c` | 1c9e05f6a224df91 |
| `tests/engine_vm_close_test.c` | 8c8060f8d7cdbb49 |
| `tests/fake_native.h` | 698755a19913413b |
| `tests/fov_policy_test.c` | 144e21d33c8dfd2d |
| `tests/lifecycle_test.c` | 81919a67685c102c |
| `tests/markers_test.c` | 6e305fc4187227ee |
| `tests/migration_test.py` | 63e7ab1b73e2ba8f |
| `tests/models_test.c` | 828b2b01596bf1a5 |
| `tests/native_capture_analysis_test.py` | 05cc62de5af4dede |
| `tests/native_observer_test.c` | 08293f88dbf2d866 |
| `tests/near_policy_test.c` | 80655e6d2d1141ae |
| `tests/placement_test.c` | d290ba8b4a499e8e |
| `tests/pose_policy_test.c` | 253e5a21be0391a7 |
| `tests/preferences_test.c` | 28c6aae1d54a8cc9 |
| `tests/probe_smoke.c` | 83d53ac9f39e7ea3 |
| `tests/research_recording_test.c` | b763b48955016c98 |
| `tests/run-native.sh` | 9b107ca8ca64b2ae |
| `tests/run-windows.py` | a539f1ee33486d65 |
| `tests/runtime_test.c` | 8b8534aad1a16fc5 |
| `tests/scene_test.c` | 6003d4e2838ac885 |
| `tests/settings_test.c` | 5a2084e8a54bcd82 |
| `tests/slot_platform_test.c` | 11d0c0a506485e26 |
| `tests/visibility_policy_test.c` | ef655a554cd1f911 |
| `tools/analyze_native_capture.py` | d358af53854888e2 |
| `tools/generate_contract.py` | a7168ea84ac450f7 |
| `tools/generate_features.py` | 40d007439b6b6872 |
| `tools/migrate_settings.py` | 281e0ba733bc3f04 |
| `tools/package.py` | 40017ae45cc4b1e4 |
| `mod/hellforge-mod.json` | 05957ff85b8136ee |
| `mod/probe-manifest.json` | f62ba3789f0cc329 |

## Session log tools

`src/smooth_meter.c|h`, `src/request_log.c|h`, `tools/smooth_report.py` and their tests come from `~/Projects/HellForge/Backports/true-first-person-controller`, branch `stage1a-aim` at 347ca0b.

## Retail reference

`docs/RETAIL-SMOOTH66-INVENTORY.md` was written on 2026-11-16 from `~/Worktrees/goose-gadgets/tfp-smooth/Goose Gadget Kit/` at commit 1d0d453 (0.5.41-smooth.66). That folder is read only for this project.

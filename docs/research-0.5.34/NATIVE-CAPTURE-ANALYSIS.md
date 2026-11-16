# Interpreting the native pose capture

`tools/analyze_native_capture.py` reads the research build's copied TSV. It has
no process, SDK, DLL-loading or memory-access capability. Python remains offline
development tooling; the playable mod remains entirely C.

```sh
python3 tools/analyze_native_capture.py /absolute/path/true-first-person-native-g1.tsv --output /absolute/path/native-analysis.json
python3 tests/native_capture_analysis_test.py
```

The analyzer reports camera-relative root0, branch5, camera50, gun, socket8 and
hands13/34 positions. It also reports root-relative camera50/branch5 and gun- or
socket-relative positions. Full inverse-affine coordinates account for a moving,
rotating or scaled reference frame. This can distinguish whole-rig movement from
changes between the hands and gun without treating a world-space camera pan as
grip movement.

Each metric includes coordinate ranges and median,95th-percentile and maximum
adjacent position changes. Adjacent steps require identical camera request,
body/rig/camera/weapon identities, enabled state, camera kind and ADS state, with
a positive gap no larger than150ms. Owner/state changes are listed separately.
Failed samples, unavailable components and malformed partial rows never supply
substitute zero poses. Partial `.tsv.part` captures are accepted with their
missing finalization footer explicitly reported.

Ranges pool all valid samples of each named mode, possibly across weapons;
adjacent step statistics never cross weapon/request changes. These are sampled
position diagnostics, not rotation measurements, input-latency measurements or
a jitter verdict. Native recoil, reload and locomotion can legitimately change
these values. The game-thread recorder does not establish that all matrices in
a row were produced by the same native evaluation. A suspected evaluation-phase
mismatch still requires a separately qualified boundary observation.

Offline verification covers1,001 known inverse-affine transforms, including
nonuniform scale and nonorthogonal axes; large common world-motion cancellation;
singular/nonfinite rejection; owner changes; invalid/missing observations; long
gaps; and truncated/finalized recordings. No real live capture has been analyzed
with this tool yet.

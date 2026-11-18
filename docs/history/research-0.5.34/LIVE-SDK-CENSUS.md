# First live SDK census — HellForge dev.3

Session `9472b065334c40c9a443d56cb03a3279`, build25480438, captured VM generation1.
The loader reports dev.3 DLL SHA256
`4852f844e50c76fb1a30db7535ffc9668560a75ec0ef3c23b73f60104884d2f8`.
The game was launched using HellForge's saved settings. The mod remained off.

The C research module traversed the captured `stingray` table using raw Lua C
API operations. It did not invoke any discovered SDK function or activate a
camera. This is live registration/pointer evidence, not gameplay acceptance.

| Observation | Result |
|---|---:|
| Emitted entries |2645|
| Visited tables |143|
| Table aliases |123|
| Function paths |1947|
| Functions in captured EXE address range |1876|
| Functions with C pointers outside EXE/game ranges |53|
| Lua functions without a C entry pointer |18|
| Original static paths present |1223/1223|
| Original callback RVA mismatches |0|
| Additional function paths beyond the static registration inventory |724|
| Selected native API pointers readable |11/11|

All1,223 static registered paths were present with one of their originally
recorded callback RVAs. The selected UnitAPI position/rotation/pre-input getter
and CameraAPI position/rotation/near/FOV/post-projection pointers also read
successfully. Agreement establishes current entry identities; it does not prove
that every live function body is unchanged or qualify call signatures/effects.

The additional functions are largely dynamically registered device tables,
component accessors and audio functions. Relevant leads include
`Mouse/Pad.link_to_camera_shake`, `Renderer.resource`, and generic component
property accessors. Their names do not establish read-only behavior or a useful
first-person control. Separate bounded static follow-ups examine their actual
implementations before any invocation.

The walker reported **38 omitted entries**, no row limit, and no depth limit.
It omits non-string keys, unsupported key text and overlong paths; this output
does not identify which omission category each entry belonged to. Aliases are
reported but not expanded again, and their target paths are not recorded.
Its `complete=0` is retained. This census does not establish complete SDK
coverage merely because every path in the selected static inventory matched.

Raw evidence: `live-sdk-census-g1-9472b065.tsv`.
Comparison and hashes: `live-sdk-census-g1-9472b065.json`.
No pose capture or manual F11 trial had begun when this report was written.

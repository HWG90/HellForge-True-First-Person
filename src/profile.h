/* Build25480438 only. Derived from the original native FPS profile and
 * checked byte-for-byte against its captured image; not Steam-build offsets. */
#ifndef TFP_PROFILE_H
#define TFP_PROFILE_H
#include <stdint.h>
#include <stddef.h>
#define FP_GAME_STAMP UINT32_C(0x6ab3b43f)
#define FP_GAME_SIZE UINT32_C(0x4744000)
#define FP_EXE_STAMP UINT32_C(0x6ab382e4)
#define FP_EXE_SIZE UINT32_C(0x39e8000)
#define FP_CAMERA UINT32_C(0x346d560)
#define FP_CONTROLLER UINT32_C(0x347ce70)
#define FP_WORLD UINT32_C(0x346bf98)
#define FP_PLAYER UINT32_C(0x3326468)
#define FP_AVATAR UINT32_C(0x3326d20)
#define FP_WIELD UINT32_C(0x3326420)
#define FP_WEAPONS UINT32_C(0x3326ce0)
#define FP_REGISTRY UINT32_C(0x1a100f0)
/* Added 2026-11-16 for the smooth.66 rewrite; see docs/BUILD-DIFFERENCES.md. */
#define FP_INVENTORY UINT32_C(0x3326738)   /* retail 0x2ce18e8: backpack slot rows */
#define FP_STATE UINT32_C(0x3326340)       /* retail 0x2ce1508 */
#define FP_STATE_SCENE UINT32_C(0xac21c)   /* retail state+0xac29c: scene 3 or 4 on foot */
#define FP_API_ROOT UINT32_C(0x3326308)    /* retail 0x2ce14c8: engine API tables */
#define FP_ENTRY UINT32_C(0x127ddb0)
#define FP_EXIT UINT32_C(0x127e9d0)
#define FP_ALLOWED UINT32_C(0x12b2d20)
#define FP_AVATAR_FLAGS UINT32_C(0x53e880)
#define FP_AVATAR_RIG UINT32_C(0x53e9b8)
#define FP_AVATAR_STRIDE UINT32_C(0x1238)
#define FP_ENTITY_MAP UINT32_C(0xf1aeb0)
#define FP_LIGHT_MAP UINT32_C(0xf22ec8)
#define FP_ENTITY_ROWS UINT32_C(0xf32f18)
typedef struct FpWitness {const char *name;uint32_t rva;const char *hex;} FpWitness;
static const FpWitness fp_witnesses[] = {
    {"enter_wrapper",UINT32_C(0x127ddb0),"40534883ec20488b1da3f71e02488bcbe85b0000008b83fc0100003983f80100007509488d93001f"},
    {"constructor",UINT32_C(0x127de20),"4055535657488dac2478ffffff4881ec88010000488b05d5e13b014833c4488945500f57c033c041"},
    {"exit",UINT32_C(0x127e9d0),"405541544156488dac24d0feffff4881ec30020000488b0524d63b014833c4488945708b81fc0100"},
    {"evaluator",UINT32_C(0x1286330),"48895c24205556574154415541564157488dac24f0fcffff4881ec10040000440f299c24b0030000"},
    {"controller_enter",UINT32_C(0x12b29b0),"48895c2410574883ec20807904008bda488bf90f85010100008339000f85f8000000807905000f85"},
    {"controller_exit",UINT32_C(0x12b2ae0),"4883ec28807904007446c6410400488b0d6baa1b02e8d6befcff488b053f3807"},
    {"controller_update",UINT32_C(0x12b26a0),"f30f114c24105341574883ec38488b05b43d07024c8bf983b8880000000074128b90a8030000488d"},
    {"controller_aim_gate",UINT32_C(0x12b28a3),"48b800000000000001004985842480e853004c8b64243074083b3dc61e1d027508498bcfe814020000"},
    {"input_desired_gate",UINT32_C(0xa41c0b),"488b0d5eb2a3024c8bb424b80000003a4104741984c074098bd3e8860d8700eb0c488b0d3db2a302e8a80e8700"},
    {"allowed",UINT32_C(0x12b2d20),"48895c240848896c2410488974241857415641574883ec203b15e20e1d027452488b1d993f070245"},
    {"allow_fpv_field",UINT32_C(0x12b2dfb),"418bc9488b0ccee8396c25ff80b880010000007482b001eb80"},
    {"root_position_call",UINT32_C(0x12887c9),"41ff5138"},
    {"root_rotation_call",UINT32_C(0x12887e9),"41ff5148"},
    {"pose_branch_position_call",UINT32_C(0x1288420),"41ff5138"},
};
#define FP_WITNESS_COUNT (sizeof fp_witnesses/sizeof fp_witnesses[0])
#endif

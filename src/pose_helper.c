/* native/first_person/native_ads_editor.cpp (0.5.41-smooth.66, 1,707 lines)
 * in C11, for build 25480438. Fresh native arms/weapon offsets plus paired
 * carrier-depth normalization.
 *
 * A translation, not a rewrite: every check, its order, every reset, counter
 * and expression is retail's. "cpp:a-b" above a function is its line range in
 * the retail file. What differs is in docs/translation/POSE-HELPER-TRANSLATION.md. */
#include "pose_helper.h"
#include "pose_math.h"
#include <stdatomic.h>

/* ======================================================================
 * Build constants, build 25480438. Each line: retail 26568081's value, and
 * how this build's value was established.
 *   build_map  = tools/build_map.py --to-here (the kit's migration records)
 *   image      = bytes read from game-pre1020.dll / r6_reload_exe_20261017.bin
 *   input-json = docs/history/research-0.5.34/native-input-evidence.json (verified_old_mapping,
 *                cross_build_comparisons, static_caller_frame)
 *   layout     = structure layout, same value in retail; the named file of
 *                this repository reads the same field on this build
 *   OPEN       = not established; 0; the path that needs it refuses
 * ====================================================================== */
/* -- image identity -- */
#define B_GAME_STAMP        UINT32_C(0x6ab3b43f) /* retail 0x6af322c1; build_map; profile.h FP_GAME_STAMP */
#define B_GAME_SIZE         UINT32_C(0x4744000)  /* retail 0x3382000;  build_map; profile.h FP_GAME_SIZE */
#define B_EXE_STAMP         UINT32_C(0x6ab382e4) /* retail 0x6af2d5e6; build_map; profile.h FP_EXE_STAMP */
#define B_EXE_SIZE          UINT32_C(0x39e8000)  /* retail 0x29fe000;  build_map; profile.h FP_EXE_SIZE */
/* -- the four Unit API functions behind the pose slots, and their first bytes -- */
#define B_EXE_POSITION        UINT32_C(0x1fc720) /* retail POSITION 0x1f25e0; build_map; native_contract.h unit_position */
#define B_EXE_ROTATION        UINT32_C(0x1fc880) /* retail ROTATION 0x1f2740; build_map; image (same body as unit_position, see B_HEX_ROTATION) */
#define B_EXE_BRANCH_ROTATION UINT32_C(0x1fc7a0) /* retail BRANCH_ROTATION 0x1f2660; build_map; native_contract.h unit_branch_rotation */
#define B_EXE_LOCAL_POSITION  UINT32_C(0x1fc450) /* retail LOCAL_POSITION 0x1f2310; build_map; native_contract.h unit_local_position */
#define B_HEX_POSITION        "48895c24084889742410574883ec20498bf08bfa8bd9e88511eaff4c8b5018b801000000f0410fc182380017008bc04c"
                                                 /* 48 bytes as retail; retail call displacement d5e0eaff, here 8511eaff; image at exe+0x1fc720 */
#define B_HEX_ROTATION        "48895c24084889742410574883ec20498bf08bfa8bd9e82510eaff4c8b5018b801000000f0410fc182380017008bc04c"
                                                 /* 48 bytes as retail; retail call displacement 75dfeaff, here 2510eaff; image at exe+0x1fc880 */
#define B_HEX_BRANCH_ROTATION "48895c24084889742410574881eca0000000440f29b42490000000498bd84863fae8fa10eaff488bc8488bf04c8b0841"
                                                 /* 48 bytes as retail; retail call displacement 4ae0eaff, here fa10eaff; image at exe+0x1fc7a0 */
#define B_HEX_LOCAL_POSITION  "40534883ec204863dae86214eaff488bc84c8b0041ff90e8000000488bcb48c1e1064883c124488b40204803c14883c4205bc3"
                                                 /* 51 bytes as retail; retail call displacement b2e3eaff, here 6214eaff; image at exe+0x1fc450 */
#define B_EXE_GRAPH_GETTER    UINT32_C(0x2bd870) /* retail GRAPH_GETTER 0x28d3c0; build_map; native_contract.h graph_getter */
#define B_HEX_GRAPH_GETTER    "488d4160c3"       /* retail the same; image at exe+0x2bd870 */
/* -- call sites in the first-person camera evaluator -- */
#define B_GAME_CARRIER_CALL   UINT32_C(0x12887c9) /* retail CALLS[0] 0x111a809; build_map; native_contract.h rig_carrier_call */
#define B_GAME_CARRIER_PAIR   UINT32_C(0x12887e9) /* retail CALLS[1] 0x111a829; build_map; image */
#define B_GAME_RIG_CALL       UINT32_C(0x1288420) /* retail RIG_CALL 0x111a460; build_map; native_contract.h rig_position_call */
#define B_GAME_ROTATION_CALL  UINT32_C(0x12890db) /* retail ROTATION_CALL 0x111b11b; build_map; native_contract.h rig_rotation_call */
#define B_GAME_OBSERVE_CALL   UINT32_C(0x1287a41) /* retail OBSERVE_CALL 0x1119a81; build_map; native_contract.h rig_observe_call */
#define B_HEX_CARRIER_CALL    "41ff5138"          /* retail the same; image at game+0x12887c9 */
#define B_HEX_CARRIER_PAIR    "41ff5148"          /* retail the same; image at game+0x12887e9 */
#define B_HEX_RIG_CALL        "41ff5138"          /* retail the same; image at game+0x1288420 */
#define B_HEX_ROTATION_CALL   "41ff5140"          /* retail the same; image at game+0x12890db */
#define B_HEX_OBSERVE_CALL    "41ffd0"            /* retail the same; image at game+0x1287a41 */
#define L_CALL_RETURN         4                   /* length of "41ff51xx": the return address is the call + 4 */
#define L_OBSERVE_RETURN      3                   /* length of "41ffd0" */
/* -- globals -- */
#define B_GAME_CONTEXT      UINT32_C(0x346d560)  /* retail 0x2e287d0; build_map; profile.h FP_CAMERA */
#define B_GAME_AVATAR_OWNER UINT32_C(0x3326d20)  /* retail 0x2ce1ee0; build_map; profile.h FP_AVATAR */
#define B_GAME_CONTROLLER   UINT32_C(0x347ce70)  /* retail 0x2e381a8; build_map; profile.h FP_CONTROLLER */
#define B_GAME_API_ROOT     UINT32_C(0x3326308)  /* retail 0x2ce14c8; build_map; profile.h FP_API_ROOT */
#define B_EXE_REGISTRY      UINT32_C(0x1a100f0)  /* retail 0x1ab76f8; build_map; profile.h FP_REGISTRY */
#define B_GAME_WEAPON_POOL  UINT32_C(0x3326ce0)  /* retail 0x2ce1ea0; build_map; profile.h FP_WEAPONS */
#define B_GAME_WORLD        UINT32_C(0x346bf98)  /* retail 0x2ce41b8; build_map; profile.h FP_WORLD */
#define B_GAME_SETTINGS     UINT32_C(0x3326340)  /* retail 0x2ce1508; build_map; profile.h FP_STATE */
#define B_SETTINGS_DEGREES  UINT32_C(0xac408)    /* retail 0xac488; build_map; input-json tpv_fov_setting; depth.c */
/* -- avatar owner rows -- */
#define B_AVATAR_FLAGS      UINT32_C(0x53e880)   /* retail 0x53e888; build_map; profile.h FP_AVATAR_FLAGS */
#define B_AVATAR_RIG        UINT32_C(0x53e9b8)   /* retail 0x53e9c0; build_map; profile.h FP_AVATAR_RIG */
#define B_AVATAR_STRIDE     UINT32_C(0x1238)     /* retail 0x1240;   build_map; profile.h FP_AVATAR_STRIDE */
/* -- weapon definitions (zoom depth) -- */
#define B_WORLD_RESOURCES   UINT32_C(0xf12bd8)   /* retail 0xf146a0; image: mov r10,[rax+0xf12bd8] at game+0x50957f; depth.c; BUILD-DATA-25480438.md */
#define B_RESOURCE_BUCKETS  730                  /* retail 746; image: imul eax,eax,0x2da at game+0x5095a3; depth.c */
#define B_RESOURCE_RECORDS  UINT32_C(0x2da0)     /* retail 0x2EA0 (= buckets*16); image: add rax,0x2da0 at game+0x5095f1; depth.c */
#define B_DEFINITION        UINT32_C(0x4d0)      /* retail 0x4D8; image: imul ...,0x4d0 at game+0x509ae2 and game+0x5095f1; depth.c */
#define B_DEPTH_PIN_1 UINT32_C(0x509ae2)         /* retail 0x5120b2; image search for the override getter (the only one with a record within 8 bytes of retail's); depth.c */
#define B_DEPTH_HEX_1 "4869c0d0040000490383b00000004883c428c3" /* retail 4869c0d8040000...; image */
#define B_DEPTH_PIN_2 UINT32_C(0x50957f)         /* retail 0x511b3f; image: the resource-row getter's table load and division; depth.c */
#define B_DEPTH_HEX_2 "4c8b90d82bf10048b8379f711660f3196748f7e1488bc1482bc248d1e84803c248c1e809"
                                                 /* retail 24 bytes (mov, movabs, mul, shr 9: divide by 746); here 36 bytes: this build divides by 730 with mul, mov, sub, shr 1, add, shr 9; image */
#define B_DEPTH_PIN_3 UINT32_C(0x5095a3)         /* retail 0x511b57; image; depth.c */
#define B_DEPTH_HEX_3 "69c0da020000442bc0"       /* retail 69c2ea020000442bc0; image */
#define B_DEPTH_PIN_4 UINT32_C(0x5095f1)         /* retail 0x511bb1; image; depth.c */
#define B_DEPTH_HEX_4 "4869c1d00400004805a02d00004903c2c3" /* retail 4869c1d80400004805a02e00004903c2c3; image */
#define B_DEPTH_PIN_5 UINT32_C(0x12873b3)        /* retail 0x11193f3; build_map; depth.c */
#define B_DEPTH_HEX_5 "f30f1044245cf30f1015df4b1401" /* retail rip displacement 9f241301, here df4b1401; image */
#define B_DEPTH_PIN_6 UINT32_C(0x12873d9)        /* retail 0x1119419; build_map */
#define B_DEPTH_HEX_6 "f30f5c4424600f54c2f30f5ec1" /* retail the same; image */
#define B_DEPTH_PIN_7 UINT32_C(0x1287a91)        /* retail 0x1119ad1; build_map */
#define B_DEPTH_HEX_7 "f3450f58fb"               /* retail the same; image */
#define B_DEPTH_PIN_8 UINT32_C(0x1287b43)        /* retail 0x1119b83; build_map */
#define B_DEPTH_HEX_8 "f30f59faf3410f59cff3"     /* retail the same; image */
#define B_DEPTH_PIN_9 UINT32_C(0x1287c04)        /* retail 0x1119c44; build_map */
#define B_DEPTH_HEX_9 "f3410f11774cf3450f114750" /* retail the same; image */
/* -- carrier (rig root) code -- */
#define B_CARRIER_PIN_1 UINT32_C(0x1286335)      /* retail 0x1118375; build_map */
#define B_CARRIER_HEX_1 "5556574154415541564157488dac24f0fcffff4881ec10040000" /* retail the same; image */
#define B_CARRIER_PIN_2 UINT32_C(0x128878b)      /* retail 0x111a7cb; build_map */
#define B_CARRIER_HEX_2 "4c8d8540010000f2410f11475433" /* retail the same; image */
#define B_CARRIER_PIN_3 UINT32_C(0x12887d4)      /* retail 0x111a814; build_map */
#define B_CARRIER_HEX_3 "4c8d8530010000"         /* retail the same; image */
/* -- the evaluator's stack frame, from S = its stack pointer at a Unit API call -- */
#define B_STACK_CONTEXT     0x80   /* retail the same; input-json static_caller_frame (store at game+0x1286370) */
#define B_STACK_INDEX       0x48   /* retail the same; input-json (store at game+0x1286430) */
#define B_STACK_DT          0xa0   /* retail the same; input-json (store at game+0x128637d) */
#define B_STACK_LENS        0x44   /* retail the same; image: B_DEPTH_HEX_5 reads [rsp+0x5c] = +0x44+0x18, B_DEPTH_HEX_6 [rsp+0x60] = +0x44+0x1c */
#define B_STACK_ROOT        0x240  /* retail the same; image: B_CARRIER_HEX_2 lea r8,[rbp+0x140] with rbp = S+0x100 (B_CARRIER_HEX_1) */
#define B_AVATAR_MAIN       UINT32_C(0x53d8b0) /* the avatar's main row: docs/history/research-0.5.34/NATIVE-UNZOOM-OWNERSHIP.md (main = avatar_owner + 0x53d8b0 + index*0x1238); read for the "Gate:" line only */
#define B_AVATAR_MAIN_STRIDE UINT32_C(0x1238)  /* the avatar stride of this build (src/camera_helper.c B_AVATAR_STRIDE) */
#define B_STACK_ROOT_SOURCE 0x10   /* retail the same; image: B_CARRIER_HEX_3 lea r8,[rbp+0x130], the quaternion 16 bytes below the root position */
/* -- look-gain normalisation. Refused on this build: see B_INPUT_ESTABLISHED -- */
#define B_INPUT_ESTABLISHED 1                    /* all 16 pins entered; B_PIN_EXIT_FNV accepted 2026-11-17 on its preset use (see there). Evidence for each: docs/build-25480438/LOOK-GAIN-CONSTANTS-25480438.md. input_code still proves every pin in the running game before the first write. */
#define B_EXE_WORLD_MATRIX  UINT32_C(0x1fd220)   /* retail 0x1f30e0; build_map; native_contract.h unit_world_matrix; input-json */
#define B_HEX_WORLD_MATRIX  "40534883ec204863dae89206eaff488bc84c8b0041ff90e8000000488bcb48c1e10648034828488bc14883c4205bc3"
                                                 /* 47 bytes as retail; retail call displacement e2d5eaff, here 9206eaff; image at exe+0x1fd220 */
#define B_GAME_INPUT_RETURN UINT32_C(0x12865fe)  /* retail 0x111863e; input-json pre_filter_return; image: ffd0 at game+0x12865fc */
#define B_GAME_EXP          UINT32_C(0x21087d0)  /* retail 0x1f8ff70; input-json native_exp; image: call at game+0x128662a */
#define B_GAME_PRESETS      UINT32_C(0x32f9990)  /* retail 0x2cb2140; input-json preset_table; image: lea r9,[rip] at native_contract.h look_gain_table */
#define B_GAME_INPUT_OWNER  UINT32_C(0x347cf18)  /* retail 0x2e38250; input-json input_owner_cell */
#define B_GAME_CLOCK        UINT32_C(0x3326348)  /* retail 0x2ce14f8; input-json clock_cell; image: native_contract.h look_clock */
#define B_GAME_PLAYER       UINT32_C(0x3326468)  /* retail 0x2ce1618; build_map; profile.h FP_PLAYER */
#define B_GAME_NO_AVATAR    UINT32_C(0x3484788)  /* retail 0x2e3fcb8 (the "no avatar" id steering compares with); image: cmp eax,[rip] at game+0x128d860 in B_PIN_STEERING */
#define B_GAME_HOST_AVATAR  UINT32_C(0x348456c)  /* retail 0x2e3fa9c (avatar id when the local entity is 0x7fff); image: mov eax,[rip] at game+0xfd9bb9 in B_PIN_9 */
#define B_GAME_IDLE_AVATAR  UINT32_C(0x3483c34)  /* retail 0x2e3f0e4 (avatar id when the player count is 0); image: mov eax,[rip] at game+0x128d84f in B_PIN_STEERING */
#define B_WORLD_ENTITY_MAP  UINT32_C(0xf22ec8)   /* retail 0xf24990; build_map; profile.h FP_LIGHT_MAP */
#define B_WORLD_ENTITY_AVATAR UINT32_C(0xf32f20) /* retail 0xf349e8 = rows 0xf349e0 + 8; build_map 0xf349e0 -> 0xf32f18 (profile.h FP_ENTITY_ROWS); snapshot.c takes word 2 of the 24-byte row */
#define B_AVATAR_STEERING   UINT32_C(0x53e9a4)   /* retail 0x53e9ac; input-json steering_components */
#define B_INPUT_RAW         UINT32_C(0xa7c24)    /* retail the same; input-json raw_offset; native_contract.h look_field */
#define B_INPUT_RAW_STRIDE  UINT32_C(0xa7aec)    /* retail the same; input-json raw_stride; native_contract.h look_stride */
#define B_INPUT_FLAGS       UINT32_C(0xa7af8)    /* retail the same; input-json: scope gains at owner+0xa7b50 (= +0x58) and +0xa7b44 (= +0x4c) */
#define B_INPUT_DEVICE      UINT32_C(0x324)      /* retail the same (device kind); image: mov eax,[rdi+0x324] at game+0x128678b and game+0x128686a, rdi = [B_GAME_INPUT_OWNER] loaded at game+0x12865fe */
#define B_CONTEXT_INHIBIT   0x1a9                /* retail the same; input-json additional_evaluator_fields (game+0x1286756) */
#define B_CONTEXT_LOOK      0x15c                /* retail the same; NATIVE-INPUT-DEEP-DIVE.md "context+0x15C/+0x160" */
/* The 16 code pins (FNV-1a 64 over the range), all from the images. */
#define B_PIN_EVALUATOR     UINT32_C(0x1286330)  /* retail 0x1118370; build_map; input-json native6_evaluator */
#define B_PIN_EVALUATOR_LEN 9688                 /* retail the same; input-json (2051 instructions both builds) */
#define B_PIN_EVALUATOR_FNV UINT64_C(0x925c6f76c4b866ed) /* retail 0xac98356760db02c9; computed from the image */
#define B_PIN_2             UINT32_C(0x1288908)  /* retail game+0x111a948, 24 bytes, 0xded70c3fd0eba95c; image: the evaluator's jump table, named by its table load at game+0x1286680; six entries, each retail's + 0x16dfc0 */
#define B_PIN_2_FNV         UINT64_C(0x6f23c15a305e84dc)/* computed from the image */
#define B_PIN_3             UINT32_C(0x1282a40)  /* retail game+0x1114950, 1724 bytes, 0x3690803fb2e8bced; image: the only place where the 1724 bytes equal retail's outside relocated targets */
#define B_PIN_3_FNV         UINT64_C(0x6d6b65a1d985444d)/* computed from the image */
#define B_PIN_EXP_FNV       UINT64_C(0xfa3d329f82edb886)/* retail game+0x1f8ff70, 1013 bytes, 0xe4a380a8807b6ec2; image at B_GAME_EXP: 1013 bytes equal retail's outside relocated targets, and this build's function table ends the function at +1013 */
#define B_PIN_SOLVER        UINT32_C(0x1277250)  /* retail 0x1109110; input-json native_orientation_solver (equal after masking, 1328 bytes) */
#define B_PIN_SOLVER_FNV    UINT64_C(0xef9a68f3f959a945) /* retail 0xabb508e82158f3f1; computed from the image */
#define B_PIN_6             UINT32_C(0x1277780)  /* retail game+0x1109640, 0x1a0 bytes, 0xf17ad9ae58e57ab0; image: only match; differs in B_SETTINGS_DEGREES and the padding byte after the 0x19f-byte function */
#define B_PIN_6_FNV         UINT64_C(0x36566b360cb60898)/* computed from the image */
#define B_PIN_7             UINT32_C(0x1279970)  /* retail game+0x110b830, 0x5b0 bytes, 0xd77b549041450f76; image: only match; like retail's it starts 2 bytes before an instruction boundary; differs in B_GAME_PRESETS+0x8c and its own jump table */
#define B_PIN_7_FNV         UINT64_C(0xcb8c325895c1bf1d)/* computed from the image */
#define B_PIN_STEERING      UINT32_C(0x128d820)  /* retail 0x111f860; input-json steering_addition (292 bytes, 68 instructions both builds) */
#define B_PIN_STEERING_FNV  UINT64_C(0xba858f4fe27538d8) /* retail 0x23ae2b71a5e0f871; computed from the image */
#define B_PIN_9             UINT32_C(0xfd9ba0)   /* retail game+0xe69ca0, 0xdc bytes, 0x91b780e54d5bb826; image: only match, and the callee of B_PIN_STEERING at game+0x128d848; differs in B_WORLD_ENTITY_MAP and B_WORLD_ENTITY_AVATAR */
#define B_PIN_9_FNV         UINT64_C(0x29608cd098aef135)/* computed from the image */
#define B_PIN_ENTER         UINT32_C(0x127de20)  /* retail 0x110fcf0; build_map; input-json constructor (2992 bytes, 665 instructions both builds) */
#define B_PIN_ENTER_FNV     UINT64_C(0x4bed0e223589e85e) /* retail 0x32dc74e3dc6a26e9; computed from the image */
#define B_PIN_EXIT          UINT32_C(0x127e9d0)  /* retail 0x11108a0; build_map; profile.h FP_EXIT */
#define B_PIN_EXIT_FNV      UINT64_C(0xab98f35fc8fb95d5) /* retail 0x1dd0 bytes, 0x1a26ca7b7bbba077. This build: the exit function (0x1d58 bytes), padding, the next function, padding. A recompiled body, accepted because its only preset use is retail's: index [r12+0x1f0c] *144 + table with no bound, then the same 14 field reads and 2 pointer passes (tools/find_look_gain.py "exit preset use") */
#define B_PIN_12            UINT32_C(0x1fd190)   /* retail exe+0x1f3050, 51 bytes, 0xdafbc85ae8dffa23; image: only match, 0x90 below B_EXE_WORLD_MATRIX as in retail */
#define B_PIN_12_FNV        UINT64_C(0x2d9df1a66a439fb6)/* computed from the image */
#define B_PIN_13            UINT32_C(0x1fd1d0)   /* retail exe+0x1f3090, 76 bytes, 0x9f25c14a58784f1f; image: only match, 0x50 below B_EXE_WORLD_MATRIX as in retail */
#define B_PIN_13_FNV        UINT64_C(0xe99ec6ca69868d09)/* computed from the image */
#define B_PIN_WORLD_MATRIX_FNV   UINT64_C(0xcca0a362b082795b) /* retail 0xd34dcd5634fed114 over exe+0x1f30e0, 47 bytes; computed from the image at B_EXE_WORLD_MATRIX */
#define B_PIN_LOCAL_POSITION_FNV UINT64_C(0xb51d92e7e9e2090d) /* retail 0x542e94a63b7ebae6 over exe+0x1f2310, 51 bytes; computed from the image at B_EXE_LOCAL_POSITION */
#define B_PIN_GRAPH_GETTER_FNV   UINT64_C(0x86a7f411c046a30c) /* retail the same over exe+0x28d3c0, 5 bytes; computed from the image at B_EXE_GRAPH_GETTER */
/* The seven numeric constants the evaluator and the solver load (six floats, one double): each is the address the matched instruction names. */
#define B_CONST_ONE         UINT32_C(0x23c6d70)  /* retail game+0x22465c4 = 1.0f; image: the load at game+0x12866cf (evaluator) and four more matched loads */
#define B_CONST_MINUS_ONE   UINT32_C(0x23c800c)  /* retail game+0x2247894 = -1.0f; image: the load at game+0x1286791 (evaluator) and two more */
#define B_CONST_RAD_TO_DEG  UINT32_C(0x23c7730)  /* retail game+0x2246fb0 = 0x42652ee0; image: the load at game+0x128685a (evaluator) and two more */
#define B_CONST_MINUS_HALF_PI UINT32_C(0x23c8040)/* retail game+0x22478c8 = 0xbfc90fdb; image: the load at game+0x12774d8 (B_PIN_SOLVER) */
#define B_CONST_HALF_PI     UINT32_C(0x23c6f1c)  /* retail game+0x2246774 = 0x3fc90fdb; image: the load at game+0x1277503 (B_PIN_SOLVER) */
#define B_CONST_MINUS_75    UINT32_C(0x23c8228)  /* retail game+0x2247ab0 = -75.0f; image: the load at game+0x128661e (evaluator) */
#define B_CONST_DOUBLE_ONE  UINT32_C(0x23c7098)  /* retail game+0x2246900 = 1.0; image: the loads at game+0x128662f and game+0x12866be (evaluator) */
/* -- structure layout shared by both builds -- */
#define L_API_UNIT          0x18   /* UnitAPI table at root+0x18: image, mov r9,[rcx+0x18] at game+0x1288414; NATIVE-INPUT-DEEP-DIVE.md */
#define L_API_POSITION      0x38   /* image: "41ff5138" call [r9+0x38] */
#define L_API_ROTATION      0x48   /* image: "41ff5148" */
#define L_API_BRANCH        0x40   /* image: "41ff5140" */
#define L_API_LOCAL         0x00   /* input-json: UnitAPI+0, the call at game+0x1287a41 */
#define L_API_WORLD_MATRIX  0x90   /* input-json: UnitAPI+0x90 */
#define L_REGISTRY_UNITS    0x88   /* layout; backend.c */
#define L_REGISTRY_CAPACITY 0x98   /* layout; backend.c */
#define L_REGISTRY_GENERATIONS 0xA0 /* layout; backend.c */
#define L_AVATAR_COUNT      0x6C   /* layout; snapshot.c */
#define L_AVATAR_ROWS       0x110  /* layout; snapshot.c; descriptor: avatar +8, body +12 */
#define L_AVATAR_MAP        0xf8   /* layout; snapshot.c (avatar_owner+0xf8) */
#define L_FLAG_SEATED       14     /* layout; snapshot.c flags[14]&4 */
#define L_FLAG_NATIVE_AIM   6      /* layout; snapshot.c flags[6]&1 ("bit 48") */
#define L_FLAG_SHOULDER_A   10     /* layout; snapshot.c flags[10]&4 */
#define L_FLAG_SHOULDER_B   15     /* layout; snapshot.c flags[15]&32 */
#define L_CONTROL_AIM       4      /* layout; snapshot.c controller byte 4 */
#define L_CONTEXT_HEAD      0x1F8  /* layout; snapshot.c, backend.c */
#define L_CONTEXT_TAIL      0x1FC  /* layout; snapshot.c, backend.c */
#define L_CONTEXT_IDLE      0x1F00 /* layout; snapshot.c, backend.c */
#define L_CONTEXT_RECORDS   0x200  /* layout; snapshot.c, backend.c */
#define L_CONTEXT_RECORD    0xE8   /* layout; snapshot.c, backend.c */
#define L_REQUEST_SIGHT     0x18   /* layout; pose_policy.c */
#define L_REQUEST_HISTORY   0x3c   /* input-json request_history */
#define L_REQUEST_POSITION  0x48   /* image: lea r8,[r15+0x48] at game+0x128840e, before the rig position call */
#define L_REQUEST_STATE     0x80   /* input-json request_preset (+0x80), request_lens (+0x84), request_pitch_gate (+0x8c); 0x24 bytes are read */
#define L_REQUEST_COUPLING  0x98   /* layout; pose_policy.c */
#define L_VT_GRAPH_GETTER   0xE8   /* layout; scene.c */
#define L_UNIT_COUNT        0x70   /* layout; scene.c */
#define L_UNIT_LOCALS       0x80   /* layout; scene.c (local pose array; node 0 position at +0x24) */
#define L_UNIT_LINKS        0x98   /* layout; scene.c */
#define L_UNIT_HASHES       0xA0   /* layout; scene.c */
#define L_LOCAL_POSITION    0x24   /* image: "4883c124" in B_HEX_LOCAL_POSITION */
#define L_RIG_NODES         63     /* same; scene.c */
#define L_RIG_NODE          5      /* same; scene.c (hash 0x9B115563) */
#define L_RIG_CAMERA_NODE   50     /* same; scene.c */
#define L_POOL_HEADER       0x30   /* layout; depth.c (0x88 bytes from pool+0x30: maps at +0 and +0x40, rows +0x18, +0x28, +0x30, +0x80) */
#define L_PARAMETERS        0x3F0  /* same; image: imul rax,rdx,0x3f0 at game+0x178759d; NATIVE-ZOOM-STATE-ROUTES.md */
#define L_PARAMETER_KINDS   0x350  /* same; image: add rax,0x350 at game+0x17875a4 */
#define L_DEFINITION_ZOOM   0x74   /* layout; depth.c; NATIVE-ZOOM-STATE-ROUTES.md (0xdc bytes: zoom +0, coefficient +0xC8, near terms +0xD4, +0xD8) */
#define L_PRESET            144    /* same; input-json preset_stride; gains at +4, +8 */
#define L_PLAYER_COUNT      0x88   /* layout; snapshot.c */
#define L_PLAYER_ENTITY     0x3a8  /* layout; snapshot.c */
/* ====================================================================== */
/* TEST ONLY, never defined in a shipped build. tests/run-diff-pose.sh builds a
 * second object with -DTFP_POSE_TEST_OPEN_CONSTANTS='"diff_pose_helper_open.h"':
 * that header redefines the OPEN names above (and B_INPUT_ESTABLISHED) with
 * places in the test's fake game image, so the look-gain normalisation, which
 * refuses on this build, can still be compared with retail's call by call. */
#ifdef TFP_POSE_TEST_OPEN_CONSTANTS
#include TFP_POSE_TEST_OPEN_CONSTANTS
#endif

#define HIGH UINT64_C(0x800000000000)
#define SLOT_COUNT 4u

/* cpp:14-56 */
static TfpPoseHost host;
static _Atomic uintptr_t originals[SLOT_COUNT];
static _Atomic uint32_t installed, armed, reserved, expired, last_error;
static _Atomic uint64_t seen[2], forwarded[2], rig_return, rotation_return;
static _Atomic uint64_t getter_return, active_generation;
static _Atomic uint64_t carrier_return;
static _Atomic uint32_t watched_viewmodel;
static _Atomic uint64_t weapon_updates, zero_restores;
static uint64_t slots[SLOT_COUNT], slot_image = 0;
static bool possible[SLOT_COUNT];
static _Atomic uintptr_t input_original;
static uint64_t input_slot = 0;
static bool input_possible = false;
static _Atomic bool input_mode;
static _Atomic uint64_t input_return;
static _Atomic uint64_t input_watched_request, input_lease_generation;
/* control_lock protects the lease generation and prevents stale GUI handles
 * from changing or ending a later preview. Counter zero is never issued. */
static uint64_t generation_counter = 0, owned_generation = 0, retired_generation = 0;
/* Lifecycle-owned immutable deadline, accessed only under control_lock. */
static uint64_t stats_deadline = 0;

static void control_acquire(void) { host.lock_exclusive(host.user, TFP_POSE_LOCK_CONTROL); }
static void control_release(void) { host.unlock_exclusive(host.user, TFP_POSE_LOCK_CONTROL); }
static void policy_acquire(void) { host.lock_exclusive(host.user, TFP_POSE_LOCK_POLICY); }
static void policy_release(void) { host.unlock_exclusive(host.user, TFP_POSE_LOCK_POLICY); }
static void policy_acquire_shared(void) { host.lock_shared(host.user, TFP_POSE_LOCK_POLICY); }
static void policy_release_shared(void) { host.unlock_shared(host.user, TFP_POSE_LOCK_POLICY); }

/* cpp:73-82 */
#define RIG_NODE ((uint32_t)L_RIG_NODE)
static const uint32_t API_OFFSETS[SLOT_COUNT] = {L_API_POSITION, L_API_ROTATION, L_API_BRANCH, L_API_LOCAL};
static const uint32_t WRAPPERS[SLOT_COUNT] = {B_EXE_POSITION, B_EXE_ROTATION, B_EXE_BRANCH_ROTATION, B_EXE_LOCAL_POSITION};
static const char *const PREFIX[SLOT_COUNT] = {B_HEX_POSITION, B_HEX_ROTATION, B_HEX_BRANCH_ROTATION, B_HEX_LOCAL_POSITION};
/* cpp:83-86 */
enum { OK = 0, INVALID_CONFIG = 1, BUSY = 2, READ_FAILED = 3, IMAGE_MISMATCH = 4,
       CODE_MISMATCH = 5, API_MISMATCH = 6, OWNER_MISMATCH = 7, UNIT_MISMATCH = 8, PIN_FAILED = 9,
       SLOT_NOT_WRITABLE = 10, CAS_MISMATCH = 11, CAS_FAULT = 12, CLEANUP_PENDING = 13, SIGHT_NODE_UNAVAILABLE = 14,
       GRAPH_MISMATCH = 15, POSE_INVALID = 16, TOKEN_MISMATCH = 17, DEADLINE_EXPIRED = 18 };
/* cpp:87-105 */
typedef struct DepthProbe { uint64_t buckets; uint32_t cap, start, count; uint64_t rows[64]; } DepthProbe;
typedef struct DepthData {
  uint64_t pool, world, settings, data, resource_table, resource_row, parameters, packed;
  uint8_t pool_bytes[0x88], data_bytes[0xdc], parameter_bytes[16], resource_bytes[16];
  DepthProbe primary, overrides; float degrees, zoom[3], coefficient[3], near_term[2];
} DepthData;
typedef struct DepthState {
  bool checked, code_ok, signature_set, valid, previous_known;
  uint32_t signature[4]; uint64_t next_check, retry_at, refreshes, checks;
  float current, previous, current_reference, previous_reference;
  TfpPoseDepthStats observed;
  DepthData data;
} DepthState;
typedef struct DepthObservation {
  uint64_t native_stack, generation, request; uint32_t thread; bool ready;
  uint8_t frame[0x20];
  uint8_t request_state[0x24];
} DepthObservation;
typedef struct CarrierPending { DepthObservation observation; float delta; bool ready, steady; /* steady: the delta is the held depth's (src/depth_steady.h), not retail's */ } CarrierPending;
/* cpp:106-127 */
typedef struct Policy {
  TfpPoseConfig c; uint64_t camera, controller, descriptor, object, weapon_object, deadline;
  uint8_t descriptor_bytes[20], weapon_bytes[20];
  uint64_t graph_vtable, graph_links, graph_hashes;
  bool native_aiming, shoulder_aiming, full_ads, rig_shifted, rotation_shifted, carrier_shifted;
  bool zoom_policy_active;
  float baseline[4], rotation_baseline[4];
  float carrier_baseline[4], carrier_written[3]; uint64_t carrier_poses;
  bool carrier_checked, carrier_code_ok;
  float motion[3];
  float ads_placement[3];
  bool authored_ads_rotation;
  float rotation[4]; /* {0,0,0,1} */
  TfpPhase shared; float weight, target /* 1 */, start_weight; uint64_t blend_start;
  /* Entry guard and trace for rig node 5's rotation (see entry_rotation). */
  bool entry_position_seen, steady;
  uint64_t armed_at; bool guard_have[2], guard_seen; uint32_t guarded, trace_count, guard_still; float guard_previous[4];
  TfpPoseRotationTraceRow trace[TFP_POSE_ROTATION_TRACE_ROWS];
  DepthState depth;
  DepthObservation observation;
  CarrierPending carrier;
} Policy;
/* Policy{}: the default member initialisers. Everything 0/false except the
 * identity rotation, the phase's progress 1 and target 1. */
static void policy_init(Policy *p) {
  memset(p, 0, sizeof *p);
  p->rotation[3] = 1;
  tfp_phase_reset(&p->shared);
  p->target = 1;
}
/* cpp:128. The same start-up values, as a static initialiser. */
static Policy policy = { .rotation = {0, 0, 0, 1}, .shared = {.progress = 1}, .target = 1 };

/* cpp:129-149. The display floats are kept as their bit patterns in
 * lock-free 32-bit atomics (C has no always-lock-free guarantee for
 * _Atomic float); the values read back are the values stored. */
typedef struct Display { float blend, target; } Display;
static Display display_cache = {0, 1}; /* control_lock only; callbacks never touch it. */
static _Atomic uint32_t display_sequence;
static _Atomic uint32_t display_blend, display_target = UINT32_C(0x3f800000);
_Static_assert(ATOMIC_INT_LOCK_FREE == 2 && sizeof(float) == 4, "Pose diagnostics require lock-free floats");
static uint32_t float_bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float bits_float(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
/* cpp:136-141 */
static void publish_diagnostics(const Policy *p) {
  atomic_fetch_add_explicit(&display_sequence, 1, memory_order_seq_cst);
  atomic_store_explicit(&display_blend, float_bits(p->weight), memory_order_seq_cst);
  atomic_store_explicit(&display_target, float_bits(p->target), memory_order_seq_cst);
  atomic_fetch_add_explicit(&display_sequence, 1, memory_order_seq_cst);
}
/* cpp:142-149 */
static void read_diagnostics(TfpPoseStats *s) {
  const uint32_t before = atomic_load_explicit(&display_sequence, memory_order_seq_cst);
  if ((before & 1) == 0) {
    Display next;
    next.blend = bits_float(atomic_load_explicit(&display_blend, memory_order_seq_cst));
    next.target = bits_float(atomic_load_explicit(&display_target, memory_order_seq_cst));
    if (before == atomic_load_explicit(&display_sequence, memory_order_seq_cst)) display_cache = next;
  }
  s->blend = display_cache.blend; s->target_weight = display_cache.target;
}
/* cpp:150-157 */
static bool read(uint64_t at, void *out, size_t length) {
  if (at < 65536 || at >= HIGH || length > 1024 || at + length >= HIGH) return false;
  return host.read(host.user, at, out, length);
}
/* cpp:158 */
#define value(at, out) read((at), &(out), sizeof(out))
/* cpp:159 */
static bool pointer(uint64_t at, uint64_t *out) { return value(at, *out) && *out >= 65536 && *out < HIGH; }
/* cpp:160 */
static void error(uint32_t code) { atomic_store_explicit(&last_error, code, memory_order_release); }
/* cpp:161 */
static bool reject(uint32_t code) { error(code); return false; }
/* cpp:162-168. IMAGE_DOS_HEADER is 64 bytes (e_magic +0, e_lfanew +0x3c);
 * IMAGE_NT_HEADERS64 is 264 (Signature +0, Machine +4, TimeDateStamp +8,
 * OptionalHeader.Magic +24, SizeOfImage +80). Same two reads, same sizes. */
static bool image(uint64_t base, uint32_t stamp, uint32_t length) {
  uint8_t dos[64] = {0}, nt[264] = {0};
  uint16_t e_magic = 0, machine = 0, magic = 0; int32_t e_lfanew = 0; uint32_t signature = 0, time_date_stamp = 0, size_of_image = 0;
  if (!read(base, dos, sizeof dos)) return false;
  memcpy(&e_magic, dos, 2); memcpy(&e_lfanew, dos + 0x3c, 4);
  if (!(e_magic == 0x5A4D && e_lfanew >= 64 && e_lfanew <= 4096)) return false;
  if (!read(base + (uint64_t)(int64_t)e_lfanew, nt, sizeof nt)) return false;
  memcpy(&signature, nt, 4); memcpy(&machine, nt + 4, 2); memcpy(&time_date_stamp, nt + 8, 4);
  memcpy(&magic, nt + 24, 2); memcpy(&size_of_image, nt + 80, 4);
  return signature == 0x00004550 && machine == 0x8664 && magic == 0x20b
      && time_date_stamp == stamp && size_of_image == length;
}
/* cpp:169-177 */
static uint8_t hex_digit(char c) { return (uint8_t)(c <= '9' ? c - '0' : c - 'a' + 10); }
static bool bytes(uint64_t at, const char *hex) {
  uint8_t actual[128] = {0}; const size_t count = strlen(hex)/2;
  if (!count || count > sizeof actual || !read(at, actual, count)) return false;
  for (size_t i = 0; i < count; ++i) {
    if (actual[i] != (uint8_t)((hex_digit(hex[i*2]) << 4) | hex_digit(hex[i*2+1]))) return false;
  }
  return true;
}
/* cpp:178-182 */
static bool executable(uint64_t at) { return host.executable(host.user, at); }
/* cpp:183-190 */
static bool unit_object(const TfpPoseConfig *c, uint32_t ref, uint64_t *object) {
  uint64_t registry = 0, gens = 0, objects = 0; uint32_t cap = 0; uint8_t generation = 0;
  const uint32_t index = ref & 0x3fffff;
  return pointer(c->exe + B_EXE_REGISTRY, &registry) && value(registry + L_REGISTRY_CAPACITY, cap) && index < cap && cap <= 0x400000
      && pointer(registry + L_REGISTRY_GENERATIONS, &gens) && value(gens + index, generation)
      && generation == (uint8_t)(ref >> 22) && pointer(registry + L_REGISTRY_UNITS, &objects)
      && pointer(objects + (uint64_t)index*8, object);
}
/* The action hold (pose_helper.h). hold_mode: the tolerance of ownership(). hold_probe: set by the first
 * tfp_pose_set_hold and never cleared; only then is anything counted. Both 0 unless the binder calls it. */
static _Atomic uint32_t hold_mode, hold_probe;
static _Atomic uint64_t hold_node5_calls;
/* cpp:191-228. With `hold` false every read, its order and every answer are retail's. */
static bool ownership(Policy *p, bool entering) {
  const bool hold = !entering && atomic_load_explicit(&hold_mode, memory_order_relaxed) != 0;
  const TfpPoseConfig *c = &p->c;
  uint64_t ctx = 0, owner = 0, camera = 0, controller = 0, descriptor = 0, object = 0, weapon_object = 0;
  uint32_t count = 0, head = 0, tail = 0, viewmodel = 0;
  uint8_t d[20] = {0}, wd[20] = {0}, flags[24] = {0}, controller_bytes[6] = {0}; uint32_t request[4] = {0};
  if (!pointer(c->game + B_GAME_CONTEXT, &ctx) || ctx != c->context || !pointer(ctx, &camera)
      || !pointer(c->game + B_GAME_AVATAR_OWNER, &owner) || owner != c->avatar_owner
      || !pointer(c->game + B_GAME_CONTROLLER, &controller) || !read(controller, controller_bytes, sizeof controller_bytes)
      || !value(owner + L_AVATAR_COUNT, count) || count == 0 || count > 8 || c->avatar_index >= count
      || !pointer(owner + L_AVATAR_ROWS + (uint64_t)c->avatar_index*8, &descriptor)
      || !read(descriptor, d, sizeof d) || !read(owner + B_AVATAR_FLAGS + (uint64_t)c->avatar_index*B_AVATAR_STRIDE, flags, sizeof flags)
      || !value(owner + B_AVATAR_RIG + (uint64_t)c->avatar_index*B_AVATAR_STRIDE, viewmodel)
      || viewmodel != c->viewmodel || !unit_object(c, c->viewmodel, &object)) return false;
  /* The weapon's unit and descriptor: in a hold the gun may be put away or gone; the record decides below. */
  const bool weapon_read = unit_object(c, c->weapon_unit, &weapon_object) && read(c->weapon_descriptor, wd, sizeof wd);
  if (!weapon_read && !hold) return false;
  if (!value(ctx + L_CONTEXT_HEAD, head) || !value(ctx + L_CONTEXT_TAIL, tail)
      || head >= 32 || tail >= 32) return false;
  uint32_t avatar = 0, body = 0; memcpy(&avatar, d + 8, 4); memcpy(&body, d + 12, 4);
  uint32_t weapon = 0, weapon_unit = 0; memcpy(&weapon, wd + 8, 4); memcpy(&weapon_unit, wd + 12, 4);
  if (avatar != c->avatar || body != c->body || (flags[L_FLAG_SEATED] & 4) != 0) return false;
  if (!hold && (weapon != c->weapon || weapon_unit != c->weapon_unit)) return false;
  if (!hold) for (unsigned i = 0; i < 6; ++i) if (i != L_CONTROL_AIM && controller_bytes[i]) return false;
  /* Bit48 belongs to the native camera lifecycle. Normal accepted aim uses
   * agreeing character bits82/125 even while a forced kind6 keeps48 clear.
   * An inconsistent pair cannot claim shoulder presentation; no extra reads. */
  p->native_aiming = (flags[L_FLAG_NATIVE_AIM] & 1) != 0;
  p->shoulder_aiming = (flags[L_FLAG_SHOULDER_A] & 4) != 0 && (flags[L_FLAG_SHOULDER_B] & 32) != 0;
  p->full_ads = controller_bytes[L_CONTROL_AIM] != 0;
  if (entering && (p->native_aiming || p->full_ads)) return false;
  const uint64_t top = head == tail ? ctx + L_CONTEXT_IDLE : ctx + L_CONTEXT_RECORDS + ((tail+31)%32)*L_CONTEXT_RECORD;
  if (!read(top, request, sizeof request) || request[1] != c->avatar) return false;
  if (entering) {
    if ((request[0] & 0xffff) != 4 || c->request != ctx + L_CONTEXT_RECORDS + tail*L_CONTEXT_RECORD) return false;
    p->camera = camera; p->controller = controller; p->descriptor = descriptor; p->object = object; p->weapon_object = weapon_object;
    memcpy(p->descriptor_bytes, d, 20); memcpy(p->weapon_bytes, wd, 20);
    return true;
  }
  if (hold) /* the same owned record, avatar, body and rig; the weapon's side is not asked */
    return top == c->request && (request[0] & 0xffff) == 6 && request[3] == c->body
        && camera == p->camera && controller == p->controller && descriptor == p->descriptor && object == p->object
        && memcmp(d, p->descriptor_bytes, 20) == 0;
  return top == c->request && (request[0] & 0xffff) == 6 && request[2] == c->weapon && request[3] == c->body
      && camera == p->camera && controller == p->controller && descriptor == p->descriptor && object == p->object
      && weapon_object == p->weapon_object && memcmp(d, p->descriptor_bytes, 20) == 0 && memcmp(wd, p->weapon_bytes, 20) == 0;
}

/* cpp:230-258. This candidate supports the captured 63-node rig only. The
 * camera is a sibling of node 5, while both arms and the weapon attachment
 * descend from node 5. The executable() test is retail's non-fixture path. */
typedef struct GraphRow { uint32_t node, hash, link; } GraphRow;
static const GraphRow graph_rows[] = { /* same in both builds; scene.c holds the same table */
  {0, 0x4A182741, 0}, {1, 0x7F30E61C, 1}, {2, 0x32623EB2, 0x10001},
  {3, 0x975CEBBD, 0x20001}, {4, 0xBCCF91E5, 0x30001}, {5, 0x9B115563, 0x40001},
  {7, 0x6F3BD021, 0x50001}, {8, 0x4FDD3337, 0x70001},
  {10, 0x03752C98, 0x70001}, {12, 0x1CA2CD66, 0xA0001}, {13, 0x5C0C68D3, 0xC0001},
  {30, 0x1620B2CE, 0x70001}, {32, 0x31E34DAC, 0x1E0001}, {34, 0x248004F5, 0x200001},
  {50, 0x60ED8C39, 0x40001}};
static bool graph(Policy *p, bool capture) {
  uint64_t vtable = 0, getter = 0, links = 0, hashes = 0; uint32_t count = 0;
  if (!pointer(p->object, &vtable) || !pointer(vtable + L_VT_GRAPH_GETTER, &getter) || getter != p->c.exe + B_EXE_GRAPH_GETTER
      || !bytes(getter, B_HEX_GRAPH_GETTER) || !value(p->object + L_UNIT_COUNT, count) || count != L_RIG_NODES
      || !pointer(p->object + L_UNIT_LINKS, &links) || !pointer(p->object + L_UNIT_HASHES, &hashes)) return false;
  if (!executable(getter)) return false;
  if (!capture && (vtable != p->graph_vtable || links != p->graph_links || hashes != p->graph_hashes)) return false;
  /* Count63 proves these two complete arrays. Keep every selected-node
   * comparison live, but copy each array once instead of 30 scalar reads. */
  uint32_t current_hashes[L_RIG_NODES], current_links[L_RIG_NODES];
  if (!read(hashes, current_hashes, sizeof current_hashes) || !read(links, current_links, sizeof current_links)) return false;
  for (size_t i = 0; i < sizeof graph_rows/sizeof graph_rows[0]; ++i) {
    const GraphRow *row = &graph_rows[i];
    if (current_hashes[row->node] != row->hash || current_links[row->node] != row->link) return false;
  }
  if (capture) { p->graph_vtable = vtable; p->graph_links = links; p->graph_hashes = hashes; }
  return true;
}

/* cpp:260-266 */
static uint64_t tick(void) { return host.tick(host.user); }
/* cpp:267-282. The alignment test stays here; the page test is the host's. */
static bool writable_slot(uint64_t slot, uint64_t exe) {
  if ((slot & 7) != 0) return false;
  return host.writable_slot(host.user, slot, exe);
}
/* cpp:283-288 */
static bool slot_read(uint64_t at, uint64_t *current) {
  return value(at, *current);
}
/* cpp:289-306 */
static bool cas(uint64_t slot, uint64_t expected, uint64_t desired, uint64_t *observed) {
  return host.cas(host.user, slot, expected, desired, observed);
}
/* cpp:307-329. The non-fixture path: each slot must hold exe + wrapper. */
static bool contract(const TfpPoseConfig *c, uint64_t *out, uint64_t *addresses) {
  if (!image(c->game, B_GAME_STAMP, B_GAME_SIZE) || !image(c->exe, B_EXE_STAMP, B_EXE_SIZE)) return reject(IMAGE_MISMATCH);
  if (!bytes(c->game + B_GAME_CARRIER_CALL, B_HEX_CARRIER_CALL) || !bytes(c->game + B_GAME_CARRIER_PAIR, B_HEX_CARRIER_PAIR)
      || !bytes(c->game + B_GAME_RIG_CALL, B_HEX_RIG_CALL) || !bytes(c->game + B_GAME_ROTATION_CALL, B_HEX_ROTATION_CALL)
      || !bytes(c->game + B_GAME_OBSERVE_CALL, B_HEX_OBSERVE_CALL)) return reject(CODE_MISMATCH);
  uint64_t root = 0, api = 0;
  if (!pointer(c->game + B_GAME_API_ROOT, &root) || !pointer(root + L_API_UNIT, &api)) return reject(API_MISMATCH);
  for (unsigned i = 0; i < SLOT_COUNT; ++i) {
    addresses[i] = api + API_OFFSETS[i];
    if (!writable_slot(addresses[i], c->exe)) return reject(SLOT_NOT_WRITABLE);
    uint64_t current = 0; if (!slot_read(addresses[i], &current)) return reject(READ_FAILED);
    out[i] = current;
    if (current != c->exe + WRAPPERS[i]) return reject(API_MISMATCH);
    if (!bytes(current, PREFIX[i])) return reject(CODE_MISMATCH);
    if (!executable(current)) return reject(API_MISMATCH);
  }
  return true;
}
/* cpp:330, 1345-1348 */
static uint64_t callback(unsigned which) {
  if (which == 3) return atomic_load(&input_mode) ? host.hooks[TFP_POSE_HOOK_GETTER_INPUT] : host.hooks[TFP_POSE_HOOK_GETTER];
  return which == 2 ? host.hooks[TFP_POSE_HOOK_BRANCH_ROTATION] : (which ? host.hooks[TFP_POSE_HOOK_ROTATION] : host.hooks[TFP_POSE_HOOK_POSITION]);
}
/* cpp:331-339 */
static bool dispatch_owned(const TfpPoseConfig *c) {
  uint64_t root = 0, api = 0;
  if (!pointer(c->game + B_GAME_API_ROOT, &root) || !pointer(root + L_API_UNIT, &api)) return false;
  for (unsigned i = 0; i < SLOT_COUNT; ++i) {
    uint64_t current = 0;
    if (api + API_OFFSETS[i] != slots[i] || !slot_read(slots[i], &current) || current != callback(i)) return false;
  }
  return true;
}
/* cpp:340-346. The native postpass visits every kind-6 ring entry. Its
 * quaternion lives on the stack, so only a sole owned entry proves which
 * request supplied it. */
static bool sole_request(const Policy *p) {
  uint32_t head = 0, tail = 0;
  return value(p->c.context + L_CONTEXT_HEAD, head) && value(p->c.context + L_CONTEXT_TAIL, tail) && head < 32 && tail < 32
      && ((tail + 32 - head)%32) == 1 && p->c.request == p->c.context + L_CONTEXT_RECORDS + (uint64_t)head*L_CONTEXT_RECORD;
}
/* cpp:347-350 */
static bool rotation_uncoupled(const Policy *p) {
  float coupling = 1;
  return value(p->c.request + L_REQUEST_COUPLING, coupling) && coupling == 0.0f;
}
/* cpp:351-353, template field<T> */
static uint32_t field_u32(const uint8_t *block, unsigned offset) { uint32_t result = 0; memcpy(&result, block + offset, sizeof result); return result; }
static uint64_t field_u64(const uint8_t *block, unsigned offset) { uint64_t result = 0; memcpy(&result, block + offset, sizeof result); return result; }
static float field_f32(const uint8_t *block, unsigned offset) { float result = 0; memcpy(&result, block + offset, sizeof result); return result; }
/* cpp:354 */
static bool address(uint64_t at) { return at >= 65536 && at < HIGH; }
/* cpp:355-374. Cache the exact bounded probe prefix, so periodic validation
 * does not repeat a registry scan. The native key multiplication
 * intentionally wraps32 bits. */
static bool depth_map(const uint8_t *header, uint32_t key, DepthProbe *out, uint32_t *index) {
  out->buckets = field_u64(header, 0); out->cap = field_u32(header, 8);
  const uint32_t empty = field_u32(header, 12), mult = field_u32(header, 16);
  if (!address(out->buckets) || !out->cap || out->cap > 0x100000 || (out->cap & (out->cap - 1)) || key == empty) return false;
  out->start = (key*mult) & (out->cap - 1); out->count = 0; *index = UINT32_MAX;
  while (out->count < out->cap && out->count < 64) {
    const uint32_t at = (out->start + out->count) & (out->cap - 1);
    uint32_t n = out->cap - at; if (n > 8) n = 8; if (n > out->cap - out->count) n = out->cap - out->count; if (n > 64 - out->count) n = 64 - out->count;
    uint64_t rows[8] = {0}; if (!read(out->buckets + (uint64_t)at*8, rows, n*8)) return false;
    for (uint32_t i = 0; i < n; ++i) {
      const uint64_t row = rows[i]; out->rows[out->count++] = row;
      const uint32_t entity = (uint32_t)row;
      if (entity == key) { *index = (uint32_t)(row >> 32); return true; }
      if (entity == empty) return true;
    }
  }
  return false;
}
/* cpp:375-381 */
static bool depth_probe_valid(const DepthProbe *probe) {
  if (!probe->count || probe->count > 64) return false;
  uint64_t rows[64] = {0}; uint32_t first = probe->cap - probe->start; if (first > probe->count) first = probe->count;
  if (!read(probe->buckets + (uint64_t)probe->start*8, rows, first*8)) return false;
  if (first < probe->count && !read(probe->buckets, rows + first, (probe->count - first)*8)) return false;
  return memcmp(rows, probe->rows, probe->count*8) == 0;
}
/* cpp:382-394 */
static bool depth_data_valid(const Policy *p, const DepthData *d) {
  uint64_t pool = 0, world = 0, settings = 0, table = 0; uint8_t bytes_[0xdc] = {0}; float degrees = 0;
  if (!pointer(p->c.game + B_GAME_WEAPON_POOL, &pool) || pool != d->pool || !pointer(p->c.game + B_GAME_WORLD, &world) || world != d->world
      || !pointer(p->c.game + B_GAME_SETTINGS, &settings) || settings != d->settings
      || !read(pool + L_POOL_HEADER, bytes_, sizeof d->pool_bytes) || memcmp(bytes_, d->pool_bytes, sizeof d->pool_bytes)
      || !depth_probe_valid(&d->primary) || !depth_probe_valid(&d->overrides)
      || !read(d->parameters, bytes_, 16) || memcmp(bytes_, d->parameter_bytes, 16)
      || !read(d->data + L_DEFINITION_ZOOM, bytes_, sizeof d->data_bytes) || memcmp(bytes_, d->data_bytes, sizeof d->data_bytes)
      || !value(settings + B_SETTINGS_DEGREES, degrees) || degrees != d->degrees) return false;
  if (d->resource_row && (!pointer(world + B_WORLD_RESOURCES, &table) || table != d->resource_table
      || !read(d->resource_row, bytes_, 16) || memcmp(bytes_, d->resource_bytes, 16))) return false;
  return true;
}
/* cpp:395-445 */
static bool depth_data(const Policy *p, DepthData *d, uint32_t *selected) {
  if (!pointer(p->c.game + B_GAME_WEAPON_POOL, &d->pool) || !pointer(p->c.game + B_GAME_WORLD, &d->world)
      || !pointer(p->c.game + B_GAME_SETTINGS, &d->settings) || !read(d->pool + L_POOL_HEADER, d->pool_bytes, sizeof d->pool_bytes)) return false;
  uint32_t wi = UINT32_MAX, di = UINT32_MAX;
  if (!depth_map(d->pool_bytes, p->c.weapon, &d->primary, &wi) || wi >= 4096
      || !depth_map(d->pool_bytes + 0x40, p->c.weapon, &d->overrides, &di)) return false;
  const uint64_t descriptors = field_u64(d->pool_bytes, 0x18);
  uint64_t descriptor = 0; if (!address(descriptors) || !pointer(descriptors + (uint64_t)wi*8, &descriptor)
      || descriptor != p->c.weapon_descriptor) return false;
  if (di != UINT32_MAX) {
    const uint64_t data = field_u64(d->pool_bytes, 0x80);
    if (!address(data) || di >= 4096) return false;
    d->data = data + (uint64_t)di*B_DEFINITION;
  } else {
    uint64_t resource = 0; memcpy(&resource, p->weapon_bytes, 8);
    if (!resource || !pointer(d->world + B_WORLD_RESOURCES, &d->resource_table)) return false;
    const uint32_t bucket = (uint32_t)(resource%B_RESOURCE_BUCKETS);
    for (uint32_t n = 0; n < B_RESOURCE_BUCKETS;) {
      const uint32_t at = (bucket + n)%B_RESOURCE_BUCKETS; uint32_t count = B_RESOURCE_BUCKETS - at; if (count > 16) count = 16; if (count > B_RESOURCE_BUCKETS - n) count = B_RESOURCE_BUCKETS - n;
      uint8_t rows[256] = {0}; if (!read(d->resource_table + (uint64_t)at*16, rows, count*16)) return false;
      for (uint32_t i = 0; i < count; ++i, ++n) {
        const uint64_t key = field_u64(rows, i*16); if (!key) return false;
        if (key == resource) {
          di = field_u32(rows, i*16 + 8); if (di >= B_RESOURCE_BUCKETS) return false;
          d->resource_row = d->resource_table + (uint64_t)(at + i)*16;
          memcpy(d->resource_bytes, rows + i*16, 16); break;
        }
      }
      if (d->resource_row) break;
    }
    if (!d->resource_row) return false;
    d->data = d->resource_table + B_RESOURCE_RECORDS + (uint64_t)di*B_DEFINITION;
  }
  const uint64_t parameters = field_u64(d->pool_bytes, 0x28), packed = field_u64(d->pool_bytes, 0x30);
  if (!address(parameters) || !address(packed)) return false;
  d->parameters = parameters + (uint64_t)wi*L_PARAMETERS + L_PARAMETER_KINDS; d->packed = packed + (uint64_t)wi*12 + 4;
  uint32_t bits = 0;
  if (!read(d->parameters, d->parameter_bytes, 16) || !value(d->packed, bits)
      || !read(d->data + L_DEFINITION_ZOOM, d->data_bytes, sizeof d->data_bytes) || !value(d->settings + B_SETTINGS_DEGREES, d->degrees)) return false;
  unsigned matches = 0; for (unsigned i = 0; i < 4; ++i) if (field_u32(d->parameter_bytes, i*4) == 1) ++matches;
  if (matches != 1) return false;
  *selected = (bits >> 4) & 3; if (*selected > 2) return false;
  for (unsigned i = 0; i < 3; ++i) {
    d->zoom[i] = field_f32(d->data_bytes, i*4); d->coefficient[i] = field_f32(d->data_bytes, 0xC8 + i*4);
    if (!isfinite(d->zoom[i]) || d->zoom[i] < .1f || d->zoom[i] > 100
        || !isfinite(d->coefficient[i]) || d->coefficient[i] < .0001f || d->coefficient[i] > 100) return false;
  }
  d->near_term[0] = field_f32(d->data_bytes, 0xD4); d->near_term[1] = field_f32(d->data_bytes, 0xD8);
  if (!isfinite(d->degrees) || d->degrees < 10 || d->degrees > 170
      || !isfinite(d->near_term[0]) || !isfinite(d->near_term[1]) || fabsf(d->near_term[0]) > 1 || fabsf(d->near_term[1]) > 1) return false;
  return depth_data_valid(p, d);
}
/* cpp:446-458. This build's weapon overrides and resource rows use 0x4D0
 * records. Pin the owning getters as well as the evaluator frame we consume. */
static bool depth_code(const TfpPoseConfig *c) {
  return bytes(c->game + B_DEPTH_PIN_1, B_DEPTH_HEX_1)
      && bytes(c->game + B_DEPTH_PIN_2, B_DEPTH_HEX_2)
      && bytes(c->game + B_DEPTH_PIN_3, B_DEPTH_HEX_3)
      && bytes(c->game + B_DEPTH_PIN_4, B_DEPTH_HEX_4)
      && bytes(c->game + B_DEPTH_PIN_5, B_DEPTH_HEX_5)
      && bytes(c->game + B_DEPTH_PIN_6, B_DEPTH_HEX_6)
      && bytes(c->game + B_DEPTH_PIN_7, B_DEPTH_HEX_7)
      && bytes(c->game + B_DEPTH_PIN_8, B_DEPTH_HEX_8)
      && bytes(c->game + B_DEPTH_PIN_9, B_DEPTH_HEX_9);
}
/* cpp:459-471 */
static bool depth_reference(const DepthData *d, uint32_t selected, float distance, float *reference) {
  const float rad = .01745329238474369f;
  /* Authored zoom arrays are not ordered: the captured 3.5/2.5/1 optic
   * puts its widest lens last. Keep that lens as the presentation reference. */
  unsigned widest = 0; for (unsigned i = 1; i < 3; ++i) if (d->zoom[i] < d->zoom[widest]) widest = i;
  const float angle = ((d->degrees/d->zoom[selected])*rad)*.5f, base = ((d->degrees/d->zoom[widest])*rad)*.5f;
  if (angle < .0001f || angle > 1.48f || base < .0001f || base > 1.48f || !isfinite(distance) || fabsf(distance) > 4) return false;
  if (selected == widest) { *reference = distance; return true; }
  const float denominator = tanf(base)*d->coefficient[widest], current = tanf(angle)*d->coefficient[selected];
  const float u = (d->degrees - 45.0f)/45.0f, base_term = (1 - u)*d->near_term[0] + u*d->near_term[1];
  *reference = base_term + (distance - base_term)*(current/denominator);
  return isfinite(*reference) && fabsf(*reference) <= 4;
}
/* cpp:472-479 */
static float depth_pass(DepthState *state, uint32_t reason) {
  state->observed.supported = 0; state->observed.reason = reason; state->observed.correction = 0;
  if (reason == 1) ++state->observed.ads_bypassed; else ++state->observed.unsupported;
  return 0;
}
/* Not in retail: what the "Gate:" line reports (src/gate_log.h), under the policy lock, and two switches the binder
 * sets. gate_reads: the avatar's main row is read too. depth_off: the depth term adds nothing (depth_off.txt). */
static TfpGateLog gate;
static _Atomic uint32_t gate_reads, depth_off;
/* One frame in which the rig root is written, after the depth term is settled. `added`: what it added. The gate
 * bytes and the lens are the ones read with the evaluator's locals in this frame, whatever the term did with them. */
static void gate_frame(const DepthObservation *observation, uint64_t native_stack, float added, bool steadied) {
  TfpGateFrame f; memset(&f, 0, sizeof f);
  f.lens_read = native_stack && observation->ready && observation->native_stack == native_stack;
  if (f.lens_read) {
    f.avatar_bit = observation->frame[0]; f.request_8f = observation->request_state[0xF]; f.request_8d = observation->request_state[0xD];
    f.request_88 = field_f32(observation->request_state, 8);
    f.target = field_f32(observation->frame, 0x18); f.lens = field_f32(observation->frame, 0x1C);
  }
  f.reason = native_stack ? policy.depth.observed.reason : (uint32_t)TFP_GATE_NO_FRAME; f.correction = added;
  if (steadied && f.reason == TFP_GATE_WORKED) f.reason = TFP_GATE_STEADY; /* the term had a number; the held depth went out in its place */
  if (atomic_load_explicit(&gate_reads, memory_order_relaxed)) {
    const uint64_t row = policy.c.avatar_owner + B_AVATAR_MAIN + (uint64_t)policy.c.avatar_index*B_AVATAR_MAIN_STRIDE;
    uint8_t head[0x3e] = {0}; float scalar = 0;
    f.row_asked = true; f.row_read = policy.c.avatar_index < 8 && read(row, head, sizeof head) && value(row + 0x11ac, scalar);
    if (f.row_read) { f.row_5 = head[5]; f.row_3d = head[0x3d]; f.row_11ac = scalar; }
  }
  gate.now.generation = atomic_load_explicit(&active_generation, memory_order_relaxed);
  tfp_gate_note(&gate, &f);
}
/* cpp:480-554 */
static float depth_delta(Policy *p, const DepthObservation *observation, uint64_t native_stack, uint64_t now) {
  if (!native_stack) return 0;
  DepthState *s = &p->depth;
  /* The selection policy supplies the authored zoom reference to native
   * lens/depth calculations. Applying the old selected-distance inverse as
   * well would interpret that reference as a magnified distance twice. */
  if (p->zoom_policy_active) {
    s->observed.supported = 0; s->observed.reason = 10; s->observed.correction = 0;
    return 0;
  }
  if (p->full_ads) return depth_pass(s, 1);
  if (!observation->ready || observation->native_stack != native_stack || observation->request != p->c.request
      || observation->generation != atomic_load_explicit(&active_generation, memory_order_relaxed)
      || observation->thread != host.thread(host.user)) return depth_pass(s, 3);
  if (!s->checked) { s->checked = true; s->code_ok = depth_code(&p->c); }
  if (!s->code_ok) return depth_pass(s, 2);
  uint8_t request[0x24] = {0}; const uint8_t *frame = observation->frame;
  if (!read(p->c.request + L_REQUEST_STATE, request, sizeof request)
      || memcmp(request, observation->request_state, 8)
      || memcmp(request + 0xC, observation->request_state + 0xC, sizeof request - 0xC)) return depth_pass(s, 3);
  const float current = field_f32(request, 0x10), previous = field_f32(request, 0x1C), prior = field_f32(request, 0x20);
  const float target = field_f32(frame, 0x18), lens = field_f32(frame, 0x1C), mix = field_f32(request, 8);
  if (request[0xD] > 1 || request[0xF] != 0 || frame[0] != 0 || frame[1] > 1 || field_f32(request, 0x18) != 0) return depth_pass(s, 4);
  /* Not in retail: depth_off.txt. The term adds nothing on any frame, as on a gated one. */
  if (atomic_load_explicit(&depth_off, memory_order_relaxed)) return depth_pass(s, TFP_GATE_SWITCHED_OFF);
  if (field_u32(request, 4) != field_u32(frame, 0x1C)
      || !isfinite(target) || target < .0002f || target > 2.96f || !isfinite(lens) || lens < .0002f || lens > 2.96f
      || !isfinite(prior) || prior < 0 || prior > 2.96f || !isfinite(current) || !isfinite(previous)
      || fabsf(current) > 4 || fabsf(previous) > 4 || (request[0xD] && (!isfinite(mix) || mix < 0 || mix > 1))) return depth_pass(s, 5);
  const float span = fabsf(target - prior);
  float t = span > 1.1920928955078125e-7f ? fabsf(target - lens)/span : 0;
  if (t > 1) t = 1;
  const uint32_t signature[4] = {field_u32(request, 0x10), field_u32(request, 0x1C),
      field_u32(request, 0x20), 0};
  const bool changed = !s->signature_set || memcmp(signature, s->signature, sizeof signature) != 0;
  if (changed || (!s->valid && now >= s->retry_at)) {
    const bool old_valid = s->valid; const float old_current = s->current, old_previous = s->previous;
    const float old_reference = s->current_reference, old_previous_reference = s->previous_reference;
    const bool old_previous_known = s->previous_known;
    s->signature_set = true; memcpy(s->signature, signature, sizeof signature); s->valid = false; s->previous_known = false;
    s->current = current; s->previous = previous; s->retry_at = now + 500;
    DepthData data; memset(&data, 0, sizeof data); uint32_t selected = 0; ++s->refreshes;
    if (!depth_data(p, &data, &selected) || !depth_reference(&data, selected, current, &s->current_reference)) return depth_pass(s, 6);
    s->observed.zoom_index = selected; s->observed.zoom = data.zoom[selected];
    if (old_valid && previous == old_current) { s->previous_reference = old_reference; s->previous_known = true; }
    else if (old_valid && old_previous_known && previous == old_previous) { s->previous_reference = old_previous_reference; s->previous_known = true; }
    else if (previous == 0) { s->previous_reference = 0; s->previous_known = true; }
    /* A missing previous event cannot be reconstructed from prior lens:
     * it may be an intermediate lens or belong to another definition. */
    s->data = data; s->next_check = now + 500; s->valid = true;
  } else if (s->valid && now >= s->next_check) {
    ++s->checks; s->next_check = now + 500;
    if (!depth_data_valid(p, &s->data)) {
      /* A different definition is not the provenance of an unchanged
       * native distance. Stay passive until a new native event publishes
       * fresh distances; do not reinterpret an old result with new data. */
      s->valid = false; s->retry_at = UINT64_MAX; return depth_pass(s, 9);
    }
  }
  if (!s->valid) return depth_pass(s, 6);
  if (t != 0 && !s->previous_known) return depth_pass(s, 7);
  const float native = (1 - t)*current + t*previous;
  const float reference = (1 - t)*s->current_reference + (t == 0 ? 0 : t*s->previous_reference);
  const float gain = request[0xD] ? mix : 1;
  const float delta = (reference - native)*gain;
  if (!isfinite(delta) || fabsf(delta) > 1) return depth_pass(s, 8);
  s->observed.supported = 1; s->observed.reason = 0; s->observed.native_distance = native;
  s->observed.reference_distance = reference; s->observed.correction = delta;
  s->observed.target_fov = target; s->observed.current_fov = lens; s->observed.gain = gain;
  return delta;
}
/* Not in retail: the rig's depth held steady against the game's unzoom gate (src/depth_steady.h,
 * docs/design/specs/2026-11-23-gate-flip.md "Round 9"). Under the policy lock. steady_mode: 0 the helper as retail has
 * it (none of this is reached); 1 on; 2 switched off (depth_steady_off.txt): the game's depth is still worked out and
 * counted for the "Steady:" line, nothing is added. steady_pair: whether the root has followed node 5 in this lease.
 * One frame of the rig's node 5. It reads what the zoom depth term reads and no more: the lens locals the position
 * getter took in this same evaluation and the request's state again (one guarded read), with the term's own proofs
 * that both belong to this call. True: `*delta` is to go out in place of the term's own number (it can be nothing).
 * `argument` is the game's own forward value for the node, for the line only. */
static TfpSteady steady; static TfpSteadyLog steady_log; static TfpSteadyPair steady_pair;
static _Atomic uint32_t steady_mode;
static bool steady_frame(Policy *p, const DepthObservation *observation, uint64_t native_stack, uint64_t now, uint32_t mode, float argument, float *delta) {
  const uint64_t generation = atomic_load_explicit(&active_generation, memory_order_relaxed);
  uint8_t request[0x24] = {0}; float depth = 0, held = 0; uint32_t reason = TFP_STEADY_NO_LENS; bool bit = false, stopped_now = false;
  *delta = 0;
  /* The root's half is a second call with proofs of its own (carrier_position_locked). Where node 5 was moved in
   * TFP_STEADY_ALONE frames running and the root's half was not done, or in TFP_STEADY_ALONE_LEASE frames of the
   * lease, nothing more is held in this lease: the game's depth is still worked out and counted, as with the switch. */
  const bool stopped = tfp_steady_pair_frame(&steady_pair, generation, &stopped_now);
  if (stopped_now) tfp_steady_stopped(&steady_log, generation, steady_pair.lost, steady_pair.alone);
  /* The lens locals are the evaluator's stack, read by offset: they are a lens only in the code the zoom depth term
   * pins (depth_code, nine places), and this mirrors that code. The term asks before it reads; so does this. */
  DepthState *s = &p->depth;
  if (!s->checked) { s->checked = true; s->code_ok = depth_code(&p->c); }
  if (!s->code_ok) reason = TFP_STEADY_NO_CODE;
  else if (observation->ready && observation->native_stack == native_stack && observation->request == p->c.request && observation->generation == generation
      && observation->thread == host.thread(host.user) && read(p->c.request + L_REQUEST_STATE, request, sizeof request)
      && !memcmp(request, observation->request_state, 8) && !memcmp(request + 0xC, observation->request_state + 0xC, sizeof request - 0xC)
      && field_u32(request, 4) == field_u32(observation->frame, 0x1C)) {
    TfpSteadyInput in; memset(&in, 0, sizeof in);
    in.target = field_f32(observation->frame, 0x18); in.lens = field_f32(observation->frame, 0x1C); in.prior = field_f32(request, 0x20);
    in.current = field_f32(request, 0x10); in.previous = field_f32(request, 0x1C);
    in.bit = observation->frame[0] != 0; in.blend = request[0xD]; in.ability = request[0xF];
    in.mix_before = field_f32(observation->request_state, 8); in.mix_after = field_f32(request, 8);
    in.share = p->weight; in.on = mode == 1 && !stopped;
    /* Where to rest: the selected optic's own distance, or the mod's reference for it where the zoom depth term has
     * one for exactly these distances (then the two agree at rest, and the term's blend over the lens is left out). */
    in.settled = in.current;
    const uint32_t signature[4] = {field_u32(request, 0x10), field_u32(request, 0x1C), field_u32(request, 0x20), 0};
    if (!p->zoom_policy_active && !atomic_load_explicit(&depth_off, memory_order_relaxed) && s->valid && s->signature_set
        && !memcmp(signature, s->signature, sizeof signature)) in.settled = s->current_reference;
    reason = tfp_steady_frame(&steady, &in, generation, (double)now, delta, &depth, &held);
    if (reason == TFP_STEADY_OFF && mode == 1) reason = TFP_STEADY_NO_ROOT;
    bit = in.bit;
  }
  steady_log.now.lease = generation;
  tfp_steady_note(&steady_log, reason, bit, depth, held, *delta, argument);
  return reason == TFP_STEADY_WORKED;
}
/* cpp:555-576. Optional native input policy. The only engine datum it
 * changes is DWORD80 of the exact owned camera6 request. All backing records
 * remain pinned. */
enum { INPUT_READY = 0, INPUT_DISABLED = 1, INPUT_CODE_UNAVAILABLE = 2,
       INPUT_SDK_UNAVAILABLE = 3, INPUT_OWNER = 4, INPUT_SAMPLE = 5, INPUT_MATH = 6, INPUT_POOL = 7,
       INPUT_FOREIGN = 8, INPUT_REENTRY = 9, INPUT_PENDING = 10, INPUT_SOURCE_CHANGED = 11, INPUT_ADS = 12 };
typedef struct InputClone {
  uint64_t game, address; uint32_t original, index;
  uint8_t source[L_PRESET]; bool used, busy, quarantined;
} InputClone;
#define INPUT_CLONES 32u
static InputClone input_clones[INPUT_CLONES];
typedef struct InputPending {
  bool ready, published, poisoned, interrupted, input_dependent;
  uint64_t generation, native_stack, request, camera;
  uint32_t thread, header[4], original, index, clone;
  TfpPoseConfig c; float expected_history[3];
} InputPending;
typedef struct InputState {
  bool requested, code_ok, failed_closed; uint64_t retry_at;
  TfpPoseInputStats stats; InputPending pending;
} InputState;
static InputState input_state;
/* cpp:582-601. Whole evaluator plus both outer preset consumers close the
 * synthetic-index domain. The second FOV copy cannot interpret the changed
 * gain fields. Same 16 rows in retail's order; lengths are retail's. */
typedef struct InputPin { uint32_t offset, length; uint64_t fnv; bool exe; } InputPin;
static const InputPin INPUT_PINS[] = {
  {B_PIN_EVALUATOR, B_PIN_EVALUATOR_LEN, B_PIN_EVALUATOR_FNV, false},
  {B_PIN_2, 24, B_PIN_2_FNV, false},
  {B_PIN_3, 1724, B_PIN_3_FNV, false},
  {B_GAME_EXP, 1013, B_PIN_EXP_FNV, false},
  {B_PIN_SOLVER, 1328, B_PIN_SOLVER_FNV, false},
  {B_PIN_6, 0x1a0, B_PIN_6_FNV, false},
  {B_PIN_7, 0x5b0, B_PIN_7_FNV, false},
  {B_PIN_STEERING, 0x124, B_PIN_STEERING_FNV, false},
  {B_PIN_9, 0xdc, B_PIN_9_FNV, false},
  {B_PIN_ENTER, 0xbb0, B_PIN_ENTER_FNV, false},
  {B_PIN_EXIT, 0x1dd0, B_PIN_EXIT_FNV, false},
  {B_PIN_12, 51, B_PIN_12_FNV, true},
  {B_PIN_13, 76, B_PIN_13_FNV, true},
  {B_EXE_WORLD_MATRIX, 47, B_PIN_WORLD_MATRIX_FNV, true},
  {B_EXE_LOCAL_POSITION, 51, B_PIN_LOCAL_POSITION_FNV, true},
  {B_EXE_GRAPH_GETTER, 5, B_PIN_GRAPH_GETTER_FNV, true}};
typedef struct InputConstant { uint32_t offset, bits; } InputConstant;
/* cpp:602-618. Every pin and constant is read from the running game; one
 * difference answers false, reason INPUT_CODE_UNAVAILABLE, as in retail. */
static bool input_code(const TfpPoseConfig *c) {
  if (!B_INPUT_ESTABLISHED) return false;
  for (size_t k = 0; k < sizeof INPUT_PINS/sizeof INPUT_PINS[0]; ++k) {
    const InputPin *pin = &INPUT_PINS[k];
    uint64_t hash = UINT64_C(0xcbf29ce484222325); uint32_t done = 0;
    while (done < pin->length) {
      uint8_t block[512]; uint32_t length = pin->length - done; if (length > sizeof block) length = sizeof block;
      if (!read((pin->exe ? c->exe : c->game) + pin->offset + done, block, length)) return false;
      for (uint32_t i = 0; i < length; ++i) hash = (hash ^ block[i])*UINT64_C(0x100000001b3);
      done += length;
    }
    if (hash != pin->fnv) return false;
  }
  const InputConstant constants[] = {
    {B_CONST_ONE, 0x3f800000}, {B_CONST_MINUS_ONE, 0xbf800000}, {B_CONST_RAD_TO_DEG, 0x42652ee0},
    {B_CONST_MINUS_HALF_PI, 0xbfc90fdb}, {B_CONST_HALF_PI, 0x3fc90fdb}, {B_CONST_MINUS_75, 0xc2960000}};
  for (size_t k = 0; k < sizeof constants/sizeof constants[0]; ++k) { uint32_t bits = 0; if (!value(c->game + constants[k].offset, bits) || bits != constants[k].bits) return false; }
  uint64_t one = 0; return value(c->game + B_CONST_DOUBLE_ONE, one) && one == UINT64_C(0x3ff0000000000000);
}
/* cpp:619-625 */
static void input_check_code(void) {
  if (!atomic_load(&input_mode) || input_state.failed_closed || input_state.code_ok || tick() < input_state.retry_at) return;
  input_state.retry_at = tick() + 250;
  input_state.code_ok = input_code(&policy.c);
  input_state.stats.active = input_state.code_ok ? 1 : 0;
  input_state.stats.reason = input_state.code_ok ? INPUT_READY : INPUT_CODE_UNAVAILABLE;
}
/* cpp:626-640. The non-fixture path. */
static bool input_sdk(const TfpPoseConfig *c, uint64_t *original, uint64_t *slot) {
  uint64_t root = 0, api = 0, current = 0;
  if (!pointer(c->game + B_GAME_API_ROOT, &root) || !pointer(root + L_API_UNIT, &api)) return false;
  *slot = api + L_API_WORLD_MATRIX;
  if (!writable_slot(*slot, c->exe) || !slot_read(*slot, &current)) return false;
  if (current != c->exe + B_EXE_WORLD_MATRIX) return false;
  /* The native callsite is covered by the lazy whole-evaluator fingerprint.
   * Reading it here would prevent automatic activation on a cold code page. */
  if (!bytes(c->exe + B_EXE_WORLD_MATRIX, B_HEX_WORLD_MATRIX)) return false;
  *original = current; return executable(current);
}
/* cpp:641-646 */
static bool input_dispatch(void) {
  uint64_t root = 0, api = 0, current = 0;
  return pointer(policy.c.game + B_GAME_API_ROOT, &root) && pointer(root + L_API_UNIT, &api)
      && api + L_API_WORLD_MATRIX == input_slot && slot_read(input_slot, &current)
      && current == host.hooks[TFP_POSE_HOOK_INPUT90] && dispatch_owned(&policy.c);
}
/* cpp:647-652. Bounded native uint32-key open-address lookup. Missing keys
 * never synthesize a sentinel index, because that would make the native
 * read unbounded. */
static bool input_map(uint64_t owner, uint32_t offset, uint32_t key, uint32_t *result) {
  uint8_t header[20]; if (!read(owner + offset, header, sizeof header)) return false;
  DepthProbe proof; memset(&proof, 0, sizeof proof); return depth_map(header, key, &proof, result) && *result != UINT32_MAX;
}
/* cpp:653-668 */
static bool input_steering(const TfpPoseConfig *c, float *out) {
  uint64_t player = 0; uint32_t count = 0, avatar = 0, disabled = 0;
  if (!pointer(c->game + B_GAME_PLAYER, &player) || !value(player + L_PLAYER_COUNT, count) || !value(c->game + B_GAME_NO_AVATAR, disabled)) return false;
  if (count) {
    uint32_t entity = 0; if (!value(player + L_PLAYER_ENTITY, entity)) return false;
    if (entity == 0x7fff) { if (!value(c->game + B_GAME_HOST_AVATAR, avatar)) return false; }
    else {
      uint64_t world = 0; uint32_t row = 0;
      if (!pointer(c->game + B_GAME_WORLD, &world) || !input_map(world, B_WORLD_ENTITY_MAP, entity, &row) || row > 0x100000
          || !value(world + B_WORLD_ENTITY_AVATAR + (uint64_t)row*24, avatar)) return false;
    }
  } else if (!value(c->game + B_GAME_IDLE_AVATAR, avatar)) return false;
  if (avatar == disabled) { out[0] = out[1] = 0; return true; }
  uint32_t index = 0; if (avatar != c->avatar || !input_map(c->avatar_owner, L_AVATAR_MAP, avatar, &index) || index != c->avatar_index) return false;
  return read(c->avatar_owner + B_AVATAR_STEERING + (uint64_t)index*B_AVATAR_STRIDE, out, 2*sizeof(float));
}
/* cpp:669-673 */
typedef struct InputSample {
  TfpGainInput math; uint32_t original; uint8_t preset[L_PRESET];
  uint64_t context, index, input_owner, settings, time;
  uint8_t flags[0x5c]; uint32_t device;
} InputSample;
static void input_sample_init(InputSample *s) { memset(s, 0, sizeof *s); tfp_gain_input_reset(&s->math); }
/* cpp:674-701 */
static bool input_sample(uint64_t native_stack, InputSample *s) {
  const TfpPoseConfig *c = &policy.c; uint8_t inhibit = 0, gate = 0;
  if (!value(native_stack + B_STACK_CONTEXT, s->context) || s->context != c->context || !value(native_stack + B_STACK_INDEX, s->index) || s->index != c->avatar_index
      || !value(native_stack + B_STACK_DT, s->math.dt_eval) || !value(c->context + B_CONTEXT_INHIBIT, inhibit) || inhibit
      || !read(c->avatar_owner + B_INPUT_RAW + s->index*B_INPUT_RAW_STRIDE, s->math.raw, sizeof s->math.raw)
      || !read(c->request + L_REQUEST_HISTORY, s->math.history, sizeof s->math.history) || !value(c->request + L_REQUEST_STATE, s->original)
      || !value(c->request + L_REQUEST_STATE + 4, s->math.lens_radians) || !value(c->request + L_REQUEST_STATE + 0xc, gate)
      || !pointer(c->game + B_GAME_INPUT_OWNER, &s->input_owner) || !value(s->input_owner + B_INPUT_DEVICE, s->device)
      || !read(s->input_owner + B_INPUT_FLAGS, s->flags, sizeof s->flags)
      || !pointer(c->game + B_GAME_SETTINGS, &s->settings) || !value(s->settings + B_SETTINGS_DEGREES, s->math.tpv_degrees)
      || !pointer(c->game + B_GAME_CLOCK, &s->time) || !value(s->time, s->math.dt_global)
      || !read(c->context + B_CONTEXT_LOOK, s->math.context, sizeof s->math.context) || !input_steering(c, s->math.steering)) return false;
  /* Only original native presets are admitted. A foreign/private selector is
   * never recursively cloned; our own cache has a separate backing domain. */
  if (s->original > 255) return false;
  const uint64_t preset = c->game + B_GAME_PRESETS + (uint64_t)s->original*L_PRESET;
  if (!read(preset, s->preset, sizeof s->preset)) return false;
  memcpy(s->math.native_gain, s->preset + 4, sizeof s->math.native_gain); s->math.pitch_gate = gate != 0;
  const bool pad = s->device == 1 || s->device == 2;
  for (unsigned axis = 0; axis < 2; ++axis) {
    const unsigned first = (pad ? 0 : 2) + axis, second = (pad ? 4 : 8) + axis;
    s->math.sign[axis] = (s->flags[first] ? -1.0f : 1.0f)*(s->flags[second] ? -1.0f : 1.0f);
  }
  s->math.device = field_f32(s->flags, pad ? 0x4c : 0x58); return true;
}
/* cpp:702-713. Deliberately exclude padding from the scalar transaction proof. */
static bool input_same(const InputSample *a, const InputSample *b) {
  const TfpGainInput *x = &a->math; const TfpGainInput *y = &b->math;
  return a->original == b->original && a->context == b->context && a->index == b->index && a->input_owner == b->input_owner
      && a->settings == b->settings && a->time == b->time && a->device == b->device
      && memcmp(a->preset, b->preset, sizeof a->preset) == 0 && memcmp(a->flags, b->flags, sizeof a->flags) == 0
      && memcmp(x->raw, y->raw, sizeof x->raw) == 0 && memcmp(x->history, y->history, sizeof x->history) == 0
      && memcmp(&x->dt_eval, &y->dt_eval, sizeof(float)*4) == 0
      && memcmp(x->sign, y->sign, sizeof x->sign) == 0 && memcmp(x->steering, y->steering, sizeof x->steering) == 0
      && memcmp(x->context, y->context, sizeof x->context) == 0 && memcmp(x->native_gain, y->native_gain, sizeof x->native_gain) == 0
      && memcmp(&x->dt_global, &y->dt_global, sizeof(float)) == 0 && x->pitch_gate == y->pitch_gate;
}
_Static_assert(offsetof(TfpGainInput, device) - offsetof(TfpGainInput, dt_eval) == 3*sizeof(float), "dt_eval..device are four adjacent floats");
/* cpp:714-749. VirtualQuery and VirtualAlloc are the host's region and
 * allocate; the private record itself is written directly, as retail does. */
static InputClone *input_clone(const InputSample *s, unsigned *selected) {
  const TfpPoseConfig *c = &policy.c;
  for (unsigned i = 0; i < INPUT_CLONES; ++i) {
    InputClone *clone = &input_clones[i];
    if (clone->used && clone->game == c->game && clone->original == s->original && memcmp(clone->source, s->preset, L_PRESET) == 0
        && !clone->busy && !clone->quarantined) { *selected = i; return clone; }
  }
  unsigned free_slot = INPUT_CLONES; for (unsigned i = 0; i < INPUT_CLONES; ++i) if (!input_clones[i].used) { free_slot = i; break; }
  if (free_slot == INPUT_CLONES) return NULL;
  const uint64_t table = c->game + B_GAME_PRESETS, limit = table + UINT64_C(0x9000000000);
  uint64_t at = (table + 0x1ffff) & ~UINT64_C(0xffff);
  for (unsigned tries = 0; tries < 4096 && at + 0x10000 < limit; ++tries) {
    uint64_t region_base = 0, region_size = 0; bool region_free = false;
    if (!host.region(host.user, at, &region_base, &region_size, &region_free)) return NULL;
    uint64_t end = region_base + region_size;
    if (end <= at) return NULL;
    if (region_free) {
      const uint64_t base = (at + 0xffff) & ~UINT64_C(0xffff);
      if (base + 0x10000 <= end) {
        const uint64_t memory = host.allocate(host.user, base, 0x10000);
        if (memory) {
          const uint64_t start = memory;
          const uint64_t record = start + (L_PRESET - (start - table)%L_PRESET)%L_PRESET;
          const uint64_t index = (record - table)/L_PRESET;
          if (record < table || record + L_PRESET > start + 0x10000 || index > UINT32_MAX || table + index*L_PRESET != record) return NULL;
          InputClone *clone = &input_clones[free_slot]; clone->game = c->game; clone->address = record; clone->original = s->original;
          clone->index = (uint32_t)index; memcpy(clone->source, s->preset, L_PRESET);
          memcpy((void *)(uintptr_t)record, s->preset, L_PRESET); clone->used = true;
          ++input_state.stats.cache_entries; *selected = free_slot; return clone;
        }
      }
    }
    at = (end + 0xffff) & ~UINT64_C(0xffff);
  }
  return NULL;
}
/* cpp:750-763. The VirtualQuery test is host.writable32; the write is
 * NADSE_InputCompareExchange of native_ads_input.S, host.cas32. */
static bool input_cas32(uint64_t at, uint32_t expected, uint32_t desired, uint32_t *observed) {
  if ((at & 3) || !address(at)) return false;
  if (!host.writable32(host.user, at)) return false;
  return host.cas32(host.user, at, expected, desired, observed);
}
/* cpp:764-775. Cleanup is narrower than admission: changing the equipped gun
 * does not revoke an old live row's exact-owned index. No unit/weapon memory
 * is written. */
static bool input_row(const InputPending *p) {
  uint64_t context = 0; uint32_t ring[2] = {0}, header[4] = {0};
  if (!pointer(p->c.game + B_GAME_CONTEXT, &context) || context != p->c.context
      || !read(context + L_CONTEXT_HEAD, ring, sizeof ring) || ring[0] >= 32 || ring[1] >= 32) return false;
  const uint64_t first = context + L_CONTEXT_RECORDS;
  if (p->request < first || p->request >= first + 32*L_CONTEXT_RECORD || (p->request - first)%L_CONTEXT_RECORD) return false;
  const uint32_t row = (uint32_t)((p->request - first)/L_CONTEXT_RECORD), count = (ring[1] + 32 - ring[0])%32;
  if (!count || ((row + 32 - ring[0])%32) >= count) return false;
  return read(p->request, header, sizeof header) && memcmp(header, p->header, sizeof header) == 0;
}
/* cpp:776-788. native_witness false = retail's default argument. */
static bool input_abort(InputPending *p, bool native_witness) {
  if (!p->published) return true;
  if (!native_witness) p->interrupted = true;
  uint32_t current = 0; if (!value(p->request + L_REQUEST_STATE, current)) return false;
  if (current != p->index) { p->published = false; ++input_state.stats.foreign_wins; return true; }
  if (native_witness) {
    uint32_t header[4] = {0}; if (!read(p->request, header, sizeof header) || memcmp(header, p->header, sizeof header) != 0) return false;
  } else if (!input_row(p)) return false;
  uint32_t observed = 0; if (!input_cas32(p->request + L_REQUEST_STATE, p->index, p->original, &observed)) return false;
  p->published = false;
  if (observed == p->index) ++input_state.stats.restored; else ++input_state.stats.foreign_wins;
  return true;
}
/* cpp:789-792 */
static void input_quarantine(InputPending *p) {
  InputClone *clone = &input_clones[p->clone];
  if (!clone->quarantined) { clone->quarantined = true; ++input_state.stats.quarantined; }
}
/* cpp:793-808 */
static void input_complete(uint64_t generation, uint64_t native_stack, uint64_t request) {
  InputPending *p = &input_state.pending;
  if (!p->ready || p->poisoned || generation != p->generation || p->thread != host.thread(host.user)
      || p->native_stack != native_stack || p->request != request) return;
  uint64_t context = 0; if (!value(native_stack + B_STACK_CONTEXT, context) || context != p->c.context) return;
  uint32_t current_index = 0;
  const bool counted = p->published && !p->interrupted && value(p->request + L_REQUEST_STATE, current_index) && current_index == p->index;
  if (!input_abort(p, true)) { input_quarantine(p); input_state.stats.reason = INPUT_PENDING; return; }
  float history[3] = {0};
  if (!read(p->request + L_REQUEST_HISTORY, history, sizeof history) || memcmp(history, p->expected_history, sizeof history) != 0) {
    ++input_state.stats.unsupported; input_state.stats.reason = INPUT_SOURCE_CHANGED;
    if (p->input_dependent) { input_state.failed_closed = true; input_state.code_ok = false; input_state.stats.active = 0; }
  } else if (counted) ++input_state.stats.normalized;
  else ++input_state.stats.unsupported;
  input_clones[p->clone].busy = false; memset(p, 0, sizeof *p); input_state.stats.pending = 0;
}
/* Calls of the game's own functions. Retail calls them directly; a host
 * with forward_setter / forward_getter / native_exp takes the call instead. */
static void call_setter(unsigned which, uint64_t original, uint32_t unit, uint32_t node, const float *pose) {
  if (host.forward_setter) host.forward_setter(host.user, which, original, unit, node, pose);
  else ((TfpPoseSetter)(uintptr_t)original)(unit, node, pose);
}
static const float *call_getter(unsigned which, uint64_t original, uint32_t unit, uint32_t node) {
  if (host.forward_getter) return host.forward_getter(host.user, which, original, unit, node);
  return ((TfpPoseGetter)(uintptr_t)original)(unit, node);
}
static uint64_t native_exp_address = 0; /* policy_lock; set just before tfp_gain_prepare */
static double native_exp(double x) {
  if (host.native_exp) return host.native_exp(host.user, native_exp_address, x);
  return ((TfpExpFn)(uintptr_t)native_exp_address)(x);
}
/* cpp:809-873, after the policy lock is taken (cpp:817). */
static void input90_locked(uint64_t request, uint64_t native_stack, uint64_t generation, uint64_t lease_generation) {
  if (!lease_generation || lease_generation != atomic_load_explicit(&input_lease_generation, memory_order_relaxed)) return;
  ++input_state.stats.observed;
  InputPending *pending = &input_state.pending;
  if (pending->ready) {
    if (!input_abort(pending, false)) input_quarantine(pending);
    if (pending->thread == host.thread(host.user) && pending->native_stack == native_stack) {
      pending->poisoned = true; input_quarantine(pending);
    }
    ++input_state.stats.unsupported; input_state.stats.reason = INPUT_REENTRY; return;
  }
  if (!atomic_load_explicit(&armed, memory_order_acquire) || generation != atomic_load_explicit(&active_generation, memory_order_relaxed)) return;
  input_check_code();
  if (!input_state.code_ok || input_state.failed_closed) return;
  if (request != policy.c.request || !native_stack || (policy.deadline && tick() >= policy.deadline)) {
    ++input_state.stats.unsupported; input_state.stats.reason = INPUT_OWNER; return;
  }
  /* In the sights the game's own gains stay: a scope's zoom level is meant to scale look input there, and
   * normalising it left up/down aim dead in full ADS (live). Bit 48 covers the frames where the aim-view
   * keeper has cleared the controller's active byte. */
  if (ownership(&policy, false) && (policy.full_ads || policy.native_aiming)) { input_state.stats.reason = INPUT_ADS; return; }
  InputSample sample; input_sample_init(&sample); if (!input_sample(native_stack, &sample)) {
    ++input_state.stats.unsupported; input_state.stats.reason = INPUT_SAMPLE; return;
  }
  TfpGainPrepared prepared; memset(&prepared, 0, sizeof prepared);
  native_exp_address = policy.c.game + B_GAME_EXP;
  const uint32_t reason = tfp_gain_prepare(&sample.math, native_exp, &prepared);
  input_state.stats.ratio = prepared.ratio;
  if (!tfp_gain_accepted(reason)) { ++input_state.stats.unsupported; input_state.stats.reason = 0x100 + reason; return; }
  if (!prepared.changed) { input_state.stats.reason = INPUT_READY; return; }
  unsigned selected = 0; InputClone *clone = input_clone(&sample, &selected);
  if (!clone) { ++input_state.stats.unsupported; input_state.stats.reason = INPUT_POOL; return; }
  /* Nongain bytes are never repurposed, even after completion: outer camera
   * readers can still have captured this private index and consume +8C. */
  memcpy((void *)(uintptr_t)(clone->address + 4), prepared.gains, sizeof prepared.gains);
  /* Allocation and math precede the terminal proof. The index-only policy
   * does not edit a rig, so pose callbacks keep their own graph guards. */
  InputSample closing; input_sample_init(&closing);
  if (!input_sample(native_stack, &closing) || !input_same(&sample, &closing)
      || !ownership(&policy, false) || !input_dispatch()) {
    ++input_state.stats.unsupported; input_state.stats.reason = INPUT_SOURCE_CHANGED; return;
  }
  InputPending next; memset(&next, 0, sizeof next); next.ready = true; next.input_dependent = prepared.input_dependent; next.generation = generation; next.native_stack = native_stack; next.request = request;
  next.thread = host.thread(host.user); next.camera = policy.camera; next.c = policy.c; next.original = sample.original;
  next.index = clone->index; next.clone = selected; memcpy(next.expected_history, prepared.native_history, sizeof next.expected_history);
  if (!read(request, next.header, sizeof next.header) || !input_row(&next) || !atomic_load_explicit(&armed, memory_order_acquire)
      || generation != atomic_load_explicit(&active_generation, memory_order_relaxed)) return;
  uint32_t observed = 0; if (!input_cas32(request + L_REQUEST_STATE, next.original, next.index, &observed) || observed != next.original) {
    ++input_state.stats.foreign_wins; input_state.stats.reason = INPUT_FOREIGN; return;
  }
  next.published = true; clone->busy = true; *pending = next; input_state.stats.pending = 1;
  input_state.stats.yaw_gain = prepared.gains[0]; input_state.stats.pitch_gain = prepared.gains[1]; input_state.stats.reason = INPUT_READY;
}
/* cpp:809-873 NADSE_Input90Call, entered from NADSE_Input90Thunk. */
const float *tfp_pose_input90_from(uint64_t caller, uint32_t unit, uint32_t node, uint64_t request, uint64_t native_stack) {
  const uint64_t original = atomic_load_explicit(&input_original, memory_order_acquire);
  const uint64_t generation = atomic_load_explicit(&active_generation, memory_order_acquire);
  const uint64_t lease_generation = atomic_load_explicit(&input_lease_generation, memory_order_acquire);
  const bool eligible = atomic_load_explicit(&input_mode, memory_order_acquire) && caller == atomic_load_explicit(&input_return, memory_order_relaxed)
      && request == atomic_load_explicit(&input_watched_request, memory_order_relaxed);
  const float *result = call_getter(4, original, unit, node);
  if (!eligible) return result; /* No lock, counters or RPM for unrelated SDK calls. */
  policy_acquire();
  input90_locked(request, native_stack, generation, lease_generation);
  policy_release();
  return result;
}

/* cpp:875-906, after the policy lock is taken (cpp:885). No counters, locks
 * or memory walks for unrelated getter callers. The narrow observation
 * forwards the original arguments/return pointer exactly, under the same
 * drain lock as End. It reads the native lens locals before the evaluator
 * reuses +5C for orientation scratch, and is consumed once by the paired
 * setter. */
static const float *position_getter_locked(uint64_t original, uint32_t unit, uint32_t node, uint64_t native_stack, uint64_t native_request,
                                           uint64_t generation, uint64_t completion_generation) {
  if (atomic_load(&input_mode)) {
    /* The pending generation survives End's disarm. Actual R15, not the
     * current configured request, proves which evaluation reached post00. */
    input_complete(completion_generation, native_stack, native_request);
  }
  if (!atomic_load_explicit(&armed, memory_order_acquire) || generation != atomic_load_explicit(&active_generation, memory_order_relaxed)) return call_getter(3, original, unit, node);
  policy.observation.ready = false;
  policy.carrier.ready = false;
  const float *result = call_getter(3, original, unit, node);
  if (atomic_load(&input_mode)) input_check_code();
  if (atomic_load_explicit(&armed, memory_order_acquire) && generation == atomic_load_explicit(&active_generation, memory_order_relaxed)
      && unit == policy.c.viewmodel && node == L_RIG_CAMERA_NODE && native_stack) {
    DepthObservation *observation = &policy.observation;
    observation->native_stack = native_stack; observation->thread = host.thread(host.user);
    observation->generation = generation; observation->request = policy.c.request;
    observation->ready = read(native_stack + B_STACK_LENS, observation->frame, sizeof observation->frame)
        && read(policy.c.request + L_REQUEST_STATE, observation->request_state, sizeof observation->request_state);
  }
  return result;
}
/* cpp:879-916 position_getter_call. It is position_getter_hook (cpp:907-913:
 * native_request 0) and NADSE_Input00Call (cpp:914-916, from
 * NADSE_Input00Thunk: native_request = the caller's R15). */
const float *tfp_pose_position_getter_from(uint64_t caller, uint32_t unit, uint32_t node, uint64_t native_stack, uint64_t native_request) {
  const uint64_t original = atomic_load_explicit(&originals[3], memory_order_acquire);
  if (caller != atomic_load_explicit(&getter_return, memory_order_relaxed)
      || (!atomic_load_explicit(&armed, memory_order_acquire) && !atomic_load(&input_mode))) return call_getter(3, original, unit, node);
  const uint64_t generation = atomic_load_explicit(&active_generation, memory_order_relaxed);
  const uint64_t completion_generation = atomic_load_explicit(&input_lease_generation, memory_order_relaxed);
  policy_acquire();
  const float *result = position_getter_locked(original, unit, node, native_stack, native_request, generation, completion_generation);
  policy_release();
  return result;
}
/* cpp:948-951 */
static bool finite_pose(const float *pose) {
  for (unsigned i = 0; i < 3; ++i) if (!isfinite(pose[i])) return false;
  return true;
}
/* cpp:917-947. policy_lock is held. Never replay a cached pose into a different owner/rig. */
static void restore_rig_if_owned(void) {
  const bool owned = (policy.rig_shifted || policy.rotation_shifted || policy.carrier_shifted)
      && ownership(&policy, false) && graph(&policy, false) && dispatch_owned(&policy.c);
  if (policy.rig_shifted && owned) {
    _Alignas(16) float native[4]; memcpy(native, policy.baseline, 16);
    call_setter(0, atomic_load_explicit(&originals[0], memory_order_acquire), policy.c.viewmodel, RIG_NODE, native);
    atomic_fetch_add_explicit(&zero_restores, 1, memory_order_relaxed);
  }
  if (policy.rotation_shifted && owned && sole_request(&policy)) {
    _Alignas(16) float native[4]; memcpy(native, policy.rotation_baseline, 16);
    call_setter(2, atomic_load_explicit(&originals[2], memory_order_acquire), policy.c.viewmodel, RIG_NODE, native);
    atomic_fetch_add_explicit(&zero_restores, 1, memory_order_relaxed);
  }
  if (policy.carrier_shifted && owned && sole_request(&policy)) {
    /* The original position setter queues its write. Replay only if that
     * exact write is now the live owned node0 pose; another writer or a
     * replaced pose array wins. Never restore unconditionally into root0. */
    uint64_t poses = 0; float current[3] = {0};
    if (pointer(policy.object + L_UNIT_LOCALS, &poses) && poses == policy.carrier_poses
        && read(poses + L_LOCAL_POSITION, current, sizeof current)
        && memcmp(current, policy.carrier_written, sizeof current) == 0) {
      _Alignas(16) float native[4]; memcpy(native, policy.carrier_baseline, 16);
      call_setter(0, atomic_load_explicit(&originals[0], memory_order_acquire), policy.c.viewmodel, 0, native);
      atomic_fetch_add_explicit(&zero_restores, 1, memory_order_relaxed);
    }
  }
  policy.rig_shifted = false;
  policy.rotation_shifted = false;
  policy.carrier_shifted = false; policy.carrier.ready = false;
}
/* cpp:952-955 */
static void forward_position(uint32_t unit, uint32_t node, const float *pose) {
  atomic_fetch_add_explicit(&forwarded[0], 1, memory_order_relaxed);
  call_setter(0, atomic_load_explicit(&originals[0], memory_order_acquire), unit, node, pose);
}
/* cpp:1252-1256 (declared at cpp:956) */
static bool quaternion(const float *q, float tolerance) {
  float norm = 0;
  for (unsigned i = 0; i < 4; ++i) { const float v = q[i]; if (!isfinite(v)) return false; norm += v*v; }
  return isfinite(norm) && fabsf(norm - 1.0f) <= tolerance;
}
/* cpp:1257 */
static bool identity(const float *q) { return q[0] == 0 && q[1] == 0 && q[2] == 0 && q[3] == 1; }
/* cpp:957-1011, after the policy lock is taken (cpp:961). */
static void carrier_position_locked(uint64_t caller, uint32_t unit, uint32_t node, const float *pose, uint64_t native_stack) {
  const CarrierPending pending = policy.carrier; policy.carrier.ready = false;
  if (unit == policy.c.viewmodel && node == 0) policy.carrier_shifted = false;
  if (!atomic_load_explicit(&armed, memory_order_acquire) || caller != atomic_load_explicit(&carrier_return, memory_order_relaxed)
      || unit != policy.c.viewmodel || node != 0 || !pending.ready) { forward_position(unit, node, pose); return; }
  const DepthObservation *observation = &pending.observation;
  if (!native_stack || observation->native_stack != native_stack || observation->request != policy.c.request
      || observation->generation != atomic_load_explicit(&active_generation, memory_order_relaxed)
      || observation->thread != host.thread(host.user) || (uint64_t)(uintptr_t)pose != native_stack + B_STACK_ROOT) {
    forward_position(unit, node, pose); return;
  }
  if (!policy.carrier_checked) {
    policy.carrier_checked = true;
    policy.carrier_code_ok = bytes(policy.c.game + B_CARRIER_PIN_1, B_CARRIER_HEX_1)
        && bytes(policy.c.game + B_CARRIER_PIN_2, B_CARRIER_HEX_2)
        && bytes(policy.c.game + B_CARRIER_PIN_3, B_CARRIER_HEX_3);
  }
  if (!policy.carrier_code_ok || !sole_request(&policy)) { forward_position(unit, node, pose); return; }
  uint32_t failure = OK;
  if (!ownership(&policy, false)) failure = OWNER_MISMATCH;
  else if (!graph(&policy, false)) failure = GRAPH_MISMATCH;
  else if (!dispatch_owned(&policy.c)) failure = API_MISMATCH;
  if (failure != OK || (policy.deadline != 0 && tick() >= policy.deadline)) {
    atomic_store_explicit(&armed, 0, memory_order_release);
    if (failure != OK) error(failure); else atomic_store_explicit(&expired, 1, memory_order_release);
    forward_position(unit, node, pose); return;
  }
  uint8_t request[0x24] = {0};
  /* Not in retail: the held depth's pair is also moved on the way into and out of full aim (its node 5 half is
   * weighted by the hip share and is nothing in full aim); retail's own depth pair never is. */
  if ((policy.full_ads && !pending.steady) || !read(policy.c.request + L_REQUEST_STATE, request, sizeof request)
      || memcmp(request, observation->request_state, 8)
      || memcmp(request + 0xC, observation->request_state + 0xC, sizeof request - 0xC)) {
    forward_position(unit, node, pose); return;
  }
  /* Same-invocation final quaternion, after all native solve passes. Native
   * root contains -rotate(q,+Y*D); cancel only our paired node5 depth term.
   * This does not change the native quaternion or its zoom-dependent solve. */
  _Alignas(16) float source[8] = {0}, q[4] = {0}, shifted[4] = {0}; uint64_t poses = 0;
  if (!read((uint64_t)(uintptr_t)pose - B_STACK_ROOT_SOURCE, source, sizeof source)) { forward_position(unit, node, pose); return; }
  memcpy(q, source, 16); memcpy(shifted, source + 4, 16);
  if (!quaternion(q, .002f) || !finite_pose(shifted) || !pointer(policy.object + L_UNIT_LOCALS, &poses)) {
    forward_position(unit, node, pose); return;
  }
  const float x = q[0], y = q[1], z = q[2], w = q[3];
  const float forward[3] = {2*(x*y - z*w), w*w + y*y - x*x - z*z, 2*(y*z + x*w)};
  for (unsigned i = 0; i < 3; ++i) shifted[i] -= pending.delta*forward[i];
  if (!finite_pose(shifted)) { forward_position(unit, node, pose); return; }
  memcpy(policy.carrier_baseline, source + 4, 16); memcpy(policy.carrier_written, shifted, 12);
  policy.carrier_poses = poses; policy.carrier_shifted = memcmp(shifted, source + 4, 12) != 0;
  if (pending.steady) { tfp_steady_root(&steady_log); tfp_steady_pair_root(&steady_pair); } /* the root's half is done, to the float (a shift node 5's float took can be under the root's) */
  if (policy.carrier_shifted) forward_position(unit, node, shifted); else forward_position(unit, node, pose);
}
/* cpp:957-1011 */
static void carrier_position(uint64_t caller, uint32_t unit, uint32_t node, const float *pose, uint64_t native_stack) {
  policy_acquire();
  carrier_position_locked(caller, unit, node, pose, native_stack);
  policy_release();
}
/* cpp:1012-1021. Draw animation: for the first entry_animation_ms of a steady
 * lease the rig starts lowered (and slightly pulled back) and rises to its
 * placement with an ease-out. It is a pure offset on node 5 added after
 * everything else; full ADS is never offset. Retail's non-fixture defaults:
 * 220 ms, 0.35 m drop, 0.06 m back; the floats as bit patterns (see the
 * display values). */
#define ENTRY_ANIMATION_DROP_BITS UINT32_C(0x3eb33333) /* .35f */
#define ENTRY_ANIMATION_BACK_BITS UINT32_C(0x3d75c28f) /* .06f */
static _Atomic uint32_t entry_animation_ms = 220;
static _Atomic uint32_t entry_animation_drop = ENTRY_ANIMATION_DROP_BITS, entry_animation_back = ENTRY_ANIMATION_BACK_BITS;
/* cpp:1022-1029. Remaining share of the offset: 1 at arming, 0 once the animation is over. */
static float entry_animation_remaining(uint64_t armed_at, uint64_t now) {
  const uint32_t duration = atomic_load_explicit(&entry_animation_ms, memory_order_relaxed);
  if (!duration || !armed_at || now < armed_at) return 0;
  const float t = (float)(now - armed_at)/(float)duration;
  if (t >= 1) return 0;
  const float left = 1 - t; return left*left*left;
}
/* cpp:1142-1181 (the state used by position_call and entry_rotation). */
static _Atomic uint32_t rotation_guard_enabled = 1;
#define ENTRY_GUARD_MS UINT64_C(300)
#define ENTRY_GUARD_COSINE_HALF .99991f /* cos(1.5 degrees / 2) */
#define ENTRY_GUARD_STILL .999998f      /* cos(0.25 degrees / 2) */
#define ENTRY_GUARD_STABLE 8u
#define ENTRY_GUARD_SAME .99999f        /* about half a degree */
static float guard_reference[4] = {0, 0, 0, 1}, guard_candidate[4] = {0, 0, 0, 1};
static uint32_t guard_reference_unit = 0, guard_candidate_unit = 0, guard_candidate_count = 0;
/* The first position this helper forwards in each lease is remembered per rig
 * and weapon, and written when a later steady lease for the same pair is armed. */
typedef struct EntryPosition { uint32_t viewmodel, weapon; float pose[4]; } EntryPosition;
static EntryPosition entry_positions[16]; static uint32_t entry_position_next = 0, entry_position_writes = 0;
static _Atomic uint32_t entry_position_enabled = 1; /* retail's non-fixture default */
/* cpp:1182-1185 */
static EntryPosition *entry_position_find(uint32_t viewmodel, uint32_t weapon) {
  for (unsigned i = 0; i < 16; ++i) { EntryPosition *entry = &entry_positions[i]; if (entry->viewmodel == viewmodel && entry->weapon == weapon && weapon) return entry; }
  return NULL;
}
/* cpp:1186-1191 */
static void entry_position_remember(uint32_t viewmodel, uint32_t weapon, const float *pose) {
  if (!viewmodel || !weapon) return;
  EntryPosition *entry = entry_position_find(viewmodel, weapon);
  if (!entry) { entry = &entry_positions[entry_position_next++%16]; entry->viewmodel = viewmodel; entry->weapon = weapon; }
  memcpy(entry->pose, pose, 16);
}
/* cpp:1031-1136, from the Publish guard (cpp:1047) on: the caller publishes
 * the diagnostics and releases the lock after every return here. */
static void position_locked(uint32_t unit, uint32_t node, const float *pose, uint64_t native_stack) {
  if (unit != policy.c.viewmodel || node != RIG_NODE) { forward_position(unit, node, pose); return; }
  const DepthObservation observation = policy.observation; policy.observation.ready = false; policy.carrier.ready = false;
  if ((uint64_t)(uintptr_t)pose != policy.c.request + L_REQUEST_POSITION) {
    /* Fading native ring records legitimately use this same callsite/unit.
     * Their fresh writes supersede our cache, but do not end our lease. */
    policy.rig_shifted = false;
    forward_position(unit, node, pose); return;
  }
  uint32_t failure = OK, sight = 0;
  const uint64_t now = tick();
  if (!ownership(&policy, false)) failure = OWNER_MISMATCH;
  else if (!graph(&policy, false)) failure = GRAPH_MISMATCH;
  else if (!dispatch_owned(&policy.c)) failure = API_MISMATCH;
  else if (!value(policy.c.request + L_REQUEST_SIGHT, sight) || sight == 0) failure = SIGHT_NODE_UNAVAILABLE;
  _Alignas(16) float native[4] = {0};
  if (failure == OK && (!read((uint64_t)(uintptr_t)pose, native, 16) || !finite_pose(native))) failure = POSE_INVALID;
  if (failure != OK || (policy.deadline != 0 && now >= policy.deadline)) {
    /* This very call supplies a fresh native baseline. Forward it exactly,
     * rather than replaying an older cached baseline before or after it. */
    if (policy.rig_shifted && failure != OWNER_MISMATCH && failure != GRAPH_MISMATCH && failure != POSE_INVALID)
      atomic_fetch_add_explicit(&zero_restores, 1, memory_order_relaxed);
    policy.rig_shifted = false; atomic_store_explicit(&armed, 0, memory_order_release);
    if (failure != OK) error(failure); else atomic_store_explicit(&expired, 1, memory_order_release);
    forward_position(unit, node, pose); return;
  }
  /* A shared phase changes only our common rig modifier. Every callback
   * still consumes the current native arm/weapon animation without filtering. */
  const float target = policy.full_ads ? 0.0f : 1.0f;
  if (tfp_phase_matches(&policy.shared, policy.full_ads, now)) {
    policy.weight += (target - policy.weight)*tfp_phase_step(&policy.shared, policy.full_ads);
    policy.target = target; policy.start_weight = policy.weight; policy.blend_start = now;
  } else {
    const float u = (float)(now - policy.blend_start)/(float)policy.c.blend_ms;
    const float t = u < 0 ? 0 : (u > 1 ? 1 : u), eased = t*t*(3 - 2*t);
    policy.weight = policy.start_weight + (policy.target - policy.start_weight)*eased;
    if (target != policy.target) { policy.start_weight = policy.weight; policy.target = target; policy.blend_start = now; }
  }
  _Alignas(16) float shifted[4]; memcpy(shifted, native, 16);
  if (policy.weight != 0) {
    shifted[0] += (policy.c.offset_x + policy.motion[0])*policy.weight;
    shifted[1] += (policy.c.offset_y + policy.motion[1])*policy.weight;
    shifted[2] += (policy.c.offset_z + policy.motion[2])*policy.weight;
  }
  /* Optional user-authored ADS placement; zero retains native sight pose. */
  if (policy.full_ads) for (unsigned axis = 0; axis < 3; ++axis) shifted[axis] += policy.ads_placement[axis];
  /* This presentation-only term is independent of saved placement/shoulder
   * weight. Full native ADS bypasses it immediately inside depth_delta. */
  const uint64_t depth_reads = policy.depth.refreshes + policy.depth.checks;
  float depth = depth_delta(&policy, &observation, native_stack, now);
  if (depth_reads != policy.depth.refreshes + policy.depth.checks) {
    uint32_t changed = OK;
    if (!ownership(&policy, false)) changed = OWNER_MISMATCH;
    else if (!graph(&policy, false)) changed = GRAPH_MISMATCH;
    else if (!dispatch_owned(&policy.c)) changed = API_MISMATCH;
    if (changed != OK) {
      depth_pass(&policy.depth, 9);
      policy.rig_shifted = false; atomic_store_explicit(&armed, 0, memory_order_release); error(changed);
      forward_position(unit, node, pose); return;
    }
    if (policy.full_ads) { depth = 0; depth_pass(&policy.depth, 1); }
  }
  /* Not in retail: the depth held steady. Where it has a number for the frame, that goes out in place of the zoom
   * depth term's (which goes on and off with the game's unzoom gate), through the same pair: node 5 here, the root
   * in carrier_position_locked. */
  bool steadied = false;
  const uint32_t steady_now = atomic_load_explicit(&steady_mode, memory_order_relaxed);
  if (steady_now && native_stack) {
    float held_delta = 0;
    if (steady_frame(&policy, &observation, native_stack, now, steady_now, native[1], &held_delta)) { depth = held_delta; steadied = true; }
  }
  float applied_depth = 0;
  if (depth != 0) {
    const float before = shifted[1]; shifted[1] += depth;
    if (shifted[1] != before) { if (!steadied) ++policy.depth.observed.applied; applied_depth = shifted[1] - before; }
  }
  gate_frame(&observation, native_stack, steadied ? 0 : applied_depth, steadied); /* counted for the log; nothing is changed by it */
  if (!finite_pose(shifted)) {
    policy.rig_shifted = false; atomic_store_explicit(&armed, 0, memory_order_release); error(POSE_INVALID);
    forward_position(unit, node, pose); return;
  }
  memcpy(policy.baseline, native, 16);
  if (applied_depth != 0) { policy.carrier.observation = observation; policy.carrier.delta = applied_depth; policy.carrier.steady = steadied; policy.carrier.ready = true; }
  /* Counted where node 5's float took something of the held depth: the same frames the root's half is asked for. */
  if (applied_depth != 0 && steadied) { tfp_steady_moved(&steady_log, applied_depth); tfp_steady_pair_moved(&steady_pair); }
  policy.rig_shifted = memcmp(shifted, native, 16) != 0;
  if (!policy.entry_position_seen && !policy.full_ads && policy.weight == 1) {
    policy.entry_position_seen = true; entry_position_remember(policy.c.viewmodel, policy.c.weapon, shifted);
  }
  if (policy.steady && !policy.full_ads) {
    const float remaining = entry_animation_remaining(policy.armed_at, now);
    if (remaining > 0) {
      shifted[1] -= bits_float(atomic_load_explicit(&entry_animation_back, memory_order_relaxed))*remaining;
      shifted[2] -= bits_float(atomic_load_explicit(&entry_animation_drop, memory_order_relaxed))*remaining;
      policy.rig_shifted = true;
    }
  }
  if (policy.rig_shifted) {
    atomic_fetch_add_explicit(&weapon_updates, 1, memory_order_relaxed);
    forward_position(unit, node, shifted);
  } else forward_position(unit, node, pose);
}
/* cpp:1031-1136. native_stack 0 is retail's default argument. */
static void position_call(uint64_t caller, uint32_t unit, uint32_t node, const float *pose, uint64_t native_stack) {
  /* Only this owned unit's root can need invalidation; all other global
   * position setters keep the exact no-lock/no-read forwarding path. */
  if ((node == 0 && unit == atomic_load_explicit(&watched_viewmodel, memory_order_relaxed))
      || (atomic_load_explicit(&armed, memory_order_acquire) && caller == atomic_load_explicit(&carrier_return, memory_order_relaxed))) {
    carrier_position(caller, unit, node, pose, native_stack); return;
  }
  if (!atomic_load_explicit(&armed, memory_order_acquire) || caller != atomic_load_explicit(&rig_return, memory_order_relaxed)) {
    forward_position(unit, node, pose); return;
  }
  policy_acquire();
  if (!atomic_load_explicit(&armed, memory_order_acquire)) {
    forward_position(unit, node, pose); policy_release(); return;
  }
  /* Publish is destroyed before Unlock, after the original setter completes or refuses. */
  position_locked(unit, node, pose, native_stack);
  publish_diagnostics(&policy);
  policy_release();
}
/* cpp:1137-1141 position_hook */
void tfp_pose_position_from(uint64_t return_address, uint32_t unit, uint32_t node, const float *pose, uint64_t native_stack) {
  atomic_fetch_add_explicit(&seen[0], 1, memory_order_relaxed);
  if (node == RIG_NODE && atomic_load_explicit(&hold_probe, memory_order_relaxed)
      && unit == atomic_load_explicit(&watched_viewmodel, memory_order_relaxed))
    atomic_fetch_add_explicit(&hold_node5_calls, 1, memory_order_relaxed); /* the hold probe's count; nothing reads it but tfp_pose_hold_probe */
  position_call(return_address, unit, node, pose, native_stack);
}
/* cpp:1192-1200 */
static void entry_trace(unsigned which, uint64_t caller, const float *q, uint64_t now, bool held) {
  if (policy.trace_count >= TFP_POSE_ROTATION_TRACE_ROWS) return;
  TfpPoseRotationTraceRow *row = &policy.trace[policy.trace_count++];
  row->slot = which + 1; row->ms = (uint32_t)(now - policy.armed_at); row->replaced = held ? 1 : 0;
  const uint64_t game = policy.c.game, exe = policy.c.exe;
  row->caller = caller >= game && caller - game < 0x40000000 ? (uint32_t)(caller - game)
      : caller >= exe && caller - exe < 0x40000000 ? (uint32_t)(caller - exe) | 0x80000000u : 0xffffffffu;
  memcpy(row->q, q, 16);
}
/* cpp:1201-1233. True: forward `held` (the settled rotation) in place of this
 * write. policy_lock is held. The comment block at cpp:1145-1164 describes it. */
static bool entry_rotation(uint64_t caller, const float *pose, float *held) {
  /* The setter about to run dereferences this same pointer; copying it here
   * adds no fault the forwarded call would not have. */
  _Alignas(16) float q[4] = {0};
  if (!pose) return false;
  memcpy(q, pose, 16);
  if (!quaternion(q, .02f)) return false;
  const uint64_t now = tick();
  const bool window = policy.armed_at && now >= policy.armed_at && now - policy.armed_at <= ENTRY_GUARD_MS;
  bool hold = false;
  if (window) {
    const bool known = guard_reference_unit == policy.c.viewmodel;
    const float dot = fabsf(guard_reference[0]*q[0] + guard_reference[1]*q[1] + guard_reference[2]*q[2] + guard_reference[3]*q[3]);
    /* Once released in a lease the guard stays released: it bridges the
     * game's recovery, it does not filter later motion. */
    const float *previous = policy.guard_previous;
    const bool still = policy.guard_seen && fabsf(previous[0]*q[0] + previous[1]*q[1] + previous[2]*q[2] + previous[3]*q[3]) >= ENTRY_GUARD_STILL;
    policy.guard_still = still ? policy.guard_still + 1 : 0;
    memcpy(policy.guard_previous, q, 16); policy.guard_seen = true;
    hold = known && !policy.guard_have[1] && atomic_load_explicit(&rotation_guard_enabled, memory_order_relaxed) && dot < ENTRY_GUARD_COSINE_HALF
        && policy.guard_still < 2
        && ownership(&policy, false) && graph(&policy, false) && dispatch_owned(&policy.c);
    entry_trace(1, caller, q, now, hold);
  }
  if (hold) { memcpy(held, guard_reference, 16); ++policy.guarded; return true; }
  policy.guard_have[1] = true;
  const float same = fabsf(guard_candidate[0]*q[0] + guard_candidate[1]*q[1] + guard_candidate[2]*q[2] + guard_candidate[3]*q[3]);
  if (guard_candidate_unit == policy.c.viewmodel && same >= ENTRY_GUARD_SAME) ++guard_candidate_count;
  else { memcpy(guard_candidate, q, 16); guard_candidate_unit = policy.c.viewmodel; guard_candidate_count = 1; }
  if (guard_candidate_count >= ENTRY_GUARD_STABLE) { memcpy(guard_reference, q, 16); guard_reference_unit = policy.c.viewmodel; }
  return false;
}
/* cpp:1234-1247 rotation_hook */
void tfp_pose_rotation_from(uint64_t return_address, uint32_t unit, uint32_t node, const float *pose) {
  atomic_fetch_add_explicit(&seen[1], 1, memory_order_relaxed);
  /* Every actor and bone uses this setter: only the watched rig node of an
   * armed lease takes the lock, and only to trace its entry window. */
  if (node == RIG_NODE && pose && atomic_load_explicit(&armed, memory_order_acquire) && unit == atomic_load_explicit(&watched_viewmodel, memory_order_relaxed)) {
    const uint64_t caller = return_address;
    policy_acquire();
    const uint64_t now = tick();
    if (atomic_load_explicit(&armed, memory_order_acquire) && unit == policy.c.viewmodel && policy.armed_at && now >= policy.armed_at
        && now - policy.armed_at <= ENTRY_GUARD_MS) { _Alignas(16) float q[4]; memcpy(q, pose, 16); entry_trace(0, caller, q, now, false); }
    policy_release();
  }
  atomic_fetch_add_explicit(&forwarded[1], 1, memory_order_relaxed); call_setter(1, atomic_load_explicit(&originals[1], memory_order_acquire), unit, node, pose);
}
/* cpp:1248-1251 */
static void forward_branch_rotation(uint32_t unit, uint32_t node, const float *pose) {
  atomic_fetch_add_explicit(&forwarded[1], 1, memory_order_relaxed);
  call_setter(2, atomic_load_explicit(&originals[2], memory_order_acquire), unit, node, pose);
}
/* cpp:1258-1336, after the policy lock is taken (cpp:1273). */
static void branch_rotation_locked(uint64_t caller, uint32_t unit, uint32_t node, const float *pose, uint64_t generation) {
  /* End and a complete re-arm may finish while this invocation waits. It
   * must never apply the replacement generation's delta to an older solve. */
  if (!atomic_load_explicit(&armed, memory_order_acquire) || generation != atomic_load_explicit(&active_generation, memory_order_acquire)
      || unit != policy.c.viewmodel || node != RIG_NODE) {
    forward_branch_rotation(unit, node, pose); return;
  }
  if (caller != atomic_load_explicit(&rotation_return, memory_order_relaxed)) {
    /* Another writer owns this newer pose. Do not replay our older cache. */
    policy.rotation_shifted = false; forward_branch_rotation(unit, node, pose); return;
  }
  /* Entry guard: the rig's settled rotation stands in for the game's write
   * while that write is still recovering from its wild starting value. */
  _Alignas(16) float held[4] = {0};
  if (entry_rotation(caller, pose, held)) pose = held;
  /* No owned rotation remains to restore, and the input will be forwarded
   * untouched. Do not walk ownership/graph metadata for a no-write path.
   * The next nonidentity delta still performs the complete live guards. */
  if (identity(policy.rotation) && !policy.rotation_shifted) {
    forward_branch_rotation(unit, node, pose); return;
  }
  uint32_t failure = OK;
  if (!ownership(&policy, false)) failure = OWNER_MISMATCH;
  else if (!graph(&policy, false)) failure = GRAPH_MISMATCH;
  else if (!dispatch_owned(&policy.c)) failure = API_MISMATCH;
  if (failure != OK) {
    policy.rotation_shifted = false; policy.rig_shifted = false; atomic_store_explicit(&armed, 0, memory_order_release);
    error(failure); forward_branch_rotation(unit, node, pose); return;
  }
  const bool sole = sole_request(&policy);
  if (policy.deadline != 0 && tick() >= policy.deadline) {
    /* The current call already supplies the new native rotation. */
    if (policy.rotation_shifted && sole) atomic_fetch_add_explicit(&zero_restores, 1, memory_order_relaxed);
    policy.rotation_shifted = false; restore_rig_if_owned(); atomic_store_explicit(&armed, 0, memory_order_release);
    atomic_store_explicit(&expired, 1, memory_order_release); forward_branch_rotation(unit, node, pose); return;
  }
  /* Nonzero coupling makes the earlier native sight solver depend on weapon
   * world orientation. The supported live capture has coupling zero. Remain
   * passive for every other value, fading request and full ADS. Generic
   * shoulder aim permits the sampler's smooth return to an identity delta. */
  if (!sole || !rotation_uncoupled(&policy) || (policy.full_ads && !policy.authored_ads_rotation) || identity(policy.rotation)) {
    if (policy.rotation_shifted && sole) atomic_fetch_add_explicit(&zero_restores, 1, memory_order_relaxed);
    policy.rotation_shifted = false; forward_branch_rotation(unit, node, pose); return;
  }
  _Alignas(16) float native[4], shifted[4];
  if (!read((uint64_t)(uintptr_t)pose, native, 16) || !quaternion(native, .02f)) {
    policy.rotation_shifted = false; atomic_store_explicit(&armed, 0, memory_order_release); error(POSE_INVALID);
    forward_branch_rotation(unit, node, pose); return;
  }
  const float *a = policy.rotation; const float *b = native;
  /* Parent-local delta * fresh native local rotation; no graph readback. */
  shifted[0] = a[3]*b[0] + a[0]*b[3] + a[1]*b[2] - a[2]*b[1];
  shifted[1] = a[3]*b[1] - a[0]*b[2] + a[1]*b[3] + a[2]*b[0];
  shifted[2] = a[3]*b[2] + a[0]*b[1] - a[1]*b[0] + a[2]*b[3];
  shifted[3] = a[3]*b[3] - a[0]*b[0] - a[1]*b[1] - a[2]*b[2];
  if (!quaternion(shifted, .025f)) {
    policy.rotation_shifted = false; atomic_store_explicit(&armed, 0, memory_order_release); error(POSE_INVALID);
    forward_branch_rotation(unit, node, pose); return;
  }
  memcpy(policy.rotation_baseline, native, 16); policy.rotation_shifted = true;
  atomic_fetch_add_explicit(&weapon_updates, 1, memory_order_relaxed);
  forward_branch_rotation(unit, node, shifted);
}
/* cpp:1258-1336 */
static void branch_rotation_call(uint64_t caller, uint32_t unit, uint32_t node, const float *pose) {
  /* This API is used by every actor/bone. Unrelated work must not serialize
   * behind the FPS rig's lock, including while its original setter runs.
   * Keep a matching owned node from a different caller on the locked path:
   * that writer must retire our cached rotation before it is forwarded. */
  const uint64_t generation = atomic_load_explicit(&active_generation, memory_order_acquire);
  if (!atomic_load_explicit(&armed, memory_order_acquire) || !generation
      || unit != atomic_load_explicit(&watched_viewmodel, memory_order_relaxed) || node != RIG_NODE) {
    forward_branch_rotation(unit, node, pose); return;
  }
  policy_acquire();
  branch_rotation_locked(caller, unit, node, pose, generation);
  policy_release();
}
/* cpp:1337-1340 branch_rotation_hook */
void tfp_pose_branch_rotation_from(uint64_t return_address, uint32_t unit, uint32_t node, const float *pose) {
  atomic_fetch_add_explicit(&seen[1], 1, memory_order_relaxed);
  branch_rotation_call(return_address, unit, node, pose);
}
/* cpp:1341-1344 */
static bool pin(uint64_t address_) { return host.pin(host.user, address_); }
/* cpp:1349-1386, after the policy lock is taken. control_lock is held;
 * callbacks have already been disarmed and drained. */
static bool restore_slots_locked(void) {
  bool pending = false;
  /* No callback is removed while a native evaluation can still read a
   * private record or needs the matched post00 restoration callback. */
  if (input_state.pending.ready) return false;
  if (input_possible) {
    uint64_t current = 0;
    if (!slot_read(input_slot, &current)) pending = true;
    else if (current == host.hooks[TFP_POSE_HOOK_INPUT90]) {
      uint64_t observed = 0;
      if (!writable_slot(input_slot, slot_image) || !cas(input_slot, current, atomic_load(&input_original), &observed)) pending = true;
      else input_possible = false;
    } else input_possible = false;
  }
  for (unsigned i = 0; i < SLOT_COUNT; ++i) if (possible[i]) {
    uint64_t current = 0;
    if (!slot_read(slots[i], &current)) { pending = true; continue; }
    if (current == callback(i)) {
      if (!writable_slot(slots[i], slot_image)) { pending = true; continue; }
      uint64_t observed = 0;
      if (!cas(slots[i], callback(i), atomic_load(&originals[i]), &observed)) { pending = true; continue; }
      /* A foreign replacement won the CAS race: preserve it unchanged. */
    }
    possible[i] = false;
  }
  atomic_store_explicit(&installed, pending ? 1U : 0U, memory_order_release);
  if (!pending) {
    retired_generation = owned_generation; owned_generation = 0;
    stats_deadline = 0;
    atomic_store_explicit(&reserved, 0, memory_order_release);
    atomic_store(&input_mode, false); input_state.stats.installed = 0; input_state.stats.active = 0;
    atomic_store_explicit(&input_lease_generation, 0, memory_order_relaxed); atomic_store_explicit(&input_watched_request, 0, memory_order_relaxed);
  }
  return !pending;
}
/* cpp:1349-1386 */
static bool restore_slots(void) {
  policy_acquire();
  const bool restored = restore_slots_locked();
  policy_release();
  return restored;
}
/* cpp:1387-1390 */
static bool write_generation(uint64_t *output, uint64_t generation) {
  return host.write(host.user, output, &generation, sizeof generation);
}
/* cpp:1391-1396 */
static uint32_t failed_begin(uint32_t cause, uint64_t *output) {
  atomic_store_explicit(&armed, 0, memory_order_release);
  policy_acquire(); policy_release();
  if (!restore_slots()) { error(CLEANUP_PENDING); return 2; }
  write_generation(output, 0); error(cause); return 0;
}
/* cpp:1397-1405 */
static bool copy_config(const TfpPoseConfig *input, TfpPoseConfig *c) {
  if (!read((uint64_t)(uintptr_t)input, c, sizeof *c) || c->size != sizeof *c || c->abi != 2 || c->reserved != 0
      || c->duration_ms > 30500 || c->avatar_index >= 8 || c->avatar == 0 || c->weapon == 0 || c->body == 0
      || c->viewmodel == 0 || c->viewmodel == 0xffffffff || c->weapon_unit == 0 || c->weapon_unit == 0xffffffff
      || c->weapon_unit == c->viewmodel || c->weapon_unit == c->body || c->weapon_descriptor < 65536
      || c->blend_ms == 0 || c->blend_ms > 1000 || !isfinite(c->offset_x) || !isfinite(c->offset_y) || !isfinite(c->offset_z)
      || fabsf(c->offset_x) > 0.5f || fabsf(c->offset_y) > 0.5f || fabsf(c->offset_z) > 0.5f) return reject(INVALID_CONFIG);
  return true;
}
/* cpp:1406-1418, under control_lock */
static uint32_t validate_locked(const TfpPoseConfig *input) {
  TfpPoseConfig c; memset(&c, 0, sizeof c); if (!copy_config(input, &c)) return 0;
  if (atomic_load_explicit(&reserved, memory_order_acquire)) return reject(BUSY);
  Policy checked; policy_init(&checked); checked.c = c; uint64_t setters[SLOT_COUNT] = {0}, addresses[SLOT_COUNT] = {0};
  if (!ownership(&checked, true)) return reject(OWNER_MISMATCH);
  if (!graph(&checked, true)) return reject(GRAPH_MISMATCH);
  if (!contract(&c, setters, addresses)) return 0;
  if (!ownership(&checked, true)) return reject(OWNER_MISMATCH);
  if (!graph(&checked, false)) return reject(GRAPH_MISMATCH);
  error(OK); return 1;
}
/* cpp:1406-1418 NADSE_Validate */
uint32_t tfp_pose_validate(const TfpPoseConfig *input) {
  control_acquire();
  const uint32_t result = validate_locked(input);
  control_release();
  return result;
}
/* InputState{} */
static void input_state_reset(void) { memset(&input_state, 0, sizeof input_state); }
/* cpp:1419-1506, under control_lock */
static uint32_t begin_pose_locked(const TfpPoseConfig *input, uint64_t *output, bool steady, bool normalize_input) {
  if (!write_generation(output, 0)) return reject(INVALID_CONFIG);
  TfpPoseConfig c; memset(&c, 0, sizeof c); if (!copy_config(input, &c)) return 0;
  if (atomic_load_explicit(&reserved, memory_order_acquire)) return reject(BUSY);
  Policy next; policy_init(&next); next.c = c; uint64_t verified[SLOT_COUNT] = {0}, addresses[SLOT_COUNT] = {0};
  if (!ownership(&next, true)) return reject(OWNER_MISMATCH);
  if (!graph(&next, true)) return reject(GRAPH_MISMATCH);
  if (!contract(&c, verified, addresses)) return 0;
  uint64_t optional_original = 0; uint64_t optional_slot = 0;
  bool optional_sdk = normalize_input && input_sdk(&c, &optional_original, &optional_slot);
  for (unsigned i = 0; i < SLOT_COUNT; ++i) if (atomic_load(&originals[i]) && atomic_load(&originals[i]) != verified[i]) return reject(API_MISMATCH);
  if (!pin((uint64_t)(uintptr_t)&tfp_pose_begin)) return reject(PIN_FAILED);
  if (!pin(c.exe)) return reject(PIN_FAILED);
  if (optional_sdk && !pin(c.game)) optional_sdk = false;
  policy_acquire();
  atomic_store_explicit(&input_mode, optional_sdk, memory_order_release); input_possible = false; input_state_reset();
  input_state.requested = normalize_input; input_state.stats.size = sizeof(TfpPoseInputStats); input_state.stats.abi = 1;
  for (unsigned i = 0; i < INPUT_CLONES; ++i) if (input_clones[i].used) ++input_state.stats.cache_entries;
  input_state.stats.reason = normalize_input ? (optional_sdk ? INPUT_CODE_UNAVAILABLE : INPUT_SDK_UNAVAILABLE) : INPUT_DISABLED;
  policy_release();
  if (optional_sdk) { atomic_store_explicit(&input_original, optional_original, memory_order_release); input_slot = optional_slot; }
  for (unsigned i = 0; i < SLOT_COUNT; ++i) { atomic_store_explicit(&originals[i], verified[i], memory_order_release); slots[i] = addresses[i]; possible[i] = false; }
  if (++generation_counter == 0) ++generation_counter;
  if (!write_generation(output, generation_counter)) return reject(INVALID_CONFIG);
  owned_generation = generation_counter; slot_image = c.exe; atomic_store_explicit(&reserved, 1, memory_order_release);
  atomic_store_explicit(&input_lease_generation, owned_generation, memory_order_relaxed); atomic_store_explicit(&input_watched_request, c.request, memory_order_relaxed);
  stats_deadline = 0; display_cache.blend = 0; display_cache.target = 1; publish_diagnostics(&next);
  /* Original callbacks are visible before either pointer is published. Until
   * the final arm store, even a cached or in-flight callback only forwards. */
  for (unsigned i = 0; i < SLOT_COUNT; ++i) {
    possible[i] = true; uint64_t observed = 0;
    if (!writable_slot(slots[i], c.exe)) return failed_begin(SLOT_NOT_WRITABLE, output);
    if (!cas(slots[i], verified[i], callback(i), &observed)) return failed_begin(CAS_FAULT, output);
    if (observed != verified[i]) { possible[i] = false; return failed_begin(CAS_MISMATCH, output); }
    atomic_store_explicit(&installed, 1, memory_order_release);
  }
  if (optional_sdk) {
    input_possible = true; uint64_t observed = 0;
    if (!writable_slot(input_slot, c.exe)) return failed_begin(SLOT_NOT_WRITABLE, output);
    if (!cas(input_slot, optional_original, host.hooks[TFP_POSE_HOOK_INPUT90], &observed)) return failed_begin(CAS_FAULT, output);
    if (observed != optional_original) { input_possible = false; return failed_begin(CAS_MISMATCH, output); }
    input_state.stats.installed = 1;
  }
  if (!ownership(&next, true)) return failed_begin(OWNER_MISMATCH, output);
  if (!graph(&next, false)) return failed_begin(GRAPH_MISMATCH, output);
  for (unsigned i = 0; i < SLOT_COUNT; ++i) {
    uint64_t current = 0;
    if (!slot_read(slots[i], &current) || current != callback(i)) return failed_begin(CAS_MISMATCH, output);
  }
  if (optional_sdk) { uint64_t current = 0; if (!slot_read(input_slot, &current) || current != host.hooks[TFP_POSE_HOOK_INPUT90]) return failed_begin(CAS_MISMATCH, output); }
  /* Persistent first person applies saved placement to the first fresh pose
   * of the incoming gun. Its native draw animation supplies the transition;
   * replaying an extra zero-to-offset ramp makes every weapon swap slide. */
  if (steady) next.weight = next.start_weight = next.target = 1;
  next.steady = steady;
  policy_acquire(); policy = next;
  /* Owner, graph and all four slots were proven directly above. Through the
   * hook-free original, like the helper's own restores. */
  if (steady && atomic_load_explicit(&entry_position_enabled, memory_order_relaxed)) {
    const EntryPosition *entry = entry_position_find(c.viewmodel, c.weapon);
    if (entry) {
      _Alignas(16) float pose[4]; memcpy(pose, entry->pose, 16);
      /* With the draw animation on, the remembered position starts fully lowered. */
      if (atomic_load_explicit(&entry_animation_ms, memory_order_relaxed)) {
        pose[1] -= bits_float(atomic_load_explicit(&entry_animation_back, memory_order_relaxed)); pose[2] -= bits_float(atomic_load_explicit(&entry_animation_drop, memory_order_relaxed));
      }
      call_setter(0, verified[0], c.viewmodel, RIG_NODE, pose); ++entry_position_writes;
    }
  }
  policy.deadline = c.duration_ms ? tick() + c.duration_ms : 0; policy.blend_start = tick(); policy.armed_at = policy.blend_start ? policy.blend_start : 1;
  stats_deadline = policy.deadline; publish_diagnostics(&policy);
  for (unsigned i = 0; i < 2; ++i) { atomic_store(&seen[i], 0); atomic_store(&forwarded[i], 0); }
  atomic_store(&weapon_updates, 0); atomic_store(&zero_restores, 0);
  atomic_store_explicit(&rig_return, c.game + B_GAME_RIG_CALL + L_CALL_RETURN, memory_order_relaxed);
  atomic_store_explicit(&rotation_return, c.game + B_GAME_ROTATION_CALL + L_CALL_RETURN, memory_order_relaxed);
  atomic_store_explicit(&getter_return, c.game + B_GAME_OBSERVE_CALL + L_OBSERVE_RETURN, memory_order_relaxed);
  atomic_store_explicit(&input_return, c.game + B_GAME_INPUT_RETURN, memory_order_relaxed);
  atomic_store_explicit(&carrier_return, c.game + B_GAME_CARRIER_CALL + L_CALL_RETURN, memory_order_relaxed); atomic_store_explicit(&watched_viewmodel, c.viewmodel, memory_order_relaxed);
  atomic_store_explicit(&active_generation, owned_generation, memory_order_relaxed);
  atomic_store_explicit(&expired, 0, memory_order_release); atomic_store_explicit(&armed, 1, memory_order_release); policy_release();
  error(OK); return 1;
}
/* cpp:1419-1506. steady and normalize_input are passed explicitly. */
static uint32_t begin_pose(const TfpPoseConfig *input, uint64_t *output, bool steady, bool normalize_input) {
  control_acquire();
  const uint32_t result = begin_pose_locked(input, output, steady, normalize_input);
  control_release();
  return result;
}
/* cpp:1507-1511 */
uint32_t tfp_pose_begin(const TfpPoseConfig *input, uint64_t *output) { return begin_pose(input, output, false, false); }
uint32_t tfp_pose_begin_steady(const TfpPoseConfig *input, uint64_t *output) { return begin_pose(input, output, true, false); }
uint32_t tfp_pose_validate_input(const TfpPoseConfig *input) { return tfp_pose_validate(input); }
uint32_t tfp_pose_begin_input(const TfpPoseConfig *input, uint64_t *output) { return begin_pose(input, output, false, true); }
uint32_t tfp_pose_begin_steady_input(const TfpPoseConfig *input, uint64_t *output) { return begin_pose(input, output, true, true); }
/* cpp:1512-1518 NADSE_SetTransition */
uint32_t tfp_pose_set_transition(uint64_t generation, uint64_t epoch, float progress, uint32_t ads) {
  control_acquire();
  if (!generation || !atomic_load_explicit(&reserved, memory_order_acquire) || generation != owned_generation) { const uint32_t r = reject(TOKEN_MISMATCH); control_release(); return r; }
  policy_acquire();
  uint32_t result;
  if (!atomic_load_explicit(&armed, memory_order_acquire) || !tfp_phase_publish(&policy.shared, epoch, progress, ads, tick())) result = reject(INVALID_CONFIG);
  else { error(OK); result = 1; }
  policy_release();
  control_release();
  return result;
}
/* cpp:1519-1559, from the policy lock (cpp:1536) on */
static uint32_t set_offsets_policy(float x, float y, float z, bool motion, const float *rotation, bool ads, bool authored_ads, const float *normalized) {
  if (!atomic_load_explicit(&armed, memory_order_acquire)) return reject(atomic_load(&expired) ? DEADLINE_EXPIRED : OWNER_MISMATCH);
  uint32_t failure = OK, sight = 0;
  if (policy.deadline != 0 && tick() >= policy.deadline) {
    restore_rig_if_owned(); atomic_store_explicit(&armed, 0, memory_order_release); atomic_store_explicit(&expired, 1, memory_order_release);
    return reject(DEADLINE_EXPIRED);
  }
  if (!ownership(&policy, false)) failure = OWNER_MISMATCH;
  else if (!graph(&policy, false)) failure = GRAPH_MISMATCH;
  else if (!dispatch_owned(&policy.c)) failure = API_MISMATCH;
  else if (!value(policy.c.request + L_REQUEST_SIGHT, sight) || sight == 0) failure = SIGHT_NODE_UNAVAILABLE;
  if (failure != OK) {
    if (failure != OWNER_MISMATCH && failure != GRAPH_MISMATCH) restore_rig_if_owned();
    policy.rig_shifted = false; policy.rotation_shifted = false; atomic_store_explicit(&armed, 0, memory_order_release); return reject(failure);
  }
  /* Store all axes together. The next matching native call applies them to
   * its fresh baseline; neither editing nor observing extends the deadline. */
  if (ads) { policy.ads_placement[0] = x; policy.ads_placement[1] = y; policy.ads_placement[2] = z; }
  else if (motion) { policy.motion[0] = x; policy.motion[1] = y; policy.motion[2] = z; }
  else { policy.c.offset_x = x; policy.c.offset_y = y; policy.c.offset_z = z; }
  if (rotation) { memcpy(policy.rotation, normalized, 16); policy.authored_ads_rotation = authored_ads; }
  error(OK); return 1;
}
/* cpp:1519-1559, under control_lock */
static uint32_t set_offsets_locked(uint64_t generation, float x, float y, float z, bool motion, const float *rotation, bool ads, bool authored_ads) {
  if (!generation || !atomic_load_explicit(&reserved, memory_order_acquire) || generation != owned_generation) return reject(TOKEN_MISMATCH);
  const float limit = motion ? 0.15f : 0.5f;
  if (!isfinite(x) || !isfinite(y) || !isfinite(z)
      || fabsf(x) > limit || fabsf(y) > limit || fabsf(z) > limit) return reject(INVALID_CONFIG);
  _Alignas(16) float normalized[4] = {0, 0, 0, 1};
  if (rotation) {
    memcpy(normalized, rotation, 16);
    if (!quaternion(normalized, .002f)) return reject(INVALID_CONFIG);
    const float norm = sqrtf(normalized[0]*normalized[0] + normalized[1]*normalized[1]
        + normalized[2]*normalized[2] + normalized[3]*normalized[3]);
    for (unsigned i = 0; i < 4; ++i) normalized[i] /= norm;
    if (normalized[3] < 0) for (unsigned i = 0; i < 4; ++i) normalized[i] = -normalized[i];
    if (normalized[3] < .5f) return reject(INVALID_CONFIG); /* cos(120 degrees / 2) */
  }
  policy_acquire();
  const uint32_t result = set_offsets_policy(x, y, z, motion, rotation, ads, authored_ads, normalized);
  policy_release();
  return result;
}
/* cpp:1519-1559. rotation NULL, ads false, authored_ads false are retail's default arguments. */
static uint32_t set_offsets(uint64_t generation, float x, float y, float z, bool motion, const float *rotation, bool ads, bool authored_ads) {
  control_acquire();
  const uint32_t result = set_offsets_locked(generation, x, y, z, motion, rotation, ads, authored_ads);
  control_release();
  return result;
}
/* cpp:1560-1574 */
uint32_t tfp_pose_set_placement(uint64_t generation, float x, float y, float z) {
  return set_offsets(generation, x, y, z, false, NULL, false, false);
}
uint32_t tfp_pose_set_ads_placement(uint64_t generation, float x, float y, float z) {
  return set_offsets(generation, x, y, z, false, NULL, true, false);
}
uint32_t tfp_pose_set_motion(uint64_t generation, float dx, float dy, float dz) {
  return set_offsets(generation, dx, dy, dz, true, NULL, false, false);
}
uint32_t tfp_pose_set_transform(uint64_t generation, float dx, float dy, float dz, float qx, float qy, float qz, float qw) {
  const float rotation[4] = {qx, qy, qz, qw}; return set_offsets(generation, dx, dy, dz, true, rotation, false, false);
}
uint32_t tfp_pose_set_authored_transform(uint64_t generation, float dx, float dy, float dz, float qx, float qy, float qz, float qw) {
  const float rotation[4] = {qx, qy, qz, qw}; return set_offsets(generation, dx, dy, dz, true, rotation, false, true);
}
/* cpp:1575-1605, from the policy lock (cpp:1580) on */
static uint32_t set_zoom_policy_policy(uint32_t active) {
  if (!atomic_load_explicit(&armed, memory_order_acquire)) return reject(atomic_load(&expired) ? DEADLINE_EXPIRED : OWNER_MISMATCH);
  if (policy.deadline != 0 && tick() >= policy.deadline) {
    restore_rig_if_owned(); atomic_store_explicit(&armed, 0, memory_order_release); atomic_store_explicit(&expired, 1, memory_order_release);
    return reject(DEADLINE_EXPIRED);
  }
  uint32_t failure = OK, sight = 0;
  if (!ownership(&policy, false)) failure = OWNER_MISMATCH;
  else if (!graph(&policy, false)) failure = GRAPH_MISMATCH;
  else if (!dispatch_owned(&policy.c)) failure = API_MISMATCH;
  else if (!value(policy.c.request + L_REQUEST_SIGHT, sight) || sight == 0) failure = SIGHT_NODE_UNAVAILABLE;
  if (failure != OK) {
    if (failure != OWNER_MISMATCH && failure != GRAPH_MISMATCH) restore_rig_if_owned();
    policy.rig_shifted = false; policy.rotation_shifted = false; atomic_store_explicit(&armed, 0, memory_order_release); return reject(failure);
  }
  if (policy.zoom_policy_active != (active != 0)) {
    policy.zoom_policy_active = active != 0;
    policy.depth.valid = false; policy.depth.signature_set = false; policy.depth.previous_known = false;
    policy.depth.retry_at = 0; policy.depth.next_check = 0;
    policy.observation.ready = false; policy.carrier.ready = false;
    /* Keep all saved/native baselines: End must still restore a previous
     * modified pose if no fresh native setter runs before cleanup. */
  }
  error(OK); return 1;
}
/* cpp:1575-1605 NADSE_SetZoomPolicy */
uint32_t tfp_pose_set_zoom_policy(uint64_t generation, uint32_t active) {
  if (active > 1) return reject(INVALID_CONFIG);
  control_acquire();
  uint32_t result;
  if (!generation || !atomic_load_explicit(&reserved, memory_order_acquire) || generation != owned_generation) result = reject(TOKEN_MISMATCH);
  else {
    policy_acquire();
    result = set_zoom_policy_policy(active);
    policy_release();
  }
  control_release();
  return result;
}
/* cpp:1606-1627, under control_lock */
static uint32_t end_locked(uint64_t generation) {
  if (!generation) return reject(TOKEN_MISMATCH);
  if (!atomic_load_explicit(&reserved, memory_order_acquire)) {
    if (generation != retired_generation) return reject(TOKEN_MISMATCH);
    error(OK); return 1;
  }
  if (generation != owned_generation) return reject(TOKEN_MISMATCH);
  atomic_store_explicit(&armed, 0, memory_order_release);
  atomic_store_explicit(&active_generation, 0, memory_order_relaxed);
  policy_acquire(); restore_rig_if_owned();
  if (input_state.pending.ready) {
    if (!input_abort(&input_state.pending, false)) input_quarantine(&input_state.pending);
  }
  policy.observation.ready = false; policy.depth.valid = false;
  memset(policy.motion, 0, sizeof policy.motion); memset(policy.rotation, 0, sizeof policy.rotation); policy.rotation[3] = 1;
  policy_release();
  atomic_store_explicit(&watched_viewmodel, 0, memory_order_relaxed);
  if (!restore_slots()) return reject(CLEANUP_PENDING);
  error(OK); return 1;
}
/* cpp:1606-1627 NADSE_End */
uint32_t tfp_pose_end(uint64_t generation) {
  control_acquire();
  const uint32_t result = end_locked(generation);
  control_release();
  return result;
}
/* cpp:1628 */
uint32_t tfp_pose_error(void) { return atomic_load_explicit(&last_error, memory_order_acquire); }
/* cpp:1629-1647, under control_lock */
static uint32_t read_stats_locked(uint64_t generation, TfpPoseStats *output) {
  if (atomic_load_explicit(&reserved, memory_order_acquire) ? (!generation || generation != owned_generation) : generation != 0) return reject(TOKEN_MISMATCH);
  TfpPoseStats s; memset(&s, 0, sizeof s);
  if (!read((uint64_t)(uintptr_t)output, &s, sizeof s) || s.size != sizeof s || s.abi != 2) return reject(INVALID_CONFIG);
  if (atomic_load_explicit(&armed, memory_order_acquire) && stats_deadline != 0 && tick() >= stats_deadline) {
    policy_acquire();
    if (atomic_load_explicit(&armed, memory_order_acquire) && policy.deadline != 0 && tick() >= policy.deadline) {
      restore_rig_if_owned(); atomic_store_explicit(&armed, 0, memory_order_release); atomic_store_explicit(&expired, 1, memory_order_release);
    }
    policy_release();
  }
  read_diagnostics(&s);
  s.installed = atomic_load(&installed); s.armed = atomic_load(&armed); s.expired = atomic_load(&expired); s.error = atomic_load(&last_error);
  s.seen_position = atomic_load(&seen[0]); s.seen_rotation = atomic_load(&seen[1]); s.weapon_updates = atomic_load(&weapon_updates); s.zero_restores = atomic_load(&zero_restores);
  s.forwarded_position = atomic_load(&forwarded[0]); s.forwarded_rotation = atomic_load(&forwarded[1]);
  return host.write(host.user, output, &s, sizeof s);
}
/* cpp:1629-1647 NADSE_ReadStats */
uint32_t tfp_pose_read_stats(uint64_t generation, TfpPoseStats *output) {
  control_acquire();
  const uint32_t result = read_stats_locked(generation, output);
  control_release();
  return result;
}
/* cpp:1648-1659, under control_lock */
static uint32_t read_depth_stats_locked(uint64_t generation, TfpPoseDepthStats *output) {
  if (atomic_load_explicit(&reserved, memory_order_acquire) ? (!generation || generation != owned_generation) : generation != 0) return reject(TOKEN_MISMATCH);
  TfpPoseDepthStats s; memset(&s, 0, sizeof s);
  if (!read((uint64_t)(uintptr_t)output, &s, sizeof s) || s.size != sizeof s || s.abi != 1 || s.reserved) return reject(INVALID_CONFIG);
  policy_acquire();
  s = policy.depth.observed; s.cache_refreshes = policy.depth.refreshes; s.cache_checks = policy.depth.checks;
  policy_release();
  s.size = sizeof s; s.abi = 1; s.installed = atomic_load(&installed); s.armed = atomic_load(&armed);
  return host.write(host.user, output, &s, sizeof s);
}
/* cpp:1648-1659 NADSE_ReadDepthStats */
uint32_t tfp_pose_read_depth_stats(uint64_t generation, TfpPoseDepthStats *output) {
  control_acquire();
  const uint32_t result = read_depth_stats_locked(generation, output);
  control_release();
  return result;
}
/* cpp:1660-1673, under control_lock */
static uint32_t read_input_stats_locked(uint64_t generation, TfpPoseInputStats *output) {
  if (atomic_load_explicit(&reserved, memory_order_acquire) ? (!generation || generation != owned_generation) : generation != 0) return reject(TOKEN_MISMATCH);
  TfpPoseInputStats request; memset(&request, 0, sizeof request);
  if (!read((uint64_t)(uintptr_t)output, &request, sizeof request) || request.size != sizeof request || request.abi != 1 || request.reserved) return reject(INVALID_CONFIG);
  policy_acquire(); TfpPoseInputStats s = input_state.stats;
  s.size = sizeof s; s.abi = 1; s.pending = input_state.pending.ready ? 1 : 0;
  uint64_t current = 0;
  s.installed = input_possible && slot_read(input_slot, &current) && current == host.hooks[TFP_POSE_HOOK_INPUT90] ? 1 : 0;
  s.active = s.installed && input_state.code_ok && !input_state.failed_closed && atomic_load(&armed) ? 1 : 0;
  if (input_possible && !s.installed) s.reason = INPUT_SDK_UNAVAILABLE;
  policy_release();
  return host.write(host.user, output, &s, sizeof s);
}
/* cpp:1660-1673 NADSE_ReadInputStats */
uint32_t tfp_pose_read_input_stats(uint64_t generation, TfpPoseInputStats *output) {
  control_acquire();
  const uint32_t result = read_input_stats_locked(generation, output);
  control_release();
  return result;
}

/* cpp:1675-1685 NADSE_RotationTrace49. Diagnostics for the entry rotation
 * guard. No generation: read-only copy of the current or last lease's trace. */
uint32_t tfp_pose_rotation_trace(TfpPoseRotationTraceData *output) {
  control_acquire();
  TfpPoseRotationTraceData data; memset(&data, 0, sizeof data); data.size = sizeof data;
  policy_acquire_shared();
  data.count = policy.trace_count; data.guarded = policy.guarded; data.enabled = atomic_load_explicit(&rotation_guard_enabled, memory_order_relaxed);
  memcpy(data.rows, policy.trace, sizeof data.rows);
  policy_release_shared();
  const uint32_t result = host.write(host.user, output, &data, sizeof data) ? 1 : 0;
  control_release();
  return result;
}
/* cpp:1686-1688 NADSE_SetRotationGuard49 */
uint32_t tfp_pose_set_rotation_guard(uint32_t enabled) {
  atomic_store_explicit(&rotation_guard_enabled, enabled ? 1 : 0, memory_order_relaxed); return 1;
}

/* cpp:1690-1697 NADSE_EntryPositionStats50 */
uint32_t tfp_pose_entry_position_stats(TfpPoseEntryPositionData *output) {
  control_acquire();
  TfpPoseEntryPositionData data; memset(&data, 0, sizeof data); data.size = sizeof data; data.enabled = atomic_load_explicit(&entry_position_enabled, memory_order_relaxed);
  policy_acquire_shared();
  data.writes = entry_position_writes; for (unsigned i = 0; i < 16; ++i) if (entry_positions[i].weapon) ++data.remembered;
  policy_release_shared();
  const uint32_t result = host.write(host.user, output, &data, sizeof data) ? 1 : 0;
  control_release();
  return result;
}
/* cpp:1698-1700 NADSE_SetEntryPosition50 */
uint32_t tfp_pose_set_entry_position(uint32_t enabled) {
  atomic_store_explicit(&entry_position_enabled, enabled ? 1 : 0, memory_order_relaxed); return 1;
}

/* cpp:1702-1707 NADSE_SetEntryAnimation50. Duration 0 switches it off; drop and back are metres (0 to 1). */
uint32_t tfp_pose_set_entry_animation(uint32_t milliseconds, float drop, float back) {
  if (milliseconds > 1000 || !isfinite(drop) || drop < 0 || drop > 1 || !isfinite(back) || back < 0 || back > 1) return 0;
  atomic_store_explicit(&entry_animation_ms, milliseconds, memory_order_relaxed);
  atomic_store_explicit(&entry_animation_drop, float_bits(drop), memory_order_relaxed); atomic_store_explicit(&entry_animation_back, float_bits(back), memory_order_relaxed); return 1;
}

/* ---- not in retail ---- */
uint32_t tfp_pose_gate_take(TfpGateSecond *out) {
  if (!out || !host.lock_exclusive) return 0;
  policy_acquire();
  tfp_gate_take(&gate, out);
  policy_release();
  out->depth_off = atomic_load_explicit(&depth_off, memory_order_relaxed);
  return 1;
}
uint32_t tfp_pose_set_gate_reads(uint32_t on) { atomic_store_explicit(&gate_reads, on ? 1 : 0, memory_order_relaxed); return 1; }
uint32_t tfp_pose_set_depth_off(uint32_t on) { atomic_store_explicit(&depth_off, on ? 1 : 0, memory_order_relaxed); return 1; }
uint32_t tfp_pose_set_depth_steady(uint32_t mode) {
  if (mode > 2) return 0;
  atomic_store_explicit(&steady_mode, mode, memory_order_relaxed); return 1;
}
uint32_t tfp_pose_steady_take(TfpSteadySecond *out) {
  if (!out || !host.lock_exclusive) return 0;
  policy_acquire();
  tfp_steady_take(&steady_log, out);
  policy_release();
  out->off = atomic_load_explicit(&steady_mode, memory_order_relaxed) == 2;
  return 1;
}
void tfp_pose_set_host(const TfpPoseHost *h) { host = *h; }
void tfp_pose_reset_for_tests(void) {
  for (unsigned i = 0; i < SLOT_COUNT; ++i) { atomic_store(&originals[i], 0); slots[i] = 0; possible[i] = false; }
  atomic_store(&installed, 0); atomic_store(&armed, 0); atomic_store(&reserved, 0); atomic_store(&expired, 0); atomic_store(&last_error, 0);
  for (unsigned i = 0; i < 2; ++i) { atomic_store(&seen[i], 0); atomic_store(&forwarded[i], 0); }
  atomic_store(&rig_return, 0); atomic_store(&rotation_return, 0); atomic_store(&getter_return, 0); atomic_store(&active_generation, 0);
  atomic_store(&carrier_return, 0); atomic_store(&watched_viewmodel, 0); atomic_store(&weapon_updates, 0); atomic_store(&zero_restores, 0);
  slot_image = 0;
  atomic_store(&input_original, 0); input_slot = 0; input_possible = false; atomic_store(&input_mode, false);
  atomic_store(&input_return, 0); atomic_store(&input_watched_request, 0); atomic_store(&input_lease_generation, 0);
  generation_counter = 0; owned_generation = 0; retired_generation = 0; stats_deadline = 0;
  policy_init(&policy);
  display_cache.blend = 0; display_cache.target = 1;
  atomic_store(&display_sequence, 0); atomic_store(&display_blend, 0); atomic_store(&display_target, UINT32_C(0x3f800000));
  memset(input_clones, 0, sizeof input_clones); input_state_reset(); native_exp_address = 0;
  atomic_store(&entry_animation_ms, 220); atomic_store(&entry_animation_drop, ENTRY_ANIMATION_DROP_BITS); atomic_store(&entry_animation_back, ENTRY_ANIMATION_BACK_BITS);
  atomic_store(&rotation_guard_enabled, 1);
  guard_reference[0] = guard_reference[1] = guard_reference[2] = 0; guard_reference[3] = 1;
  guard_candidate[0] = guard_candidate[1] = guard_candidate[2] = 0; guard_candidate[3] = 1;
  guard_reference_unit = 0; guard_candidate_unit = 0; guard_candidate_count = 0;
  memset(entry_positions, 0, sizeof entry_positions); entry_position_next = 0; entry_position_writes = 0;
  atomic_store(&entry_position_enabled, 1);
  atomic_store(&hold_mode, 0); atomic_store(&hold_probe, 0); atomic_store(&hold_node5_calls, 0);
  memset(&gate, 0, sizeof gate); atomic_store(&gate_reads, 0); atomic_store(&depth_off, 0);
  memset(&steady, 0, sizeof steady); memset(&steady_log, 0, sizeof steady_log); memset(&steady_pair, 0, sizeof steady_pair); atomic_store(&steady_mode, 0);
}
/* Not in retail (pose_helper.h). */
uint32_t tfp_pose_set_hold(uint32_t on) {
  atomic_store_explicit(&hold_probe, 1, memory_order_relaxed);
  return atomic_exchange_explicit(&hold_mode, on ? 1 : 0, memory_order_relaxed);
}
uint32_t tfp_pose_hold_probe(TfpPoseHoldProbe *out) {
  if (!out) return 0;
  out->hold = atomic_load_explicit(&hold_mode, memory_order_relaxed);
  out->armed = atomic_load_explicit(&armed, memory_order_acquire);
  out->error = atomic_load_explicit(&last_error, memory_order_acquire);
  out->node5_calls = atomic_load_explicit(&hold_node5_calls, memory_order_relaxed);
  return 1;
}
/* Not in retail (pose_helper.h). Reads only. */
uint32_t tfp_pose_input_held(void) {
  if (input_state.pending.ready || input_state.pending.published) return 1;
  for (unsigned i = 0; i < INPUT_CLONES; ++i) if (input_clones[i].busy) return 1;
  return 0;
}

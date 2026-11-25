/* TEST ONLY. Values for the build constants that docs/translation/POSE-HELPER-TRANSLATION.md lists as open on build
 * 25480438 (all of them in the look-gain normalisation), so that the normalisation's LOGIC can be compared
 * with the retail helper in tests/diff_pose_helper.cpp. They are NOT this build's values: they are places in
 * the test's fake game image, chosen at offsets unlike retail's, and the code-pin hashes are those of the
 * retail bytes the test copies to those places. tests/run-diff-pose.sh builds a second object of
 * src/pose_helper.c with -DTFP_POSE_TEST_OPEN_CONSTANTS='"diff_pose_helper_open.h"'; no shipped build does.
 * The seven established pins and every other constant keep their production values. */
#undef B_INPUT_ESTABLISHED
#define B_INPUT_ESTABLISHED 1
#undef B_PIN_2
#define B_PIN_2 UINT32_C(0x1288a00)
#undef B_PIN_2_FNV
#define B_PIN_2_FNV UINT64_C(0xded70c3fd0eba95c)
#undef B_PIN_3
#define B_PIN_3 UINT32_C(0x1282a00)
#undef B_PIN_3_FNV
#define B_PIN_3_FNV UINT64_C(0x3690803fb2e8bced)
#undef B_PIN_EXP_FNV
#define B_PIN_EXP_FNV UINT64_C(0xe4a380a8807b6ec2)
#undef B_PIN_6
#define B_PIN_6 UINT32_C(0x12777c0)
#undef B_PIN_6_FNV
#define B_PIN_6_FNV UINT64_C(0xf17ad9ae58e57ab0)
#undef B_PIN_7
#define B_PIN_7 UINT32_C(0x1279a00)
#undef B_PIN_7_FNV
#define B_PIN_7_FNV UINT64_C(0xd77b549041450f76)
#undef B_PIN_9
#define B_PIN_9 UINT32_C(0xf00100)
#undef B_PIN_9_FNV
#define B_PIN_9_FNV UINT64_C(0xa5ff70bd20fb7378) /* retail's bytes with B_PIN_9_MASKS zeroed (whole 0x91b780e54d5bb826) */
#undef B_PIN_EXIT_FNV
#define B_PIN_EXIT_FNV UINT64_C(0x1a26ca7b7bbba077)
#undef B_PIN_12
#define B_PIN_12 UINT32_C(0x1fd180)
#undef B_PIN_12_FNV
#define B_PIN_12_FNV UINT64_C(0xdafbc85ae8dffa23)
#undef B_PIN_13
#define B_PIN_13 UINT32_C(0x1fd1c0)
#undef B_PIN_13_FNV
#define B_PIN_13_FNV UINT64_C(0x9f25c14a58784f1f)
#undef B_CONST_ONE
#define B_CONST_ONE UINT32_C(0x2a00100)
#undef B_CONST_MINUS_ONE
#define B_CONST_MINUS_ONE UINT32_C(0x2a00204)
#undef B_CONST_RAD_TO_DEG
#define B_CONST_RAD_TO_DEG UINT32_C(0x2a00308)
#undef B_CONST_MINUS_HALF_PI
#define B_CONST_MINUS_HALF_PI UINT32_C(0x2a0040c)
#undef B_CONST_HALF_PI
#define B_CONST_HALF_PI UINT32_C(0x2a00510)
#undef B_CONST_MINUS_75
#define B_CONST_MINUS_75 UINT32_C(0x2a00614)
#undef B_CONST_DOUBLE_ONE
#define B_CONST_DOUBLE_ONE UINT32_C(0x2a00718)
#undef B_GAME_NO_AVATAR
#define B_GAME_NO_AVATAR UINT32_C(0x3484a10)
#undef B_GAME_HOST_AVATAR
#define B_GAME_HOST_AVATAR UINT32_C(0x3484814)
#undef B_GAME_IDLE_AVATAR
#define B_GAME_IDLE_AVATAR UINT32_C(0x3483e58)
#undef B_INPUT_DEVICE
#define B_INPUT_DEVICE UINT32_C(0x32c)

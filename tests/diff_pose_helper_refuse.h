/* TEST ONLY. The helper with its code proof switched off, as it shipped until 0.6.0-feel.1: input_code
 * answers false before it reads anything. tests/run-diff-pose.sh builds its first program with this, so
 * the refusing path (what the game sees when a pin differs) stays compared with retail call by call. */
#undef B_INPUT_ESTABLISHED
#define B_INPUT_ESTABLISHED 0

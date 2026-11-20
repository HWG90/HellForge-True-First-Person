/* The quit guard (docs/design/QUIT-CRASH.md). Build 25480438 releases the table at
 * [game.dll+0x3326340] + 0xF846C8 through its block allocator although the table sits alone at the start of a
 * 64 KiB-aligned mapping. Depending on the table's contents the release then writes 2 bytes below the table, into
 * unmapped memory, and the game faults while closing. The memory below the table cannot be mapped (tried in
 * feel.16: it is the unusable tail of the allocation before it), so the release itself is skipped instead. */
#ifndef TFP_QUIT_GUARD_H
#define TFP_QUIT_GUARD_H
#include <stdbool.h>
#include <stdint.h>

#define TFP_QUIT_GUARD_STATE  UINT32_C(0x3326340)   /* game.dll: the game state pointer (profile.h FP_STATE) */
#define TFP_QUIT_GUARD_TABLE  UINT32_C(0xF846C8)    /* in the state: {pointer, 32-bit count} */
#define TFP_QUIT_GUARD_RELEASE UINT32_C(0x873a70)   /* game.dll: releases a {pointer, count} pair; rcx = the pair */
#define TFP_QUIT_GUARD_REGION UINT64_C(0x10000)

/* The release routine frees three {pointer, count} pairs, at +0x00, +0x10 and +0x20 of its argument. */
#define TFP_QUIT_GUARD_PAIRS 3

/* True for a pointer the block allocator can never have handed out: one exactly at a 64 KiB boundary, where the
 * allocator keeps its own span header. Releasing such a pointer through it is the fault. */
static inline bool tfp_quit_guard_faulty(uint64_t pointer) {
  return pointer && !(pointer & (TFP_QUIT_GUARD_REGION - 1));
}
#endif

/* Text lines of the read-only probe. Values only; no engine calls. */
#ifndef TFP_PROBE_LOG_H
#define TFP_PROBE_LOG_H
#include "snapshot.h"

typedef struct TfpProbeLog { bool primed; char last[320]; uint64_t second; unsigned written, held; } TfpProbeLog;
/* One line when what the snapshot says changed, at most 10 a second. A change
 * held back by the cap is still written once the cap allows; held counts what
 * was left out. snapshot NULL: the snapshot failed with `error`. `why` is the
 * reason the weapon part is missing when only the player part was read.
 *   Snapshot: kind=6 safe=1 active=0 mask=0 guard=0 aim=0 shoulder=0 seated=0 reload=0 body=4203180 rig=4203186 weapon=912 unit=4203232 held=0
 *   Snapshot: kind=4 safe=0 ... body=4203180 rig=4203186 weapon=- why=Equip a firearm before starting the camera test held=0
 *   Snapshot: error=Camera is not owned by local player held=0 */
bool tfp_probe_line(TfpProbeLog *log, uint64_t milliseconds, const TfpSnapshot *snapshot, const char *error,
                    const char *why, char *text, size_t size);
#endif

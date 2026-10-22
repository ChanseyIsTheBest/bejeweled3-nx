// Temporary hooks that log the callers of functions a boot crashed in (one per finding, each with its evidence), and
// test aids.
//
// bejeweled3-nx (2026-10-21): empty at the start of the port; peggle1-nx's hooks were at Peggle 1's addresses.

#include <cstdint>
#include <cstring>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

// Test aid (2026-10-21): Bejeweled 3's boards are random (the gems differed between two runs of the same input
// script), which keeps two recordings of the same route (the emulated GPU against Renut's renderer,
// tools/compare_raw.py) from showing the same board. MTRand::SRand(this r3, seed r4) is sub_825286E0 (a seed of 0
// becomes 4357, then the Mersenne Twister's 1812433253 (0x6C078965) recurrence over 624 words; 11 call sites in 9
// functions). With bejeweled3_test_seed != 0 every MTRand is seeded with it instead: the same boards in every run.
REXCVAR_DEFINE_UINT32(bejeweled3_test_seed, 0, "Bejeweled3",
                      "Test runs: every random generator of the game seeded with this value (0 = the game's own seeds)");

extern "C" REX_FUNC(__imp__sub_825286E0);
REX_HOOK_RAW(sub_825286E0) {  // MTRand::SRand
  if (const uint32_t seed = REXCVAR_GET(bejeweled3_test_seed)) {
    static bool logged = false;
    if (!logged) {
      logged = true;
      REXLOG_INFO("[test] MTRand seeds replaced by bejeweled3_test_seed = {} (the game's first was {})", seed,
                  ctx.r4.u32);
    }
    ctx.r4.u64 = seed;
  }
  __imp__sub_825286E0(ctx, base);
}

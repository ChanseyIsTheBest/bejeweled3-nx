// Peggle: profile collection of the PGO-instrumented Switch build (BEJEWELED3_PGO=generate), see bejeweled3_pgo.cpp.
// In every other build these do nothing. From sa2-nx (sa2_pgo.h).

#pragma once

namespace bejeweled3::pgo {

// Picks this session's file (pgo/bejeweled3_pgo_<N>.stream next to the executable) and starts the 2-minute dumps.
void Start();
// Writes every count so far to this session's file (the whole session each time, replacing the previous dump).
void DumpNow();
// The last dump, then the dump thread ends.
void Stop();

}  // namespace bejeweled3::pgo

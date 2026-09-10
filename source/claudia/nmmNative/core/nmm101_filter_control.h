#pragma once

#include "nmm101_kernels.h"

namespace nmm::native
{
// Execute the 101 Filter F/092 control fragment at P:$0175..$019a.
//
// The caller supplies x0 from the preceding oscillator/control stage and
// positions r3/r4 at the filter's control allocation.  The fragment leaves
// the cursor and data/accumulator registers at the boundary immediately
// before the envelope's P:$019b load. X0/Y0 contain the P:$0199 prefetch
// values; runEnvelope20Control(..., true) consumes them and executes P:$019b.
Word24 runFilterF92Control(ModuleCursor& cursor) noexcept;
}

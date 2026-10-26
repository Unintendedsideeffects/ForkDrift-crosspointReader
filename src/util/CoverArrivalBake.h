#pragma once

namespace cover_arrival {

// Main loop only. Pops one book from CoverArrivalQueue and makes its single
// cover-thumbnail attempt; a skip or failure is left to the first-page retry.
void drainOne();

}  // namespace cover_arrival

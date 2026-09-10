// Exercise the exact portable board runners on the host, including under
// sanitizers. Cycle values here are synthetic; only board DWT reports timing.
#include "../teensy/src/filter_f92_board_runner.h"
#include "../teensy/src/graph101_board_runner.h"
#include <iostream>
int main() {
    uint32_t ticks=0;
    auto clock=[&](){return ticks++;};
    const auto filter=nmm::native::teensy::runFilterF92Vectors(clock);
    const auto graph=nmm::native::teensy::runGraph101Oracle(clock);
    const bool ok=filter.failed==0 && filter.vectors==256 &&
        graph.failed==0 && graph.blocks==nmm::native::graph101_oracle::blocks;
    std::cout<<(ok?"PASS":"FAIL")<<" portable board runners: filter="<<filter.passed
             <<"/"<<filter.vectors<<" graph="<<graph.passed<<"/"<<graph.blocks<<'\n';
    return ok?0:1;
}

#pragma once
#include "../core/nmm101_graph.h"
#include "../mcu/nord101_mcu_contract.h"
#include "filter_f92_oracle.h"

namespace nmm::native::graph101_oracle {
constexpr unsigned sweepBlocks = 128 * 4;
constexpr unsigned blocks = sweepBlocks + 400;
constexpr unsigned framesPerBlock = 256;
constexpr unsigned frames = blocks * framesPerBlock;
constexpr uint64_t initialHash = 1469598103934665603ull;
struct VoiceEvent { Word24 pitch, velocity, gate; };
constexpr bool hasEvent(unsigned frame) {
    return frame < sweepBlocks * framesPerBlock
        ? (frame % framesPerBlock == 0 || frame % framesPerBlock == 192)
        : (frame == sweepBlocks * framesPerBlock || frame == sweepBlocks * framesPerBlock + 4096);
}
constexpr VoiceEvent event(unsigned frame) {
    const unsigned block = frame / framesPerBlock;
    if(block >= sweepBlocks)
        return {mcu::pitchWord(60),mcu::velocityWordForMidi(100),
                mcu::gateWord(frame == sweepBlocks * framesPerBlock)};
    constexpr uint8_t velocities[]{1,64,100,127};
    return {mcu::pitchWord(block % 128), mcu::velocityWordForMidi(velocities[block / 128]),
            mcu::gateWord(frame % framesPerBlock == 0)};
}
inline uint64_t hashAudio(uint64_t hash, StereoWords sample) {
    return filter_f92_oracle::mixWord(filter_f92_oracle::mixWord(hash,sample.left),sample.right);
}
inline uint64_t hashState(const GraphMemory& memory) {
    auto hash=initialHash;
    for(unsigned i=0;i<Graph101::words;++i) hash=filter_f92_oracle::mixWord(hash,memory.x[i]);
    for(unsigned i=0;i<Graph101::words;++i) hash=filter_f92_oracle::mixWord(hash,memory.y[i]);
    return hash;
}
}

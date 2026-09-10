#pragma once

// Portable 101 board-oracle runner.  It has no Arduino, timer, or USB
// dependency, so the same schedule can be exercised by a desktop sanitizer
// harness and by the Teensy image.  The generated expected[] records are
// produced by nmmGraphDifferential --emit-board.

#include "graph101_oracle.h"
#include "nord101_reference_full_state.generated.h"
#include "graph101_vectors.h"

#include <cstddef>
#include <cstdint>

namespace nmm::native::teensy
{
struct Graph101BoardResult
{
    uint32_t blocks = 0;
    uint32_t passed = 0;
    uint32_t failed = 0;
    uint32_t firstFailure = 0xffffffffu;
    uint32_t controlMin = 0xffffffffu;
    uint32_t controlMax = 0;
    uint32_t sampleMin = 0xffffffffu;
    uint32_t sampleMax = 0;
    uint32_t frameMin = 0xffffffffu;
    uint32_t frameMax = 0;
    uint32_t blockRenderMin = 0xffffffffu;
    uint32_t blockRenderMax = 0;
    uint32_t controlCount = 0;
    uint32_t sampleCount = 0;
    uint32_t frameCount = 0;
    uint64_t controlTotal = 0;
    uint64_t sampleTotal = 0;
    uint64_t frameTotal = 0;
    uint64_t blockRenderTotal = 0;
    uint32_t lastBlockRenderCycles = 0;
    uint32_t overrunCount = 0;
    uint64_t lastAudioHash = 0;
    uint64_t lastStateHash = 0;
};

template<class CycleReader>
Graph101BoardResult runGraph101Oracle(CycleReader readCycles,
                                      uint32_t cycleOverhead = 0) noexcept
{
    Graph101BoardResult result{};
    Graph101 graph;
    graph.initialize(mcu::k101ReadyXFull, mcu::k101ReadyYFull);
    uint64_t audioHash = graph101_oracle::initialHash;
    uint32_t block = 0;
    uint64_t blockRenderCycles = 0;

    for(uint32_t frame = 0; frame < graph101_oracle::frames; ++frame)
    {
        if(graph101_oracle::hasEvent(frame))
        {
            const auto event = graph101_oracle::event(frame);
            graph.setVoiceWords(event.pitch, event.velocity, event.gate);
        }

        const uint32_t frameBegin = readCycles();
        if((frame & 3u) == 0)
        {
            const uint32_t begin = readCycles();
            graph.control();
            uint32_t cycles = readCycles() - begin;
            if(cycles >= cycleOverhead) cycles -= cycleOverhead;
            else cycles = 0;
            ++result.controlCount;
            result.controlTotal += cycles;
            if(cycles < result.controlMin) result.controlMin = cycles;
            if(cycles > result.controlMax) result.controlMax = cycles;
        }
        const uint32_t begin = readCycles();
        const StereoWords output = graph.sample();
        uint32_t cycles = readCycles() - begin;
        if(cycles >= cycleOverhead) cycles -= cycleOverhead;
        else cycles = 0;
        ++result.sampleCount;
        result.sampleTotal += cycles;
        if(cycles < result.sampleMin) result.sampleMin = cycles;
        if(cycles > result.sampleMax) result.sampleMax = cycles;
        audioHash = graph101_oracle::hashAudio(audioHash, output);
        const uint32_t frameCycles = readCycles() - frameBegin;
        ++result.frameCount;
        result.frameTotal += frameCycles;
        if(frameCycles < result.frameMin) result.frameMin = frameCycles;
        if(frameCycles > result.frameMax) result.frameMax = frameCycles;
        blockRenderCycles += frameCycles;

        if((frame % graph101_oracle::framesPerBlock) ==
           graph101_oracle::framesPerBlock - 1u)
        {
            const uint64_t stateHash = graph101_oracle::hashState(graph.memory());
            const auto& expected = graph101_oracle::expected[block];
            result.lastAudioHash = audioHash;
            result.lastStateHash = stateHash;
            result.lastBlockRenderCycles = static_cast<uint32_t>(blockRenderCycles);
            if(blockRenderCycles < result.blockRenderMin)
                result.blockRenderMin = static_cast<uint32_t>(blockRenderCycles);
            if(blockRenderCycles > result.blockRenderMax)
                result.blockRenderMax = static_cast<uint32_t>(blockRenderCycles);
            result.blockRenderTotal += blockRenderCycles;
            ++result.blocks;
            if(!graph.memory().fault && audioHash == expected.audio &&
               stateHash == expected.state)
                ++result.passed;
            else
            {
                ++result.failed;
                if(result.firstFailure == 0xffffffffu) result.firstFailure = block;
            }
            audioHash = graph101_oracle::initialHash;
            blockRenderCycles = 0;
            ++block;
        }
    }
    return result;
}
}

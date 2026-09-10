#pragma once
#include "dsp56kEmu/jitconfig.h"

namespace nmrack
{
    inline dsp56k::JitConfig configureJit(dsp56k::JitConfig config,bool linked,bool wordSerial)
    {
        config.maxInstructionsPerBlock=16;config.maxDoIterations=1;
        config.dynamicPeripheralAddressing=true;config.dynamicFastInterrupts=true;
        config.linkJitBlocks=linked;config.linkedBlockDeadlineChecks=linked;
        config.getBlockConfig={};
        if(wordSerial)
        {
            const auto base=config;
            config.getBlockConfig=[base](dsp56k::TWord pc)->std::optional<dsp56k::JitConfig>{
                // Addresses belong to the hash-verified Rack resident and native
                // sample prefix. These routines arm DMA, toggle banks and gate
                // ESSI. Batching their writes shifts serial startup/restart by
                // a word. Keep arithmetic/idle loops in normal 16-op blocks.
                if((pc>=0x9e&&pc<0x16a)||(pc>=0x175&&pc<0x19d))
                {
                    auto precise=base;precise.maxInstructionsPerBlock=1;
                    precise.linkInstructionLimitBlocks=precise.linkedBlockDeadlineChecks;
                    return precise;
                }
                return {};
            };
        }
        return config;
    }
}

#pragma once
#include "audioqueue.h"
#include "../nmm/nmmLib/nmmpatch.h"
#include <cstdint>
#include <memory>
#include <string>

namespace nmrack
{
    struct Machine;
    // Serialized worker API; never execute firmware/lazy JIT in an audio callback.
    // The diagnostic mixer is not yet a validated hardware codec stream.
    class Hardware
    {
    public:
        struct Options {bool trace=false,linkedJit=true,experimentalChain=false,wordSerial=false,validateDac=false,boardDiagnostics=false;};
        struct Status {
            bool tablesVerified;uint64_t mainLoopVisits,steps,mixFrames;double machineFrames;
            uint64_t serialWords=0,acceptedSerialWords=0;double maxReceiverLeadFrames=0;
            uint64_t dacFrames=0,dacWordsChecked=0,dacHeldWords=0;double dacMaxSkew=0;
        };
        Hardware(const std::string& firmware,Options options);
        ~Hardware();
        Hardware(const Hardware&)=delete;
        Hardware& operator=(const Hardware&)=delete;
        void setStepBudget(uint64_t cumulativeBudget);
        void boot(unsigned machineFrames=480000);
        void initializeTone();
        void initializeOutputTones();
        void loadPatch(const nmm::Patch& patch);
        void sendMidi(uint8_t status,uint8_t data1,uint8_t data2);
        void setParameter(uint16_t area,uint16_t module,uint16_t parameter,uint8_t value);
        struct Voice {uint8_t dsp=0,note=0,velocity=0,state=0;uint16_t program=0;};
        struct Voices {unsigned allocated=0,used=0;std::array<Voice,32> voice{};};
        Voices voices() const; // worker/paused diagnostics only
        // Worker-only serial-output target. No RAM mixer fallback.
        void renderAudio(AudioFrame* output,unsigned frames);
        void writeDacWav(const std::string& path,unsigned frames=96000);
        void advance(unsigned machineFrames);
        void renderDiagnosticMix(AudioFrame* output,unsigned frames);
        void writeMixWav(const std::string& path,unsigned frames=96000);
        void captureDiagnostics(const std::string& prefix);
        void report() const;
        Status status() const;
    private:
        std::unique_ptr<Machine> impl;
    };
}

#pragma once
#include "synthLib/device.h"
#include "nmmEditorTransport.h"
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <condition_variable>

namespace nmm {struct Capture;}

namespace nmmJucePlugin
{
    // UI/state access uses the mutex; the audio callback only touches atomics.
    struct PanelState
    {
        std::shared_ptr<EditorTransport> editor=std::make_shared<EditorTransport>();
        struct PatchEntry {std::string name,text;std::vector<uint8_t> native;};
        static constexpr unsigned MaxPatches=99;
        std::array<std::atomic<int>,4> values{{100,64,64,64}};
        std::atomic<int> button4{0};
        std::atomic<bool> button4Mapped{false};
        std::atomic<unsigned> patchGeneration{1}, knobMask{0};
        std::atomic<bool> ready{false}, offline{false};
        std::atomic<bool> loading{true}, failed{false};
        std::atomic<uint64_t> underruns{0}, droppedJobs{0};
        // Production: fixed audio batches on the existing hardware worker.
        // Test overrides are immutable after Device construction.
        unsigned audioDrivenFrames=64;
        bool jitDeadlineGraphs=false; // Opt-in deadline-checked graph continuations; disabled by default.
        std::atomic<uint64_t> controlMaxUs{0},renderMaxUs{0},jobMaxCompiles{0};
        unsigned jitBlockLimit=16; // Immutable test override; production remains 16.
        bool audioDrivenLinkedJit=true,audioDrivenThreaded=false;
        // Opt-in test telemetry. Published by the worker; never timed by the audio callback.
        std::atomic<bool> runtimeDiagnostics{false};
        // Optional caller-owned capture. Allocate before enabling; retain until
        // Device destruction, then export. Worker is the only producer/reader.
        std::atomic<nmm::Capture*> capture{nullptr};
        std::atomic<uint64_t> workerJobMaxUs{0},workerGapMaxUs{0};
        std::atomic<uint64_t> firstMissTarget{0},firstMissAvailable{0},firstMissJobDepth{0};
        std::atomic<bool> priorityApplied{false};
        std::atomic<uint64_t> startedLoads{0},coldLoads{0},warmLoads{0},cancelledLoads{0};
        std::mutex mutex;
        std::condition_variable snapshotChanged;
        uint64_t snapshotRevision=0;
        uint64_t snapshotRequest=0,snapshotCompleted=0;
        std::array<int,3> restoreKnobs{{-1,-1,-1}};
        std::string patchText, patchName="Init", status="Starting";
        std::vector<PatchEntry> bank;
        std::vector<uint8_t> flash;
        unsigned selectedPatch=0;
        uint64_t bankRevision=0;
        void requestPatch(std::string text,std::string name);
        bool selectPatch(unsigned index,bool force=false);
        bool selectPatchRelative(int direction);
    private:
        void queuePatch(); // mutex held
    };

    class Device final : public synthLib::Device
    {
    public:
        Device(const synthLib::DeviceCreateParams&,std::string firmware,std::shared_ptr<PanelState>);
        ~Device() override;
        float getSamplerate() const override { return 96000; }
        bool isValid() const override { return true; }
        uint32_t getChannelCountIn() override { return 2; }
        uint32_t getChannelCountOut() override { return 2; }
        uint32_t getInternalLatencyMidiToOutput() const override { return m_latency.load(); }
        uint32_t getInternalLatencyInputToOutput() const override { return m_latency.load()+256; }
        void setProcessingBlockSize(uint32_t samples) override;
        bool setDspClockPercent(uint32_t p) override { return p==100; }
        uint32_t getDspClockPercent() const override { return 100; }
        uint64_t getDspClockHz() const override { return 82944000; }
        void process(const synthLib::TAudioInputs&,const synthLib::TAudioOutputs&,size_t,
                     const std::vector<synthLib::SMidiEvent>&,std::vector<synthLib::SMidiEvent>&) override;
#if !SYNTHLIB_DEMO_MODE
        bool getState(std::vector<uint8_t>&,synthLib::StateType) override;
        bool setState(const std::vector<uint8_t>&,synthLib::StateType) override;
        bool setStateFromUnknownCustomData(const std::vector<uint8_t>& state) override {return setState(state,synthLib::StateTypeGlobal);}
#endif
    private:
        static constexpr uint32_t Block=256,Capacity=128,AudioCapacity=65536,MinimumLatency=1024;
        struct Midi { uint16_t offset; uint8_t a,b,c; };
        struct Job { unsigned generation=0; uint64_t start=0; uint16_t size=0,count=0; std::array<Midi,64> midi{}; std::array<std::array<float,2>,Block> input{}; };
        struct Frame { uint64_t time=0; std::array<float,2> audio{}; };
        void run(std::string firmware);
        void readMidiOut(std::vector<synthLib::SMidiEvent>&) override {}
        void processAudio(const synthLib::TAudioInputs&,const synthLib::TAudioOutputs&,size_t) override {}
        bool sendMidi(const synthLib::SMidiEvent&,std::vector<synthLib::SMidiEvent>&) override { return false; }
        std::shared_ptr<PanelState> m_panel;
        std::array<Job,Capacity> m_jobs;
        std::array<Frame,AudioCapacity> m_audio;
        std::atomic<uint64_t> m_jobRead{0},m_jobWrite{0},m_audioRead{0},m_audioWrite{0};
        std::atomic<bool> m_stop{false};
        std::atomic<uint64_t> m_discardMidiBefore{0};
        std::atomic<uint32_t> m_latency{MinimumLatency};
        uint64_t m_time=0,m_generationStart=0;
        unsigned m_audioGeneration=0;uint32_t m_audioDelay=0;bool m_audioReady=false;
        std::array<float,2> m_previousInput{},m_previousOutput{};
        std::thread m_worker;
    };
}

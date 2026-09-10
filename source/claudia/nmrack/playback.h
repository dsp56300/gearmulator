#pragma once
#include "hardware.h"
#include "rateadapter.h"
#include "commands.h"
#include <atomic>
#include <chrono>
#include <exception>
#include <limits>
#include <thread>

namespace nmrack
{
    // One emulation worker / one host callback, queued at the host rate.
    // Firmware remains at 96 kHz; conversion runs on the producer only.
    class PlaybackQueue
    {
    public:
        static constexpr unsigned Capacity=8192;
        static_assert(std::atomic<uint64_t>::is_always_lock_free,"Audio counters must be lock-free");
        unsigned available() const noexcept
        {return unsigned(write.load(std::memory_order_acquire)-read.load(std::memory_order_acquire));}
        bool push(const AudioFrame* input,unsigned count) noexcept
        {
            const auto w=write.load(std::memory_order_relaxed),r=read.load(std::memory_order_acquire);
            if(count>Capacity-(w-r)) return false;
            for(unsigned i=0;i<count;++i) frames[(w+i)%Capacity]=input[i];
            write.store(w+count,std::memory_order_release);return true;
        }
        // Callback: bounded copies, silence on shortage; no locks, allocation,
        // logging, sleeping, notifications, firmware execution or exceptions.
        unsigned consume(AudioFrame* output,unsigned count) noexcept
        {
            if(!count) return 0;
            const auto r=read.load(std::memory_order_relaxed),w=write.load(std::memory_order_acquire);
            const auto n=std::min(count,unsigned(w-r));
            for(unsigned i=0;i<n;++i) output[i]=frames[(r+i)%Capacity];
            std::fill_n(output+n,count-n,AudioFrame{});
            read.store(r+n,std::memory_order_release);
            if(n<count) {underruns.fetch_add(1,std::memory_order_relaxed);missing.fetch_add(count-n,std::memory_order_relaxed);}
            return n;
        }
        uint64_t underrunCount() const noexcept {return underruns.load(std::memory_order_relaxed);}
        uint64_t missingFrames() const noexcept {return missing.load(std::memory_order_relaxed);}
    private:
        std::array<AudioFrame,Capacity> frames{};
        alignas(64) std::atomic<uint64_t> read{0};
        alignas(64) std::atomic<uint64_t> write{0};
        std::atomic<uint64_t> underruns{0},missing{0};
    };

    class Playback
    {
    public:
        static constexpr unsigned Block=128,Target=512;
        // Ownership transfers after boot. Optional initial patch is compiled by
        // the worker before prefill, never while the device callback is running.
        explicit Playback(std::unique_ptr<Hardware> machine,double outputRate=96000,unsigned bufferedFrames=Target,
                          std::shared_ptr<const nmm::Patch> patch={})
            :hardware(std::move(machine)),rate(outputRate),target(bufferedFrames),initialPatch(std::move(patch))
        {
            if(!hardware||!hardware->status().tablesVerified) throw std::invalid_argument("Playback requires a booted machine");
            if(target<Block||target>PlaybackQueue::Capacity||target%Block) throw std::invalid_argument("Playback target must be a multiple of 128 within queue capacity");
            worker=std::thread([this]{run();});
        }
        ~Playback() {stop();}
        Playback(const Playback&)=delete;
        Playback& operator=(const Playback&)=delete;
        void stop() {stopping.store(true,std::memory_order_release);if(worker.joinable()) worker.join();}
        void checkError() const {if(failed.load(std::memory_order_acquire)) std::rethrow_exception(error);}
        void prefill(std::chrono::milliseconds timeout=std::chrono::milliseconds(2000))
        {
            const auto deadline=std::chrono::steady_clock::now()+timeout;
            while(queue.available()<target)
            {
                checkError();
                if(std::chrono::steady_clock::now()>=deadline) throw std::runtime_error("Playback prefill timed out");
                std::this_thread::sleep_for(std::chrono::milliseconds(1)); // control thread only
            }
        }
        unsigned consume(AudioFrame* output,unsigned frames) noexcept {return queue.consume(output,frames);}
        uint64_t underruns() const noexcept {return queue.underrunCount();}
        uint64_t missingFrames() const noexcept {return queue.missingFrames();}
        uint64_t worstRenderNanoseconds() const noexcept {return worstRender.load(std::memory_order_relaxed);}
        uint64_t nativeFrame() const noexcept {return publishedFrame.load(std::memory_order_relaxed);}
        uint64_t rejectedCommands() const noexcept {return rejected.load(std::memory_order_relaxed);}
        uint64_t lateCommands() const noexcept {return late.load(std::memory_order_relaxed);}
        unsigned allocatedVoices() const noexcept {return voiceSummary.load(std::memory_order_relaxed)&255;}
        unsigned voiceDspMask() const noexcept {return voiceSummary.load(std::memory_order_relaxed)>>8;}
        // Control/MIDI producers only. Overflow requests emergency all-notes-off
        // via a separate atomic flag, so a full inbox cannot lose the panic.
        bool submit(Command command)
        {
            if(command.kind==Command::Midi)
            {if(command.a<0x80||command.a>=0xf0||command.b>127||command.c>127) return false;}
            else if(command.kind!=Command::Parameter||command.a>2||command.b<1||command.b>127||command.c>127||command.d>127||
                (command.a==2&&(command.b!=1||command.c>3))) return false;
            if(commands.push(command)) return true;
            rejected.fetch_add(1,std::memory_order_relaxed);panic.store(true,std::memory_order_release);return false;
        }
        void allNotesOff() noexcept {panic.store(true,std::memory_order_release);}
    private:
        void renderControlled(AudioFrame* out,unsigned count)
        {
            unsigned serviced=0;
            while(count)
            {
                if(cursor>=midiReady&&panic.exchange(false,std::memory_order_acq_rel))
                {
                    if(!commands.clear()) panic.store(true,std::memory_order_release);
                    else
                    {
                        for(unsigned ch=0;ch<16;++ch)
                        {hardware->sendMidi(uint8_t(0xb0|ch),64,0);hardware->sendMidi(uint8_t(0xb0|ch),123,0);}
                        midiReady=cursor+32*93;
                    }
                }
                Command command;auto grant=count;
                if(!panic.load(std::memory_order_acquire)&&serviced<32&&commands.next(cursor,midiReady,command,grant))
                {
                    ++serviced;
                    const auto due=command.kind==Command::Midi?std::max(command.frame,midiReady):command.frame;
                    if(due<cursor) late.fetch_add(1,std::memory_order_relaxed);
                    if(command.kind==Command::Midi)
                    {
                        hardware->sendMidi(uint8_t(command.a),uint8_t(command.b),uint8_t(command.c));
                        midiReady=cursor+(((command.a&0xf0)==0xc0||(command.a&0xf0)==0xd0)?62:93);
                    }
                    else hardware->setParameter(command.a,command.b,command.c,uint8_t(command.d));
                    continue;
                }
                hardware->renderAudio(out,grant);out+=grant;count-=grant;cursor+=grant;
                publishedFrame.store(cursor,std::memory_order_relaxed);
            }
        }
        void run() noexcept
        {
            try
            {
                // Each render request is still bounded by its emulated-time
                // deadline and JIT grants. A boot-only cumulative cap must not
                // terminate otherwise healthy long-running playback.
                hardware->setStepBudget(std::numeric_limits<uint64_t>::max());
                if(initialPatch)
                {
                    hardware->loadPatch(*initialPatch);hardware->advance(96000);initialPatch.reset();
                    const auto voices=hardware->voices();unsigned mask=0;
                    for(unsigned i=0;i<voices.allocated;++i)
                    {if(voices.voice[i].dsp>=4) throw std::runtime_error("Invalid voice DSP index");mask|=1u<<voices.voice[i].dsp;}
                    voiceSummary.store(voices.allocated|(mask<<8),std::memory_order_relaxed);
                }
                std::array<AudioFrame,Block> output;
                while(!stopping.load(std::memory_order_acquire))
                {
                    if(queue.available()+Block>target)
                    {std::this_thread::sleep_for(std::chrono::microseconds(100));continue;}
                    const auto start=std::chrono::steady_clock::now();
                    rate.render(output.data(),Block,[this](AudioFrame* out,unsigned count){renderControlled(out,count);});
                    const auto elapsed=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count());
                    if(elapsed>worstRender.load(std::memory_order_relaxed)) worstRender.store(elapsed,std::memory_order_relaxed);
                    if(!queue.push(output.data(),Block)) throw std::runtime_error("Playback producer overflow");
                }
            }
            catch(...) {error=std::current_exception();failed.store(true,std::memory_order_release);}
        }
        std::unique_ptr<Hardware> hardware;
        RateAdapter rate;
        unsigned target;
        std::shared_ptr<const nmm::Patch> initialPatch;
        Commands commands;
        uint64_t cursor=0,midiReady=0;
        std::atomic<uint64_t> publishedFrame{0},rejected{0},late{0};
        std::atomic<unsigned> voiceSummary{0};
        std::atomic<bool> panic{false};
        PlaybackQueue queue;
        std::atomic<bool> stopping{false},failed{false};
        std::atomic<uint64_t> worstRender{0};
        std::exception_ptr error;
        std::thread worker;
    };
}

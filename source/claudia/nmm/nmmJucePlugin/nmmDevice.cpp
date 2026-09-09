#include "nmmDevice.h"
#include "nmmLib/nmmcapture.h"
#include <time.h>
#include "nmmLibraryProtocol.h"
#include "nmmLib/nmmeditorstate.h"
#include "nmmLib/nmmhardware.h"
#include "nmmLib/nmmpatch.h"
#include "nmmLib/nmmrom.h"
#include "dsp56kBase/threadtools.h"
#include <algorithm>
#include <chrono>
#include <sstream>

namespace nmmJucePlugin
{
    void PanelState::requestPatch(std::string text,std::string name)
    {
        std::lock_guard<std::mutex> lock(mutex);
        const auto found=std::find_if(bank.begin(),bank.end(),[&](const PatchEntry& p){return p.text==text;});
        if(found!=bank.end()) selectedPatch=static_cast<unsigned>(found-bank.begin());
        else
        {
            const auto empty=std::find_if(bank.begin(),bank.end(),[](const auto& p){return p.name.empty() && p.text.empty() && p.native.empty();});
            if(empty!=bank.end()) {selectedPatch=unsigned(empty-bank.begin());*empty={std::move(name),std::move(text)};}
            else if(bank.size()<MaxPatches) {selectedPatch=static_cast<unsigned>(bank.size());bank.push_back({std::move(name),std::move(text)});}
            else bank[selectedPatch]={std::move(name),std::move(text)};
        }
        queuePatch();
    }
    bool PanelState::selectPatch(unsigned index,bool force)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if(index>=bank.size() || (index==selectedPatch && !failed && !force)) return false;
        if(bank[index].name.empty())
        {
            const int direction=index>selectedPatch?1:-1;
            int candidate=int(index);
            while(candidate>=0 && candidate<int(bank.size()) && bank[size_t(candidate)].name.empty()) candidate+=direction;
            if(candidate<0 || candidate>=int(bank.size())) return false;
            index=unsigned(candidate);
        }
        selectedPatch=index;queuePatch();return true;
    }
    bool PanelState::selectPatchRelative(int direction)
    {
        if(direction==0) return false;
        std::lock_guard<std::mutex> lock(mutex);
        if(bank.size()<2) return false;
        const auto start=selectedPatch;
        auto index=start;
        for(size_t i=0;i<bank.size();++i)
        {
            index=direction>0?(index+1)%bank.size():(index+bank.size()-1)%bank.size();
            if(index!=start && !bank[index].name.empty())
            {
                selectedPatch=static_cast<unsigned>(index);queuePatch();return true;
            }
        }
        return false;
    }
    void PanelState::queuePatch()
    {
        patchText=bank[selectedPatch].text;patchName=bank[selectedPatch].name.empty()?"Empty":bank[selectedPatch].name;
        status="Loading "+patchName; ready=false;loading=true;failed=false;restoreKnobs.fill(-1);
        patchGeneration.fetch_add(1);
    }
    Device::Device(const synthLib::DeviceCreateParams& params,std::string firmware,std::shared_ptr<PanelState> panel)
        : synthLib::Device(params),m_panel(std::move(panel)),m_worker([this,firmware]{run(firmware);}) {}
    Device::~Device() { m_stop=true; m_worker.join(); }

    void Device::setProcessingBlockSize(uint32_t samples)
    {
        const auto aligned=((uint64_t(samples)+Block-1)/Block)*Block;
        m_latency.store(uint32_t(std::min<uint64_t>(AudioCapacity/2,std::max<uint64_t>(MinimumLatency,aligned+Block))));
    }

    void Device::process(const synthLib::TAudioInputs& in,const synthLib::TAudioOutputs& out,size_t size,
                         const std::vector<synthLib::SMidiEvent>& midi,std::vector<synthLib::SMidiEvent>& response)
    {
        response.clear();
        const bool offline=m_panel->offline.load();
        if(offline) while(m_panel->loading && !m_panel->failed && !m_stop)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const auto generation=m_panel->patchGeneration.load();
        const bool ready=m_panel->ready.load();
        const auto delay=m_latency.load()+getExtraLatencySamples();
        if(!ready || !m_audioReady || generation!=m_audioGeneration || delay!=m_audioDelay)
        {
            m_audioGeneration=generation;m_audioDelay=delay;m_generationStart=m_time;
            m_audioRead.store(m_audioWrite.load(std::memory_order_acquire),std::memory_order_release);
            m_previousInput={};m_previousOutput={};
        }
        m_audioReady=ready;
        if(!ready)
        {
            for(unsigned ch=0;ch<2;++ch) if(out[ch]) std::fill_n(out[ch],size,0.0f);
            m_time+=size;return; // Loading is intentional silence, not a queue underrun.
        }
        // Fixed storage, single producer/consumer. No emulation, parsing, locks,
        // allocation or waiting on the real-time path. Late output becomes silence.
        for(size_t begin=0;begin<size;begin+=Block)
        {
            const auto n=static_cast<uint16_t>(std::min<size_t>(Block,size-begin));
            auto write=m_jobWrite.load(std::memory_order_relaxed);
            // Offline bounce is explicitly permitted to wait; realtime never does.
            if(m_panel->offline) while(write-m_jobRead.load()>=Capacity && !m_stop)
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            if(write-m_jobRead.load(std::memory_order_acquire)<Capacity)
            {
                auto& job=m_jobs[write%Capacity];
                job.generation=generation;job.start=m_time+begin; job.size=n; job.count=0;
                for(unsigned i=0;i<n;++i) for(unsigned ch=0;ch<2;++ch) job.input[i][ch]=in[ch]?in[ch][begin+i]:0;
                for(const auto& ev:midi)
                {
                    if(!ev.sysex.empty() || ev.a<0x80 || ev.a>=0xf0 || ev.b>127 || ev.c>127 || ev.offset<begin || ev.offset>=begin+n) continue;
                    if(job.count==job.midi.size()) {m_discardMidiBefore=write+1;++m_panel->droppedJobs;break;}
                    job.midi[job.count++]={static_cast<uint16_t>(ev.offset-begin),ev.a,ev.b,ev.c};
                }
                m_jobWrite.store(write+1,std::memory_order_release);
                if(m_panel->offline) while(m_jobRead.load(std::memory_order_acquire)<=write && !m_stop)
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
            else {++m_panel->droppedJobs; m_discardMidiBefore=write;}
            auto read=m_audioRead.load(std::memory_order_relaxed);
            const auto available=m_audioWrite.load(std::memory_order_acquire);
            for(unsigned i=0;i<n;++i)
            {
                std::array<float,2> frame{};
                const auto now=m_time+begin+i;
                if(now>=m_generationStart+delay)
                {
                    // Targets begin at the new generation boundary; older output
                    // always has an earlier timestamp, including late worker writes.
                    const auto target=now-delay;
                    while(read<available && m_audio[read%AudioCapacity].time<target) ++read;
                    if(read<available && m_audio[read%AudioCapacity].time==target) frame=m_audio[read++%AudioCapacity].audio;
                    else
                    {
                        if(!m_panel->underruns.load(std::memory_order_relaxed) && m_panel->runtimeDiagnostics.load(std::memory_order_relaxed))
                        {
                            m_panel->firstMissTarget=target;
                            m_panel->firstMissAvailable=available>read?m_audio[read%AudioCapacity].time:0;
                            m_panel->firstMissJobDepth=m_jobWrite.load(std::memory_order_acquire)-m_jobRead.load(std::memory_order_acquire);
                        }
                        ++m_panel->underruns;
                    }
                }
                for(unsigned ch=0;ch<2;++ch)
                {
                    // Unity-gain DC coupling approximation. DAC word decoding
                    // and the OS master-volume curve run on the hardware worker.
                    const float input=frame[ch];
                    const float filtered=input-m_previousInput[ch]+0.9993457f*m_previousOutput[ch]; // 10 Hz at 96 kHz
                    m_previousInput[ch]=input;m_previousOutput[ch]=filtered;
                    if(out[ch]) out[ch][begin+i]=filtered;
                }
            }
            m_audioRead.store(read,std::memory_order_release);
        }
        m_time+=size;
    }

    void Device::run(std::string firmware)
    {
        dsp56k::ThreadTools::setCurrentThreadName("NMM hardware");
        bool elevated=false;
        auto setPlaybackPriority=[&](bool active)
        {
            if(active==elevated) return;
            elevated=active;
            // High uses QoS on macOS, not a hard realtime policy for compilation/editor work.
            const auto ok=dsp56k::ThreadTools::setCurrentThreadPriority(active?dsp56k::ThreadPriority::High:dsp56k::ThreadPriority::Normal);
            m_panel->priorityApplied=active && ok;
        };
        auto threadCpu=[]() noexcept -> uint64_t {
#ifdef CLOCK_THREAD_CPUTIME_ID
            timespec t{};if(!clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t)) return uint64_t(t.tv_sec)*1000000000+uint64_t(t.tv_nsec);
#endif
            return 0; // CPU attribution unavailable on this platform.
        };
        uint64_t measuredJobs=0;
        nmm::Capture* attachedCapture=nullptr;
        auto previousJobEnd=std::chrono::steady_clock::now();
        bool previousHadBacklog=false;
        auto lastJob=std::chrono::steady_clock::now();
        auto lastIdle=lastJob;
        std::unique_ptr<nmm::Hardware> hw;
        std::shared_ptr<const nmm::Rom> image;
        nmm::Patch patch;
        unsigned generation=0,observedRequest=0;
        auto requestChanged=std::chrono::steady_clock::now();
        uint64_t lastPanic=0;
        std::array<int,3> previous{{-1,-1,-1}};
        int previousVolume=-1;
        int previousButton4=-1;
        uint64_t pendingEditorRevision=0,flashRevision=~uint64_t(0);
        unsigned loadedIndex=0;uint64_t loadedBankRevision=0;std::string loadedText;
        std::array<uint64_t,99> libraryFingerprints{};
        uint64_t libraryRevision=~uint64_t(0);
        bool libraryInitialized=false;
        unsigned pendingLoadGeneration=0,pendingLoadPosition=0;uint8_t pendingLoadSlot=0;
        int pendingDelete=-1;unsigned deleteGeneration=0;std::vector<uint8_t> libraryReply;libraryReply.reserve(256);
        auto syncNativeKnobs=[&]
        {
                    unsigned mask=0;const auto knobs=hw->editorKnobs();
                    for(unsigned i=0;i<3;++i) if(knobs[i].module)
                    {
                        mask|=1u<<i;int expected=previous[i];
                        if(m_panel->values[i+1].compare_exchange_strong(expected,knobs[i].value)) previous[i]=knobs[i].value;
                    }
                    m_panel->knobMask=mask;
        };
        auto controlsChanged=[&]
        {
            if(m_panel->values[0].load()!=previousVolume) return true;
            for(unsigned i=0;i<3;++i) if(m_panel->values[i+1].load()!=previous[i]) return true;
            if(m_panel->button4.load()!=previousButton4) return true;
            return false;
        };
        auto applyControls=[&]
        {
            const auto volume=m_panel->values[0].load();
            if(volume!=previousVolume) {hw->setMasterVolume(uint8_t(volume));previousVolume=volume;}
            const auto knobs=hw->editorKnobs();
            for(unsigned i=0;i<3;++i)
            {
                const auto value=m_panel->values[i+1].load();
                if(value==previous[i]) continue;
                if(knobs[i].module) hw->setPatchParameter(knobs[i].area,knobs[i].module,knobs[i].parameter,uint8_t(value));
                previous[i]=value;
            }
            const auto button=hw->editorButton();
            const auto value=m_panel->button4.load();
            if(value!=previousButton4)
            {
                if(button.module) hw->setPatchParameter(button.area,button.module,button.parameter,uint8_t(value));
                previousButton4=value;
            }
        };
        while(!m_stop)
        {
            const auto requested=m_panel->patchGeneration.load();
            if(requested!=generation)
            {
                setPlaybackPriority(false);
                previousHadBacklog=false;
                const auto now=std::chrono::steady_clock::now();
                if(requested!=observedRequest) {observedRequest=requested;requestChanged=now;}
                // Coalesce a rotary gesture before allocating/compiling another machine.
                if(generation && now-requestChanged<std::chrono::milliseconds(25))
                {std::this_thread::sleep_for(std::chrono::milliseconds(1));continue;}
                m_jobRead.store(m_jobWrite.load(std::memory_order_acquire),std::memory_order_release);
                const auto previousBankRevision=loadedBankRevision;
                generation=requested;++m_panel->startedLoads;
                try
                {
                    if(hw)
                    {
                        hw->setCancellation(&m_panel->patchGeneration,requested,&m_stop);
                        bool preserve;
                        {std::lock_guard<std::mutex> lock(m_panel->mutex);preserve=loadedBankRevision==m_panel->bankRevision && loadedIndex<m_panel->bank.size() && m_panel->bank[loadedIndex].text==loadedText;}
                        if(preserve)
                        {
                            // Finish already received editor work before leaving this bank entry.
                            std::array<std::array<float,2>,Block> discard{};
                            for(unsigned i=0;i<750 && !hw->editorIdle();++i) hw->renderInto(discard.data(),Block,1000000);
                            if(!hw->editorIdle()) throw std::runtime_error("Editor is busy; finish the upload before switching patches");
                            const auto packets=hw->exportEditorPatch();std::vector<uint8_t> native;
                            for(const auto& packet:packets) native.insert(native.end(),packet.begin(),packet.end());
                            std::lock_guard<std::mutex> lock(m_panel->mutex);
                            if(loadedBankRevision==m_panel->bankRevision && loadedIndex<m_panel->bank.size() && !m_panel->bank[loadedIndex].name.empty() && m_panel->bank[loadedIndex].text==loadedText)
                                m_panel->bank[loadedIndex].native=std::move(native);
                        }
                    }
                    std::string text; std::array<int,3> restore;std::vector<uint8_t> native;
                    {std::lock_guard<std::mutex> lock(m_panel->mutex);text=m_panel->patchText;restore=m_panel->restoreKnobs;generation=m_panel->patchGeneration.load();loadedIndex=m_panel->selectedPatch;loadedText=text;loadedBankRevision=m_panel->bankRevision;if(!m_panel->bank.empty()) native=m_panel->bank[m_panel->selectedPatch].native;}
                    const bool warm=hw && previousBankRevision==loadedBankRevision && !native.empty();
                    if(!warm)
                    {
                        std::istringstream input(text);
                        patch=text.empty()?nmm::Patch{}:nmm::Patch::parse(input);
                        {std::lock_guard<std::mutex> lock(m_panel->mutex);patch.name=m_panel->patchName;}
                        std::vector<uint8_t> flash;
                        {std::lock_guard<std::mutex> lock(m_panel->mutex);
                         if(hw && previousBankRevision==m_panel->bankRevision && flashRevision!=hw->flashRevision()) m_panel->flash=hw->flashImage();
                         flash=m_panel->flash;}
                        hw.reset();
                        if(!image) image=std::make_shared<const nmm::Rom>(firmware);
                        if(m_stop || generation!=m_panel->patchGeneration.load()) throw nmm::Hardware::Cancelled{};
                        hw=std::make_unique<nmm::Hardware>(image);attachedCapture=nullptr;
                        hw->setAudioDrivenExecution(m_panel->audioDrivenFrames,m_panel->audioDrivenThreaded);
                        hw->setDeadlineLinkedJit(m_panel->audioDrivenLinkedJit);
                        hw->setJitBlockLimit(m_panel->jitBlockLimit,m_panel->jitDeadlineGraphs);
                        if(m_panel->runtimeDiagnostics.load()) hw->enableCompilationDiagnostics();
                        hw->setCancellation(&m_panel->patchGeneration,generation,&m_stop);
                        if(!flash.empty()) hw->restoreFlash(flash);
                        hw->boot(30000000);
                        if(text.empty() && hw->flashPatches()[loadedIndex].fingerprint) hw->loadFlashPatch(loadedIndex,30000000);
                        else hw->loadPatch(patch,30000000);
                        ++m_panel->coldLoads;
                        flashRevision=~uint64_t(0);libraryRevision=~uint64_t(0);
                        if(previousBankRevision!=loadedBankRevision) libraryInitialized=false;
                    }
                    else {hw->setCancellation(&m_panel->patchGeneration,generation,&m_stop);++m_panel->warmLoads;}
                    hw->setControlAudioCapture(false);
                    if(!native.empty()) hw->restoreEditorPatch(native);
                    // Startup controls advance hardware without queuing unsolicited audio.
                    previousVolume=m_panel->values[0].load();
                    hw->setMasterVolume(static_cast<uint8_t>(previousVolume));
                    unsigned mask=0;
                    const auto nativeKnobs=hw->editorKnobs();
                    for(unsigned i=0;i<3;++i) if(nativeKnobs[i].module)
                    {
                        mask|=1u<<i;m_panel->values[i+1]=nativeKnobs[i].value;
                    }
                    for(unsigned i=0;i<3;++i)
                    {
                        if(restore[i]>=0)
                        {
                            m_panel->values[i+1]=restore[i];
                            const auto& knob=nativeKnobs[i];if(knob.module)
                                hw->setPatchParameter(knob.area,knob.module,knob.parameter,static_cast<uint8_t>(restore[i]));
                        }
                        previous[i]=m_panel->values[i+1];
                    }
                    const auto button=hw->editorButton();
                    m_panel->button4Mapped=button.module!=0;
                    previousButton4=m_panel->button4.load();
                    m_panel->knobMask=mask;
                    hw->notifyEditorPatchChanged();
                    hw->setControlAudioCapture(true);hw->clearCancellation();
                    std::lock_guard<std::mutex> lock(m_panel->mutex);
                    if(generation==m_panel->patchGeneration.load()) {m_panel->status=m_panel->patchName; m_panel->ready=true;m_panel->loading=false;m_panel->failed=false;}
                }
                catch(const nmm::Hardware::Cancelled&)
                {
                    hw.reset();++m_panel->cancelledLoads;
                    continue; // Never publish partially compiled or superseded hardware.
                }
                catch(const std::exception& e)
                {
                    hw.reset();
                    std::lock_guard<std::mutex> lock(m_panel->mutex);
                    if(generation==m_panel->patchGeneration.load()) {m_panel->ready=false;m_panel->loading=false;m_panel->failed=true;m_panel->status=e.what();}
                }
            }
            if(hw && libraryRevision!=hw->flashRevision() && hw->editorIdle())
            {
                const auto entries=hw->flashPatches();
                std::lock_guard<std::mutex> lock(m_panel->mutex);
                if(generation==m_panel->patchGeneration.load())
                {
                    for(unsigned i=0;i<entries.size();++i)
                    {
                        const auto& entry=entries[i];
                        const bool changed=libraryInitialized && libraryFingerprints[i]!=entry.fingerprint;
                        const bool missing=i>=m_panel->bank.size() || m_panel->bank[i].name.empty();
                        if(entry.fingerprint && (changed || missing))
                        {
                            if(i>=m_panel->bank.size()) m_panel->bank.resize(i+1);
                            // An empty text with a name is a native flash-backed slot.
                            // It loads through the MCU, not through the text compiler shim.
                            m_panel->bank[i]={entry.name,{},{}};
                            if(i==loadedIndex) {loadedText.clear();m_panel->patchText.clear();}
                        }
                        else if(changed && !entry.fingerprint && i<m_panel->bank.size() && m_panel->bank[i].text.empty())
                            m_panel->bank[i]={};
                        libraryFingerprints[i]=entry.fingerprint;
                    }
                    libraryInitialized=true;libraryRevision=hw->flashRevision();
                }
            }
            if(hw)
            {
                auto& transport=*m_panel->editor;
                std::lock_guard<std::mutex> lock(transport.mutex);
                while(!transport.input.empty())
                {
                    const auto& message=transport.input.front();
                    const bool listing=libraryProtocol::command(message,20,10) && message[6]==0 && message[7]<99;
                    const bool loading=libraryProtocol::command(message,10,11) && message[6]==0 && message[7]==0 && message[8]<99;
                    if(listing || loading)
                    {
                        // Keep library requests ordered after preceding native edits/stores.
                        if(!hw->editorIdle()) break;
                        if(listing)
                        {
                            std::vector<std::string> names;
                            {std::lock_guard<std::mutex> panelLock(m_panel->mutex);for(const auto& p:m_panel->bank) names.push_back(p.name);}
                            const auto reply=libraryProtocol::list(names,message[7],message[2]&3);
                            if(reply.size()>EditorTransport::Capacity-transport.output.size()) break;
                            transport.output.insert(transport.output.end(),reply.begin(),reply.end());
                            transport.inputSize-=message.size();transport.input.pop_front();continue;
                        }
                        bool available;
                        {std::lock_guard<std::mutex> panelLock(m_panel->mutex);available=message[8]<m_panel->bank.size() && !m_panel->bank[message[8]].name.empty();}
                        if(available && m_panel->selectPatch(message[8],true))
                        {
                            pendingLoadPosition=message[8];pendingLoadSlot=message[2]&3;
                            pendingLoadGeneration=m_panel->patchGeneration.load();
                            transport.inputSize-=message.size();transport.input.pop_front();break;
                        }
                    }
                    if(!hw->sendEditorMidi(message.data(),message.size())) break;
                    if(libraryProtocol::command(message,12,11) && message[6]==0 && message[7]<99)
                    {pendingDelete=message[7];deleteGeneration=generation;}
                    transport.inputSize-=message.size();transport.input.pop_front();
                }
                if(transport.input.empty()) pendingEditorRevision=transport.revision.load();
                if(pendingLoadGeneration && generation==pendingLoadGeneration && m_panel->ready)
                {
                    const auto reply=libraryProtocol::frame({0,56,0,0,0,uint8_t(pendingLoadPosition)},pendingLoadSlot);
                    if(reply.size()<=EditorTransport::Capacity-transport.output.size())
                    {transport.output.insert(transport.output.end(),reply.begin(),reply.end());pendingLoadGeneration=0;}
                }
                auto reply=hw->receiveEditorMidi();
                for(const auto byte:reply)
                {
                    if(byte==0xf0) libraryReply.clear();
                    if(libraryReply.size()<256) libraryReply.push_back(byte);
                    else libraryReply.clear();
                    if(byte!=0xf7 || pendingDelete<0) continue;
                    const auto n=libraryReply.size();
                    unsigned sum=0;for(size_t i=0;i+2<n;++i) sum+=libraryReply[i];
                    if(n>=13 && libraryReply[0]==0xf0 && libraryReply[1]==0x33 && (libraryReply[2]&0x7c)==0x58 && libraryReply[3]==6 &&
                       (sum&127)==libraryReply[n-2] && libraryReply[n-7]==13 && libraryReply[n-6]==0 && libraryReply[n-5]==pendingDelete && libraryReply[n-4]==0 && libraryReply[n-3]==5)
                    {
                        // The native delete ACK also covers a text-imported location
                        // with no flash object. Remove it from the shared list only
                        // after the MCU confirms success, never on a failed request.
                        std::lock_guard<std::mutex> panelLock(m_panel->mutex);
                        if(deleteGeneration==m_panel->patchGeneration.load() && unsigned(pendingDelete)<m_panel->bank.size())
                            m_panel->bank[size_t(pendingDelete)]={};
                        pendingDelete=-1;
                    }
                }
                if(reply.size()<=EditorTransport::Capacity-transport.output.size())
                    transport.output.insert(transport.output.end(),reply.begin(),reply.end());
                else ++transport.rejected;
            }
            if(hw && hw->editorSnapshotReady())
            {
                bool needed;uint64_t request;
                {std::lock_guard<std::mutex> lock(m_panel->mutex);request=m_panel->snapshotRequest;needed=m_panel->snapshotRevision<pendingEditorRevision || m_panel->snapshotCompleted<request;}
                if(needed) try
                {
                    auto* capture=m_panel->capture.load(std::memory_order_acquire);
                    if(capture && capture->full) capture=nullptr;
                    const auto wallStart=capture?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
                    const auto cpuStart=capture?threadCpu():0;
                    const auto packets=hw->exportEditorPatch();std::vector<uint8_t> native;
                    for(const auto& packet:packets) native.insert(native.end(),packet.begin(),packet.end());
                    std::lock_guard<std::mutex> lock(m_panel->mutex);
                    if(generation==m_panel->patchGeneration.load())
                    {
                        m_panel->patchName=hw->editorPatchName();m_panel->status=m_panel->patchName;
                        if(m_panel->bank.empty()) m_panel->bank.push_back({m_panel->patchName,m_panel->patchText});
                        auto& entry=m_panel->bank[m_panel->selectedPatch];if(!entry.name.empty()) {entry.native=std::move(native);entry.name=m_panel->patchName;}
                        if(flashRevision!=hw->flashRevision()) {m_panel->flash=hw->flashImage();flashRevision=hw->flashRevision();}
                        m_panel->snapshotRevision=pendingEditorRevision;m_panel->snapshotCompleted=request;m_panel->snapshotChanged.notify_all();
                        if(capture) {
                            const auto cpuEnd=threadCpu();const auto wall=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-wallStart).count();
                            const auto timing=hw->timing();
                            capture->push(timing.cycles,timing.frames,nmm::Capture::Service,{1,uint32_t(wall),uint32_t((cpuEnd-cpuStart)/1000),uint32_t(cpuStart!=0)});
                        }
                    }
                }
                catch(const std::exception& e)
                {std::lock_guard<std::mutex> lock(m_panel->mutex);m_panel->status=std::string("Editor snapshot: ")+e.what();}
            }
            const auto read=m_jobRead.load(std::memory_order_relaxed);
            if(read==m_jobWrite.load(std::memory_order_acquire))
            {
                const auto now=std::chrono::steady_clock::now();
                const bool stopped=now-lastJob>std::chrono::milliseconds(50);
                if(stopped) setPlaybackPriority(false);
                const bool changed=hw && controlsChanged();
                const bool service=hw && (changed || !hw->editorIdle());
                // Suspend idle emulation only when no audio callbacks, UART work
                // or control changes need it. Polling still wakes the stopped editor.
                if(stopped && service && now-lastIdle>=std::chrono::microseconds(2667))
                {
                    lastIdle=now;
                    try
                    {
                        if(changed) applyControls();
                        std::array<std::array<float,2>,Block> discard{};hw->renderInto(discard.data(),Block,1000000);syncNativeKnobs();
                    }
                    catch(const std::exception& e)
                    {
                        hw.reset();m_panel->ready=false;m_panel->failed=true;
                        std::lock_guard<std::mutex> lock(m_panel->mutex);m_panel->status=e.what();
                    }
                }
                std::this_thread::sleep_for(std::chrono::microseconds(stopped?(service?1000:2000):500));continue;
            }
            lastJob=std::chrono::steady_clock::now();
            setPlaybackPriority(!m_panel->offline.load());
            auto& job=m_jobs[read%Capacity];
            if(job.generation!=generation || generation!=m_panel->patchGeneration.load())
            {m_jobRead.store(read+1,std::memory_order_release);continue;}
            auto write=m_audioWrite.load(std::memory_order_relaxed);
            if(write-m_audioRead.load(std::memory_order_acquire)+job.size>AudioCapacity)
            {std::this_thread::sleep_for(std::chrono::microseconds(100));continue;}
            std::array<std::array<float,2>,Block> audio{};
            auto* capture=m_panel->capture.load(std::memory_order_acquire);
            if(capture && capture->full) capture=nullptr;
            if(hw && capture!=attachedCapture) {hw->setCapture(capture);attachedCapture=capture;}
            // Clock calls are opt-in and sampled, never per DSP/MCU instruction.
            const bool sample=capture && !(measuredJobs++&63);
            const auto cpuStart=sample?threadCpu():0;
            const auto wallStart=sample?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
            const bool diagnostic=m_panel->runtimeDiagnostics.load(std::memory_order_relaxed);
            const auto jobStart=diagnostic?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
            if(diagnostic)
            {
                const auto gap=std::chrono::duration_cast<std::chrono::microseconds>(jobStart-previousJobEnd).count();
                if(previousHadBacklog && gap>0 && uint64_t(gap)>m_panel->workerGapMaxUs.load()) m_panel->workerGapMaxUs=uint64_t(gap);
            }
            try
            {
                if(hw)
                {
                    const auto compileStart=diagnostic?hw->compilationCount():0;
                    if(controlsChanged()) applyControls();
                    const auto renderStart=diagnostic?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
                    if(diagnostic) m_panel->controlMaxUs=std::max(m_panel->controlMaxUs.load(),uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(renderStart-jobStart).count()));
                    const auto panic=m_discardMidiBefore.load();
                    if(panic!=lastPanic)
                    {
                        for(unsigned channel=0;channel<16;++channel) hw->sendMidi(0xb0|channel,123,0);
                        lastPanic=panic;
                    }
                    if(read<panic) job.count=0;
                    // Stable insertion sort of the fixed MIDI array; equal-time
                    // messages preserve source order. No allocation is necessary.
                    for(unsigned i=1;i<job.count;++i)
                    {
                        const auto ev=job.midi[i];unsigned j=i;
                        while(j && job.midi[j-1].offset>ev.offset) {job.midi[j]=job.midi[j-1];--j;}
                        job.midi[j]=ev;
                    }
                    unsigned cursor=0;
                    auto renderTo=[&](unsigned end)
                    {
                        if(end<=cursor) return;
                        hw->renderInto(audio.data()+cursor,end-cursor,1000000,job.input.data()+cursor);cursor=end;
                    };
                    for(unsigned i=0;i<job.count;++i)
                    {
                        const auto& ev=job.midi[i]; renderTo(ev.offset);
                        hw->sendMidi(ev.a,ev.b,ev.c);
                    }
                    renderTo(job.size);
                    if(diagnostic) {
                        m_panel->renderMaxUs=std::max(m_panel->renderMaxUs.load(),uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-renderStart).count()));
                        m_panel->jobMaxCompiles=std::max(m_panel->jobMaxCompiles.load(),hw->compilationCount()-compileStart);
                    }
                    syncNativeKnobs();
                }
            }
            catch(const std::exception& e)
            {
                hw.reset(); m_panel->ready=false;m_panel->loading=false;m_panel->failed=true;
                std::lock_guard<std::mutex> lock(m_panel->mutex);m_panel->status=e.what();
            }
            for(unsigned i=0;i<job.size;++i) m_audio[(write+i)%AudioCapacity]={job.start+i,audio[i]};
            m_audioWrite.store(write+job.size,std::memory_order_release);
            if(sample)
            {
                const auto cpuEnd=threadCpu();
                const auto wall=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-wallStart).count();
                const auto timing=hw?hw->timing():nmm::Hardware::Timing{};
                capture->push(timing.cycles,timing.frames,nmm::Capture::Worker,{uint32_t(wall/1000),uint32_t((cpuEnd-cpuStart)/1000),uint32_t(m_jobWrite.load()-read-1),job.size,uint32_t(cpuStart!=0)});
            }
            // Publish the reusable job slot only after capture has read its fields.
            m_jobRead.store(read+1,std::memory_order_release);
            if(diagnostic)
            {
                previousHadBacklog=m_jobWrite.load(std::memory_order_acquire)>read+1;
                previousJobEnd=std::chrono::steady_clock::now();
                const auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(previousJobEnd-jobStart).count();
                if(elapsed>0 && uint64_t(elapsed)>m_panel->workerJobMaxUs.load()) m_panel->workerJobMaxUs=uint64_t(elapsed);
            }
        }
    }
#if !SYNTHLIB_DEMO_MODE
    bool Device::getState(std::vector<uint8_t>& state,synthLib::StateType)
    {
        std::unique_lock<std::mutex> lock(m_panel->mutex);
        const auto revision=m_panel->editor->revision.load();
        const auto request=++m_panel->snapshotRequest;
        if(!m_panel->snapshotChanged.wait_for(lock,std::chrono::seconds(2),[&]{return m_panel->snapshotRevision>=revision && m_panel->snapshotCompleted>=request;})) return false;
        // Append: synthLib::Plugin has already written its version/type prefix.
        state.insert(state.end(),{'N','M','M',4});
        for(const auto& value:m_panel->values) state.push_back(static_cast<uint8_t>(value.load()));
        state.push_back(static_cast<uint8_t>(m_panel->selectedPatch));
        state.push_back(static_cast<uint8_t>(std::max<size_t>(1,m_panel->bank.size())));
        auto writeString=[&](const std::string& value)
        {
            const auto size=static_cast<uint32_t>(value.size());
            for(unsigned i=0;i<4;++i) state.push_back(static_cast<uint8_t>(size>>(8*i)));
            state.insert(state.end(),value.begin(),value.end());
        };
        if(m_panel->bank.empty()) {writeString(m_panel->patchName);writeString(m_panel->patchText);writeString({});}
        else for(const auto& patch:m_panel->bank) {writeString(patch.name);writeString(patch.text);writeString(std::string(patch.native.begin(),patch.native.end()));}
        writeString(std::string(m_panel->flash.begin(),m_panel->flash.end()));
        return true;
    }
    bool Device::setState(const std::vector<uint8_t>& state,synthLib::StateType)
    {
        if(state.size()<8 || state[0]!='N' || state[1]!='M' || state[2]!='M' || (state[3]<1 || state[3]>4)) return false;
        for(unsigned i=0;i<4;++i) if(state[4+i]>127) return false;
        std::vector<PanelState::PatchEntry> bank;
        std::vector<uint8_t> flash;
        unsigned selected=0;
        if(state[3]==1) bank.push_back({"Restored patch",std::string(state.begin()+8,state.end())});
        else
        {
            if(state.size()<10 || !state[9] || state[9]>PanelState::MaxPatches || state[8]>=state[9]) return false;
            selected=state[8];size_t cursor=10;
            auto readString=[&](std::string& value,size_t limit)
            {
                if(state.size()-cursor<4) return false;
                uint32_t size=0;
                for(unsigned i=0;i<4;++i) size|=uint32_t(state[cursor++])<<(8*i);
                if(size>limit || size>state.size()-cursor) return false;
                value.assign(state.begin()+cursor,state.begin()+cursor+size);cursor+=size;return true;
            };
            for(unsigned i=0;i<state[9];++i)
            {
                PanelState::PatchEntry patch;
                if(!readString(patch.name,1024) || !readString(patch.text,1024*1024)) return false;
                if(state[3]>=3)
                {
                    std::string native;if(!readString(native,65536)) return false;
                    patch.native.assign(native.begin(),native.end());
                    if(!patch.native.empty() && !nmm::validEditorPatch(patch.native)) return false;
                }
                bank.push_back(std::move(patch));
            }
            if(state[3]>=4)
            {
                std::string bytes;if(!readString(bytes,0x100000) || (!bytes.empty() && bytes.size()!=0x100000)) return false;
                flash.assign(bytes.begin(),bytes.end());
            }
            if(cursor!=state.size()) return false;
        }
        std::lock_guard<std::mutex> lock(m_panel->mutex);
        m_panel->flash=std::move(flash);
        // Legacy states held only one patch; keep the bundled bank available.
        ++m_panel->bankRevision;
        if(state[3]==1 && !m_panel->bank.empty())
        {
            const auto found=std::find_if(m_panel->bank.begin(),m_panel->bank.end(),[&](const auto& p){return p.text==bank[0].text;});
            if(found!=m_panel->bank.end()) selected=static_cast<unsigned>(found-m_panel->bank.begin());
            else
            {
                selected=static_cast<unsigned>(m_panel->bank.size()<PanelState::MaxPatches?m_panel->bank.size():m_panel->selectedPatch);
                if(selected==m_panel->bank.size()) m_panel->bank.push_back(std::move(bank[0]));
                else m_panel->bank[selected]=std::move(bank[0]);
            }
        }
        else m_panel->bank=std::move(bank);
        m_panel->selectedPatch=selected;
        m_panel->patchText=m_panel->bank[selected].text;m_panel->patchName=m_panel->bank[selected].name;
        m_panel->ready=false;m_panel->loading=true;m_panel->failed=false;m_panel->status="Loading "+m_panel->patchName;
        for(unsigned i=0;i<4;++i) m_panel->values[i]=state[4+i];
        m_panel->button4=0;
        for(unsigned i=0;i<3;++i) m_panel->restoreKnobs[i]=state[5+i];
        m_panel->patchGeneration.fetch_add(1);
        return true;
    }
#endif
}

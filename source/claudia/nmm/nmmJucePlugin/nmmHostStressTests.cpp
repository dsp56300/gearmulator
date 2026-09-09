// Long-running simulated host through the shared resampler/MIDI/state framework.
// No audio device or GUI required; this does not replace testing in actual DAWs.
#include "nmmDevice.h"
#include "synthLib/plugin.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <cstring>
#include <ctime>
#include <time.h>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <thread>
using Clock=std::chrono::steady_clock;
static void require(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
static bool finite(float value) {uint32_t bits;std::memcpy(&bits,&value,4);return (bits&0x7f800000)!=0x7f800000;}
struct Instance
{
    std::shared_ptr<nmmJucePlugin::PanelState> panel=std::make_shared<nmmJucePlugin::PanelState>();
    std::unique_ptr<nmmJucePlugin::Device> device;
    std::unique_ptr<synthLib::Plugin> host;
    std::vector<float> left,right,input;
    double sum=0,squares=0,leftSum=0,leftSquares=0;uint64_t samples=0;std::atomic<uint64_t> editorBytes{0};
    void editorService()
    {
        auto& transport=*panel->editor;std::lock_guard<std::mutex> lock(transport.mutex);
        require(!transport.rejected,"Editor transport overflow");editorBytes+=transport.output.size();transport.output.clear();
    }
    void note(bool on)
    {
        for(int key:{60,64,67,71}) host->addMidiEvent({synthLib::MidiEventSource::Host,uint8_t(on?0x90:0x80),uint8_t(key),uint8_t(on?100:0),0});
    }
};
// The editor is a service thread in the actual plugin. Keep its mutexes and
// packet handling off the paced host thread here too; otherwise test-side editor
// servicing itself can manufacture missed audio deadlines.
class EditorService
{
    struct Command {Instance* instance;std::vector<uint8_t> bytes;};
    std::vector<std::unique_ptr<Instance>>& instances;
    std::mutex mutex;std::condition_variable changed;
    std::vector<Command> pending;
    std::atomic<bool> stop{false},failed{false};
    std::exception_ptr failure;std::thread worker;
public:
    explicit EditorService(std::vector<std::unique_ptr<Instance>>& values):instances(values)
    {
        pending.reserve(64);
        worker=std::thread([this] {
            try {
                std::vector<Command> batch;batch.reserve(64);
                while(!stop.load()) {
                    batch.clear();
                    {std::unique_lock<std::mutex> lock(mutex);
                     changed.wait_for(lock,std::chrono::milliseconds(1),[&]{return stop.load() || !pending.empty();});
                     if(stop.load()) break;
                     batch.swap(pending);}
                    for(auto& command:batch)
                        require(command.instance->panel->editor->receive(command.bytes.data(),command.bytes.size()),"Editor command rejected");
                    for(auto& instance:instances) instance->editorService();
                }
            } catch(...) {std::lock_guard<std::mutex> lock(mutex);failure=std::current_exception();failed.store(true,std::memory_order_release);}
        });
    }
    ~EditorService() {stop=true;changed.notify_one();worker.join();}
    void submit(Instance& instance,std::vector<uint8_t> bytes)
    {
        {std::lock_guard<std::mutex> lock(mutex);require(pending.size()<64,"Test editor queue full");pending.push_back({&instance,std::move(bytes)});}
        changed.notify_one();
    }
    void check()
    {
        if(failed.load(std::memory_order_acquire)) {std::lock_guard<std::mutex> lock(mutex);std::rethrow_exception(failure);}
    }
};
int main(int argc,char** argv)
{
    try
    {
        require(argc>=5 && argc<=13,"usage: nmmHostStressTests firmware patch-directory playback-seconds instances [--callback-cpu] [--concurrent-state] [--audio-driven frames] [--block-limit 16|32|64] [--deadline-graphs] [--offline]");
        bool offline=false;bool regions=false;unsigned blockLimit=16;
        bool cpuTrace=false,concurrentState=false,audioOverride=false;unsigned audioQuantum=0;
        for(int i=5;i<argc;++i)
        {
            const std::string flag=argv[i];
            if(flag=="--offline") offline=true;
            else if((flag=="--deadline-graphs" || flag=="--sample-regions")) regions=true;
            else if(flag=="--block-limit" && i+1<argc) blockLimit=std::stoul(argv[++i]);
            else if(flag=="--callback-cpu") cpuTrace=true;
            else if(flag=="--concurrent-state") concurrentState=true;
            else if(flag=="--audio-driven" && i+1<argc) {audioQuantum=std::stoul(argv[++i]);audioOverride=true;}
            else throw std::runtime_error("Unknown host stress option");
        }
        auto threadCpu=[]()->uint64_t {
#ifdef CLOCK_THREAD_CPUTIME_ID
            timespec t{};if(clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t)) throw std::runtime_error("Thread CPU clock unavailable");
            return uint64_t(t.tv_sec)*1000000000+uint64_t(t.tv_nsec);
#else
            throw std::runtime_error("Callback CPU tracing is unavailable on this platform");
#endif
        };
        const unsigned seconds=std::stoul(argv[3]),count=std::stoul(argv[4]);
        require(seconds>=40 && seconds<=3600 && count>=1 && count<=8,"Use 40..3600 seconds and 1..8 instances");
        const char* names[]{"FourVoices","101","SimpleSqr1","Gong01","BDrm","BasicOsc","SimpleSynth02","StereoInput"};
        std::vector<nmmJucePlugin::PanelState::PatchEntry> bank;
        for(auto name:names)
        {
            std::ifstream f(std::string(argv[2])+"/"+name+".pch");require(bool(f),"Missing patch fixture");
            bank.push_back({name,std::string(std::istreambuf_iterator<char>(f),{})});
        }
        std::vector<std::unique_ptr<Instance>> instances;
        for(unsigned i=0;i<count;++i)
        {
            auto v=std::make_unique<Instance>();v->panel->runtimeDiagnostics=cpuTrace;v->panel->bank=bank;v->panel->patchText=bank[0].text;v->panel->patchName=bank[0].name;
            if(audioOverride) {v->panel->audioDrivenFrames=audioQuantum;v->panel->audioDrivenLinkedJit=audioQuantum!=0;}
            v->panel->offline=offline;v->panel->jitBlockLimit=blockLimit;v->panel->jitDeadlineGraphs=regions;
            v->device=std::make_unique<nmmJucePlugin::Device>(synthLib::DeviceCreateParams{},argv[1],v->panel);
            v->host=std::make_unique<synthLib::Plugin>(v->device.get(),[](synthLib::Device*)->synthLib::Device* {throw std::runtime_error("Device invalid");});
            instances.push_back(std::move(v));
        }
        EditorService editorService(instances);
        struct Format {unsigned rate,block;};const Format formats[]{{44100,64},{48000,256},{96000,128},{44100,1024}};
        struct CallbackStats {uint64_t count=0,overBudget=0;double maximum=0;};
        std::array<CallbackStats,4> callbackStats{};
        struct Tail {unsigned phase,rate,block;double wall,cpu;};
        std::vector<Tail> tails;tails.reserve(32);
        struct TailReport
        {
            const std::vector<Tail>& tails;
            ~TailReport()
            {
                for(const auto& t:tails) std::cout<<"callback_tail phase="<<t.phase<<" rate="<<t.rate<<" block="<<t.block<<" wall_us="<<t.wall<<" thread_cpu_us="<<t.cpu<<" off_cpu_us="<<std::max(0.0,t.wall-t.cpu)<<'\n';
            }
        } tailReport{tails};
        std::vector<double> callbacks;callbacks.reserve(size_t(seconds)*1500*count);
        unsigned selections=0,restores=0,resumes=0,concurrentSaves=0;uint64_t processed=0;double played=0,maxLate=0;
        const auto start=Clock::now();const auto cpuStart=std::clock();
        for(unsigned phase=0;played<seconds;++phase)
        {
            const auto formatIndex=(phase+phase/8)%4;const auto format=formats[formatIndex];const auto rate=format.rate,block=format.block;
            for(unsigned i=0;i<count;++i)
            {
                auto& v=*instances[i];v.note(false);v.host->setHostSamplerate(float(rate),96000);v.host->setBlockSize(block);v.host->setLatencyBlocks((phase+phase/8)%2);
                v.left.assign(block,0);v.right.assign(block,0);v.input.assign(block,0);v.sum=v.squares=v.leftSum=v.leftSquares=0;v.samples=0;
                const auto selected=(phase+i)%bank.size();
                if(phase%5==4) {v.panel->selectPatch((selected+1)%bank.size(),true);v.panel->selectPatch((selected+2)%bank.size(),true);}
                v.panel->selectPatch(selected,true);++selections;
            }
            auto next=Clock::now();uint64_t localFrames=0;
            auto pump=[&](bool measure,bool playing=true)
            {
                for(auto& pointer:instances)
                {
                    auto& v=*pointer;
                    for(unsigned j=0;j<block;++j) v.input[j]=float(.1*std::sin(double(localFrames+j)*6.283185307179586*220/rate));
                    synthLib::TAudioInputs in{};in[0]=v.input.data();in[1]=v.input.data();
                    synthLib::TAudioOutputs out{};out[0]=v.left.data();out[1]=v.right.data();
                    const auto before=Clock::now();const auto cpuBefore=cpuTrace?threadCpu():0;v.host->process(in,out,block,120,float(localFrames)*2/rate,playing);
                    const auto cpuElapsed=cpuTrace?double(threadCpu()-cpuBefore)/1000:0;
                    const auto elapsed=std::chrono::duration<double,std::micro>(Clock::now()-before).count();
                    if(measure)
                    {
                        callbacks.push_back(elapsed);auto& stats=callbackStats[formatIndex];++stats.count;
                        stats.maximum=std::max(stats.maximum,elapsed);
                        if(elapsed>double(block)*1000000/rate) ++stats.overBudget;
                        if(cpuTrace && elapsed>500)
                        {
                            const Tail tail{phase,rate,block,elapsed,cpuElapsed};
                            if(tails.size()<32) tails.push_back(tail);
                            else {auto smallest=std::min_element(tails.begin(),tails.end(),[](const auto& a,const auto& b){return a.wall<b.wall;});if(elapsed>smallest->wall) *smallest=tail;}
                        }
                    }
                    for(unsigned j=0;j<block;++j)
                    {
                        require(finite(v.left[j]) && finite(v.right[j]) && std::abs(v.left[j])<2 && std::abs(v.right[j])<2,"Invalid/unbounded host audio");
                        if(measure) {v.sum+=v.right[j];v.squares+=double(v.right[j])*v.right[j];v.leftSum+=v.left[j];v.leftSquares+=double(v.left[j])*v.left[j];++v.samples;}
                    }
                    if(v.panel->failed) {std::lock_guard<std::mutex> lock(v.panel->mutex);throw std::runtime_error(v.panel->status);}
                    editorService.check();
                }
                localFrames+=block;++processed;
                next+=std::chrono::nanoseconds(uint64_t(block)*1000000000/rate);
                maxLate=std::max(maxLate,std::chrono::duration<double,std::milli>(Clock::now()-next).count());
                if(!offline) std::this_thread::sleep_until(next);
            };
            auto ready=[&]
            {
                const auto deadline=Clock::now()+std::chrono::seconds(15);
                for(;;)
                {
                    bool all=true;for(auto& v:instances) all &= v->panel->ready.load();
                    if(all) break;require(Clock::now()<deadline,"Patch load timed out");pump(false);
                }
            };
            ready();
            if((phase+phase/8)%4==3)
            {
                // Host state calls are outside process(), with callbacks stopped.
                for(auto& v:instances)
                {
                    std::vector<uint8_t> state;require(v->host->getState(state,synthLib::StateTypeGlobal),"State snapshot failed");
                    require(v->host->setState(state),"State restore failed");++restores;
                }
                next=Clock::now();ready();
            }
            for(auto& v:instances) v->note(true);
            std::future<bool> autosave;
            if(concurrentState) autosave=std::async(std::launch::async,[&]
            {
                for(auto& v:instances)
                {
                    std::vector<uint8_t> state;
                    if(!v->host->getState(state,synthLib::StateTypeGlobal) || state.size()<10)
                    {
                        std::lock_guard<std::mutex> lock(v->panel->mutex);
                        std::cerr<<"autosave_failure phase="<<phase<<" status="<<v->panel->status<<" requested="<<v->panel->snapshotRequest<<" completed="<<v->panel->snapshotCompleted<<" revision="<<v->panel->editor->revision<<" snapshot_revision="<<v->panel->snapshotRevision<<'\n';
                        return false;
                    }
                }
                return true;
            });
            const unsigned phaseBlocks=unsigned(std::ceil(std::min(5.0,double(seconds)-played)*rate/block));
            for(unsigned b=0;b<phaseBlocks;++b)
            {
                if(b==phaseBlocks/2)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(120));next=Clock::now();++resumes;
                }
                if(b==phaseBlocks/3)
                {
                    for(auto& v:instances)
                    {
                        std::vector<uint8_t> store{0xf0,0x33,0x5c,6,65,11,0,0,98};unsigned sum=0;for(auto x:store) sum+=x;store.push_back(sum&127);store.push_back(0xf7);
                        editorService.submit(*v,std::move(store));
                    }
                }
                if(b && b%std::max(1u,rate/block)==0)
                {
                    for(auto& v:instances)
                    {
                        v->panel->values[0]=100+(b%2); // Native volume automation, valid for every graph.
                        std::vector<uint8_t> query{0xf0,0x33,0x5c,6,65,20,0,0};unsigned sum=0;for(auto x:query) sum+=x;query.push_back(sum&127);query.push_back(0xf7);
                        editorService.submit(*v,std::move(query));
                    }
                }
                if(b==phaseBlocks*4/5) for(auto& v:instances) v->note(false);
                pump(true,b<phaseBlocks/4 || b>=phaseBlocks/2); // Transport stop with callbacks, then resume.
            }
            if(concurrentState)
            {
                require(autosave.wait_for(std::chrono::seconds(0))==std::future_status::ready,"Concurrent state snapshot missed its segment deadline");
                require(autosave.get(),"Concurrent state snapshot failed");concurrentSaves+=count;
            }
            played+=double(phaseBlocks)*block/rate;
            for(unsigned i=0;i<count;++i)
            {
                auto& v=*instances[i];
                {std::lock_guard<std::mutex> lock(v.panel->mutex);require(v.panel->bank.size()==99 && !v.panel->bank[98].name.empty(),"Native flash save did not populate slot 99");}
                const double ac=std::sqrt(std::max({0.0,v.squares/v.samples-std::pow(v.sum/v.samples,2),v.leftSquares/v.samples-std::pow(v.leftSum/v.samples,2)}));
                if(ac<=0.00001) throw std::runtime_error(std::string("Selected patch produced no host audio: ")+names[(phase+i)%bank.size()]);
                if(v.panel->underruns || v.panel->droppedJobs)
                {
                    std::cerr<<"queue_failure phase="<<phase<<" instance="<<i<<" rate="<<rate<<" block="<<block
                             <<" underruns="<<v.panel->underruns.load()<<" dropped_jobs="<<v.panel->droppedJobs.load()<<" host_late_max_ms="<<maxLate
                             <<" first_target="<<v.panel->firstMissTarget<<" first_available="<<v.panel->firstMissAvailable<<" first_job_depth="<<v.panel->firstMissJobDepth
                             <<" control_max_us="<<v.panel->controlMaxUs<<" render_max_us="<<v.panel->renderMaxUs<<" job_max_compiles="<<v.panel->jobMaxCompiles<<" worker_job_max_us="<<v.panel->workerJobMaxUs<<" worker_busy_gap_max_us="<<v.panel->workerGapMaxUs<<'\n';
                    throw std::runtime_error("Host stress lost audio/jobs");
                }
                std::cout<<"phase="<<phase<<" instance="<<i<<" patch="<<names[(phase+i)%bank.size()]<<" rate="<<rate<<" block="<<block<<" latency="<<v.host->getLatencyInputToOutput()<<" ac="<<ac<<'\n';
            }
            std::cout.flush();
        }
        const auto wall=std::chrono::duration<double>(Clock::now()-start).count();
        std::sort(callbacks.begin(),callbacks.end());
        std::cout<<"wall_seconds="<<wall<<" playback_seconds="<<played<<" cpu_percent_one_core="<<100.0*(std::clock()-cpuStart)/CLOCKS_PER_SEC/wall
                 <<" offline="<<offline<<" callbacks="<<processed<<" callback_p99_us="<<callbacks[callbacks.size()*99/100]<<" callback_max_us="<<callbacks.back()<<" host_late_max_ms="<<maxLate
                 <<" selections="<<selections<<" state_restores="<<restores<<" resumes="<<resumes<<" concurrent_saves="<<concurrentSaves<<'\n';
        for(unsigned i=0;i<4;++i)
            std::cout<<"format_rate="<<formats[i].rate<<" block="<<formats[i].block<<" callback_budget_us="<<double(formats[i].block)*1000000/formats[i].rate
                     <<" callback_max_us="<<callbackStats[i].maximum<<" callback_over_budget="<<callbackStats[i].overBudget<<" callback_count="<<callbackStats[i].count<<'\n';
        for(auto& v:instances)
        {
            require(v->editorBytes>0,"No editor responses");
            if(cpuTrace) std::cout<<"worker_job_max_us="<<v->panel->workerJobMaxUs<<" worker_busy_gap_max_us="<<v->panel->workerGapMaxUs<<" priority_applied="<<v->panel->priorityApplied<<'\n';
        }
        std::cout<<"PASS shared-host resampling, MIDI, eight patches, state restore, automation and stop/resume\n";
    }
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}

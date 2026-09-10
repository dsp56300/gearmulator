#include "hardware.h"
#include "playback.h"
#include "dsp56kBase/logging.h"
#include <charconv>
#include <chrono>
#include <iostream>
#include <limits>
#include <stdexcept>

static uint64_t number(const std::string& text)
{
    uint64_t result=0;
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),result);
    if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size()||!result)
        throw std::invalid_argument("Expected a positive integer: "+text);
    return result;
}
int main(int argc,char** argv)
{
    if(argc<2)
    {
        std::cerr<<"Usage: nmrack ROM [machine-frames=480000] [step-budget=450000000]\n"
            <<"  [--tone] [--experimental-chain] [--word-serial] [--unlinked] [--trace]\n"
            <<"  [--diagnostics PREFIX] [--mix-wav FILE] [--dac-wav FILE] [--output-tones] [--validate-dac]\n"
            <<"  [--realtime-test SECONDS] (paced 96-kHz callback simulation; no audio device)\n";
        return 2;
    }
    std::unique_ptr<nmrack::Hardware> hardware;
    try
    {
        nmrack::Hardware::Options options;
        unsigned frames=480000,positional=0;uint64_t budget=450000000;
        unsigned realtimeSeconds=0;
        bool tone=false,outputTones=false;std::string diagnostics,wav,dacWav;
        for(int i=2;i<argc;++i)
        {
            const std::string arg=argv[i];
            if(arg=="--tone") tone=true;
            else if(arg=="--output-tones") outputTones=true;
            else if(arg=="--validate-dac") {options.validateDac=true;options.wordSerial=true;options.experimentalChain=true;}
            else if(arg=="--trace") options.trace=true;
            else if(arg=="--unlinked") options.linkedJit=false;
            else if(arg=="--experimental-chain") options.experimentalChain=true;
            else if(arg=="--word-serial") {options.experimentalChain=true;options.wordSerial=true;}
            else if(arg=="--diagnostics"&&i+1<argc) diagnostics=argv[++i];
            else if(arg=="--mix-wav"&&i+1<argc) wav=argv[++i];
            else if(arg=="--dac-wav"&&i+1<argc) {dacWav=argv[++i];options.wordSerial=true;options.experimentalChain=true;}
            else if(arg=="--realtime-test"&&i+1<argc)
            {const auto seconds=number(argv[++i]);if(seconds>3600) throw std::invalid_argument("Realtime test limited to one hour");realtimeSeconds=unsigned(seconds);options.wordSerial=true;options.experimentalChain=true;}
            else if(!arg.empty()&&arg[0]!='-'&&positional<2)
            {
                const auto n=number(arg);
                if(!positional) {if(n>std::numeric_limits<unsigned>::max()) throw std::invalid_argument("Frame count too large");frames=unsigned(n);}
                else budget=n;
                ++positional;
            }
            else throw std::invalid_argument("Unknown/incomplete option: "+arg);
        }
        if(!wav.empty()&&!options.experimentalChain)
            throw std::invalid_argument("--mix-wav requires --experimental-chain; codec output is not yet validated");
        if(tone&&outputTones) throw std::invalid_argument("Choose --tone or --output-tones");
        options.boardDiagnostics=!diagnostics.empty();
        if(!options.trace) Logging::setLogFunc([](const std::string&){});
        const auto start=std::chrono::steady_clock::now();
        hardware=std::make_unique<nmrack::Hardware>(argv[1],options);
        hardware->setStepBudget(budget);hardware->boot(frames);
        if(tone) {hardware->initializeTone();hardware->advance(96000);}
        if(outputTones) {hardware->initializeOutputTones();hardware->advance(96000);}
        if(!diagnostics.empty()) hardware->captureDiagnostics(diagnostics);
        if(!wav.empty()) hardware->writeMixWav(wav);
        if(!dacWav.empty()) hardware->writeDacWav(dacWav);
        std::cout<<"elapsed_seconds="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<'\n';
        hardware->report();
        if(realtimeSeconds)
        {
            nmrack::Playback playback(std::move(hardware));playback.prefill();
            std::array<nmrack::AudioFrame,128> audio;
            const auto epoch=std::chrono::steady_clock::now();
            for(uint64_t done=0;done<uint64_t(realtimeSeconds)*96000;done+=audio.size())
            {
                std::this_thread::sleep_until(epoch+std::chrono::nanoseconds((done+audio.size())*1000000000/96000));
                playback.consume(audio.data(),unsigned(audio.size()));
                playback.checkError();
            }
            playback.stop();playback.checkError();
            std::cout<<"realtime_test_seconds="<<realtimeSeconds<<" underruns="<<playback.underruns()<<" missing_frames="<<playback.missingFrames()<<'\n';
            if(playback.underruns()) return 1;
        }
        return 0;
    }
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';if(hardware) hardware->report();return 1;}
}

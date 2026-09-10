// Deterministic OS 3.03b / 101.pch reference capture.
//
// This is deliberately a small offline harness around the public nmm::Hardware
// API.  It does not alter emulator behaviour and it never captures from an
// audio callback.  The output is a directory containing exact float bit
// patterns, firmware events, and bounded dependency-state snapshots for the
// native implementation.

#include "../../nmm/nmmLib/nmmcapture.h"
#include "../../nmm/nmmLib/nmmhardware.h"
#include "../../nmm/nmmLib/nmmrom.h"
#include "../../nmm/nmmLib/nmmpatch.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <cerrno>
#include <fstream>
#include <iomanip>
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <sys/stat.h>

namespace
{
using Path=std::string;

Path join(const Path& directory,const std::string& name)
{
    return directory.empty()||directory.back()=='/'?directory+name:directory+'/'+name;
}

void createDirectories(const Path& path)
{
    // std::filesystem is unavailable under this project's macOS 10.12
    // deployment target. Keep this tiny equivalent local to the tool.
    Path current;
    for(size_t i=0;i<path.size();++i)
    {
        current.push_back(path[i]);
        if(path[i]!='/' && i+1!=path.size()) continue;
        if(current.size()==1 && current[0]=='/') continue;
        if(::mkdir(current.c_str(),0755)!=0 && errno!=EEXIST)
            throw std::runtime_error("Cannot create "+current+": "+std::strerror(errno));
    }
    if(::mkdir(path.c_str(),0755)!=0 && errno!=EEXIST)
        throw std::runtime_error("Cannot create "+path+": "+std::strerror(errno));
}

void require(bool value,const std::string& message)
{
    if(!value) throw std::runtime_error(message);
}

std::vector<uint8_t> bytes(const Path& path)
{
    std::ifstream input(path,std::ios::binary);
    require(bool(input),"Cannot read "+path);
    return {std::istreambuf_iterator<char>(input),{}};
}

void writeBytes(const Path& path,const std::vector<uint8_t>& data)
{
    std::ofstream output(path,std::ios::binary);
    require(bool(output),"Cannot write "+path);
    output.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size()));
    require(bool(output),"Short write "+path);
}

std::string hexWord(uint32_t value,unsigned width)
{
    std::ostringstream out;
    out<<std::hex<<std::setfill('0')<<std::setw(static_cast<int>(width))<<value;
    return out.str();
}

void writeMemory(const nmm::Hardware& hardware,char space,uint32_t begin,uint32_t words,const Path& path)
{
    std::vector<uint8_t> result;
    result.reserve(space=='C'?words:words*3);
    for(uint32_t i=0;i<words;++i)
    {
        const auto value=hardware.readMemory(space,begin+i);
        if(space=='C') result.push_back(static_cast<uint8_t>(value));
        else
        {
            result.push_back(static_cast<uint8_t>(value>>16));
            result.push_back(static_cast<uint8_t>(value>>8));
            result.push_back(static_cast<uint8_t>(value));
        }
    }
    writeBytes(path,result);
}

void writeAudio(const Path& path,const std::vector<std::array<float,2>>& audio)
{
    std::ofstream output(path,std::ios::binary);
    require(bool(output),"Cannot write "+path);
    // Header: magic, version, frame count. Each following frame is two IEEE
    // 754 single-precision values in native-independent little-endian bits.
    const uint8_t header[12]={'N','M','M','A',1,0,0,0,
        static_cast<uint8_t>(audio.size()),static_cast<uint8_t>(audio.size()>>8),
        static_cast<uint8_t>(audio.size()>>16),static_cast<uint8_t>(audio.size()>>24)};
    output.write(reinterpret_cast<const char*>(header),sizeof(header));
    for(const auto& frame:audio)
        for(float sample:frame)
        {
            uint32_t bits=0;std::memcpy(&bits,&sample,sizeof(bits));
            const uint8_t encoded[4]={static_cast<uint8_t>(bits),static_cast<uint8_t>(bits>>8),
                static_cast<uint8_t>(bits>>16),static_cast<uint8_t>(bits>>24)};
            output.write(reinterpret_cast<const char*>(encoded),sizeof(encoded));
        }
    require(bool(output),"Short write "+path);
}

void writeIntegerAudio(const Path& path,const std::vector<std::array<float,2>>& audio)
{
    std::ofstream output(path,std::ios::binary);
    require(bool(output),"Cannot write "+path);
    const uint8_t header[12]={'N','M','M','I',1,0,0,0,
        static_cast<uint8_t>(audio.size()),static_cast<uint8_t>(audio.size()>>8),
        static_cast<uint8_t>(audio.size()>>16),static_cast<uint8_t>(audio.size()>>24)};
    output.write(reinterpret_cast<const char*>(header),sizeof(header));
    for(const auto& frame:audio)
        for(float sample:frame)
        {
            // decodeDac() divides a signed 18-bit integer by 2^17.  The
            // conversion is therefore exact for every emulator output float;
            // retaining this integer stream avoids hiding a one-LSB mismatch
            // behind a decimal audio comparison.
            const auto integer=static_cast<int64_t>(std::llround(double(sample)*131072.0));
            require(integer>=-131072 && integer<=131071,"Audio sample outside signed-18-bit range");
            require(std::abs(double(sample)-double(integer)/131072.0)==0.0,
                    "Audio sample is not an exact signed-18-bit decode");
            const auto value=static_cast<int32_t>(integer);
            const uint8_t encoded[4]={static_cast<uint8_t>(value),static_cast<uint8_t>(value>>8),
                static_cast<uint8_t>(value>>16),static_cast<uint8_t>(value>>24)};
            output.write(reinterpret_cast<const char*>(encoded),sizeof(encoded));
        }
    require(bool(output),"Short write "+path);
}

struct Snapshot
{
    const char* name;
    unsigned cBegin,cWords;
};

void snapshot(const nmm::Hardware& hardware,const Path& directory,const char* phase)
{
    const auto before=hardware.timing();
    // These C regions contain the compiler's area/voice records, MIDI event
    // state and parameter copies used by 101.pch.  X/Y are intentionally
    // captured in full within the public diagnostic readMemory() window
    // (0x20000 words).  The physical DSP X/Y spaces are larger; addresses
    // outside this window still need explicit access tracing before they can
    // be called dependencies of the native oscillator/filter graph.
    constexpr Snapshot regions[]={{"mcu_165000",0x165000,0x10000},
                                  {"mcu_175000",0x175000,0x10000}};
    for(const auto& region:regions)
        writeMemory(hardware,'C',region.cBegin,region.cWords,
                    join(directory,std::string(phase)+"_"+region.name+".bin"));
    writeMemory(hardware,'X',0,0x20000,join(directory,std::string(phase)+"_dsp_x.bin"));
    writeMemory(hardware,'Y',0,0x20000,join(directory,std::string(phase)+"_dsp_y.bin"));
    writeMemory(hardware,'P',0,0x20000,join(directory,std::string(phase)+"_dsp_p.bin"));
    std::ofstream registers(join(directory,std::string(phase)+".state"));
    hardware.dumpState(registers);
    require(bool(registers),"Cannot write register snapshot");
    const auto after=hardware.timing();
    require(before.cycles==after.cycles && before.frames==after.frames,
            "Snapshot advanced emulated execution");
}

void appendAudio(std::vector<std::array<float,2>>& destination,nmm::Hardware& hardware,
                 unsigned frames,uint64_t budget,const char* phase,const Path& directory)
{
    const auto start=destination.size();
    const auto block=hardware.render(frames,budget);
    destination.insert(destination.end(),block.begin(),block.end());
    std::ofstream phases(join(directory,"phases.csv"),std::ios::app);
    require(bool(phases),"Cannot append phases.csv");
    phases<<phase<<','<<start<<','<<frames<<'\n';
}

void writeFileManifest(const Path& directory)
{
    // Hash every immutable capture artifact after it has been written.  The
    // index itself is intentionally omitted so regenerating it cannot make a
    // self-referential hash unstable.
    static constexpr const char* names[]={
        "manifest.csv","phases.csv","audio.nmma","audio.int18","events.csv","summary.txt",
        "warmup_mcu_165000.bin","warmup_mcu_175000.bin","warmup_dsp_x.bin","warmup_dsp_y.bin","warmup_dsp_p.bin","warmup.state",
        "attack_mcu_165000.bin","attack_mcu_175000.bin","attack_dsp_x.bin","attack_dsp_y.bin","attack_dsp_p.bin","attack.state",
        "release_mcu_165000.bin","release_mcu_175000.bin","release_dsp_x.bin","release_dsp_y.bin","release_dsp_p.bin","release.state"};
    std::ofstream index(join(directory,"files.csv"));
    require(bool(index),"Cannot write files.csv");
    index<<"file,bytes,sha256\n";
    for(const auto* name:names)
    {
        const auto data=bytes(join(directory,name));
        index<<name<<','<<data.size()<<','<<nmm::sha256(data)<<'\n';
    }
}
}

int main(int argc,char** argv)
{
    try
    {
        require(argc>=3 && argc<=5,
                "usage: nmm101Capture firmware.bin output-dir [101.pch] [capture-frames]");
        const Path firmware=argv[1];
        const Path directory=argv[2];
        const Path patch=argc>=4?Path(argv[3]):Path("nord-micro-modular/patches/101.pch");
        const unsigned frames=argc==5?static_cast<unsigned>(std::stoul(argv[4])):4096;
        require(frames>0 && frames<=65536,"capture-frames must be in 1..65536");
        createDirectories(directory);

        const auto firmwareBytes=bytes(firmware);
        const auto patchBytes=bytes(patch);
        const auto fixture=nmm::Patch::load(patch);
        nmm::Capture capture;
        nmm::Hardware hardware(firmware);
        hardware.setAudioDrivenExecution(64,false);
        hardware.setDeadlineLinkedJit(true);
        hardware.setJitBlockLimit(16,false);
        hardware.setCapture(&capture);
        hardware.boot(30000000);
        hardware.loadPatch(fixture,30000000);
        hardware.setMasterVolume(100);

        std::ofstream manifest(join(directory,"manifest.csv"));
        require(bool(manifest),"Cannot write manifest.csv");
        manifest<<"key,value\n"
            <<"capture_version,1\n"
            <<"sample_rate,96000\n"
            <<"patch,101.pch\n"
            <<"firmware_sha256,"<<nmm::sha256(firmwareBytes)<<"\n"
            <<"patch_sha256,"<<nmm::sha256(patchBytes)<<"\n"
            <<"mcu_snapshot_regions,0x165000:0x10000;0x175000:0x10000\n"
            <<"dsp_snapshot_words,0x20000\n"
            <<"audio_format,NMMA-v1-le-f32-stereo\n"
            <<"integer_audio_format,NMMI-v1-le-signed18-stereo\n"
            <<"scheduler,audio-driven-64-cooperative\n"
            <<"jit_block_limit,16\n"
            <<"deadline_linked_jit,true\n"
            <<"master_volume,100\n"
            <<"schedule,warmup=96000;note_on="<<frames<<";note_off=8192\n";
        manifest.close();
        std::ofstream phases(join(directory,"phases.csv"));
        phases<<"phase,start_frame,frame_count\n";
        phases.close();

        // The warmup reaches the same steady 101 graph state used by the
        // existing regression tests. Events before this point remain in the
        // capture for boot/compiler provenance.
        std::vector<std::array<float,2>> audio;
        audio.reserve(96000+frames+8192);
        appendAudio(audio,hardware,96000,10000000,"warmup",directory);
        snapshot(hardware,directory,"warmup");
        // MIDI is queued at a deterministic rendered-frame boundary.
        hardware.sendMidi(0x90,60,100);
        appendAudio(audio,hardware,frames,10000000,"note_on",directory);
        snapshot(hardware,directory,"attack");
        hardware.sendMidi(0x80,60,0);
        appendAudio(audio,hardware,8192,10000000,"release",directory);
        snapshot(hardware,directory,"release");
        require(!capture.full,"Reference event capture exceeded fixed capacity; refusing truncated baseline");
        writeAudio(join(directory,"audio.nmma"),audio);
        writeIntegerAudio(join(directory,"audio.int18"),audio);
        std::ofstream events(join(directory,"events.csv"));
        require(bool(events),"Cannot write events.csv");
        capture.write(events);
        events.close();

        // A compact text summary makes accidental changes to the capture
        // schedule visible in CI without parsing the binary state files.
        const auto timing=hardware.timing();
        std::ofstream summary(join(directory,"summary.txt"));
        require(bool(summary),"Cannot write summary.txt");
        summary<<"events="<<capture.count<<"\n"
               <<"events_full="<<capture.full<<"\n"
               <<"frames="<<audio.size()<<"\n"
               <<"dsp_cycles="<<timing.cycles<<"\n"
               <<"emulator_frames="<<timing.frames<<"\n"
               <<"control_sample_counter="<<timing.controlSampleCounter<<"\n";
        summary.close();
        writeFileManifest(directory);
        std::cout<<"PASS nmm101Capture output="<<directory
                 <<" frames="<<audio.size()<<" events="<<capture.count<<'\n';
        return 0;
    }
    catch(const std::exception& error)
    {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}

#include "nmmLib/nmmhardware.h"
#include "nmmLib/nmmpatch.h"
#include <fstream>
#include <cmath>
#include <algorithm>
#include "synthLib/wavWriter.h"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <chrono>
#include <sstream>

int main(int argc,char** argv)
{
	std::unique_ptr<nmm::Hardware> hw;
	std::ofstream trace;
	try
	{
		std::string firmware,patchFile,traceFile,snapshotPrefix,output="nmm.wav";
		uint64_t budget=30000000;
		uint32_t blockSize=128,window=1,audioQuantum=0;
		uint8_t note=60,velocity=100,channel=0;
		uint16_t voices=0;
		std::vector<uint8_t> chord;
		bool bootOnly=false,benchmark=false,linked=false,threaded=true;
		for(int i=1;i<argc;++i)
		{
			const std::string arg=argv[i];
			if(arg=="--help") {std::cout << "nmmTestConsole --firmware decoded.bin [--patch patch.pch] [--output audio.wav] [--boot-only] [--budget instructions] [--trace path] [--snapshot-prefix path] [--benchmark] [--block-size frames] [--dsp-window 0..32] [--linked-jit] [--audio-driven 1..64] [--cooperative] [--note 0..127] [--velocity 0..127] [--channel 1..16] [--voices 1..32] [--chord 60,64,67,71]\n";return 0;}
			if(arg=="--cooperative") {threaded=false;continue;}
            if(arg=="--linked-jit") {linked=true;continue;}
			if(arg=="--boot-only") {bootOnly=true;continue;}
			if(arg=="--benchmark") {benchmark=true;continue;}
			if(i+1>=argc) throw std::runtime_error("Missing value for "+arg);
			if(arg=="--audio-driven") {size_t end;const std::string value=argv[++i];const auto n=std::stoul(value,&end);if(end!=value.size() || value[0]=='-' || n<1 || n>64) throw std::runtime_error("Invalid audio quantum");audioQuantum=uint32_t(n);}
            else if(arg=="--dsp-window") {size_t end;const std::string value=argv[++i];const auto n=std::stoul(value,&end);if(end!=value.size() || value[0]=='-' || n>32) throw std::runtime_error("Invalid DSP window");window=uint32_t(n);}
            else if(arg=="--firmware") firmware=argv[++i];
			else if(arg=="--patch") patchFile=argv[++i];
			else if(arg=="--output") output=argv[++i];
			else if(arg=="--trace") traceFile=argv[++i];
			else if(arg=="--snapshot-prefix") snapshotPrefix=argv[++i];
			else if(arg=="--budget") {size_t end;auto value=std::string(argv[++i]);budget=std::stoull(value,&end);if(end!=value.size()||value[0]=='-'||!budget) throw std::runtime_error("Invalid budget");}
			else if(arg=="--voices") {size_t end;const std::string value=argv[++i];auto n=std::stoi(value,&end);if(end!=value.size()||n<1||n>32) throw std::runtime_error("Invalid voices");voices=uint16_t(n);}
			else if(arg=="--chord") {std::istringstream notes(argv[++i]);std::string value;while(std::getline(notes,value,',')) {size_t end;auto n=std::stoi(value,&end);if(end!=value.size()||n<0||n>127) throw std::runtime_error("Invalid chord");chord.push_back(uint8_t(n));}if(chord.empty() || chord.size()>32 || std::string(argv[i]).back()==',') throw std::runtime_error("Invalid chord");}
			else if(arg=="--block-size") {size_t end;auto value=std::string(argv[++i]);auto n=std::stoull(value,&end);if(end!=value.size()||value[0]=='-'||!n||n>384000) throw std::runtime_error("Invalid block size");blockSize=static_cast<uint32_t>(n);}
			else if(arg=="--note" || arg=="--velocity" || arg=="--channel")
			{
				size_t end;auto value=std::string(argv[++i]);auto n=std::stoull(value,&end);
				if(end!=value.size()||value[0]=='-'||n>127||(arg=="--channel" && (n<1||n>16))) throw std::runtime_error("Invalid MIDI argument");
				if(arg=="--note") note=static_cast<uint8_t>(n);
				else if(arg=="--velocity") velocity=static_cast<uint8_t>(n);
				else channel=static_cast<uint8_t>(n-1);
			}
			else throw std::runtime_error("Unknown option "+arg);
		}
		if(firmware.empty()) throw std::runtime_error("--firmware is required");
		if(!traceFile.empty()) {trace.open(traceFile);if(!trace) throw std::runtime_error("Cannot open trace");}
		const auto setupStart=std::chrono::steady_clock::now();
		hw=std::make_unique<nmm::Hardware>(firmware,traceFile.empty()?nullptr:&trace);
		hw->setAudioDrivenExecution(audioQuantum,threaded);hw->setDeadlineLinkedJit(linked);hw->setDspExecutionWindow(window);hw->boot(budget);
		if(!bootOnly)
		{
			if(patchFile.empty()) hw->initializePatch(budget);
			else {auto patch=nmm::Patch::load(patchFile);if(voices) patch.requestedVoices=uint8_t(voices);hw->loadPatch(patch,budget);}
			const auto setupSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-setupStart).count();
			std::vector<double> blockTimes;
			if(benchmark) blockTimes.reserve(480000/blockSize+2);
			uint64_t deadlineMisses=0;
			double renderSeconds=0,firstBlockSeconds=0;
			auto render=[&](uint32_t count)
			{
				if(!benchmark) return hw->render(count,budget);
				std::vector<std::array<float,2>> result;
				result.reserve(count);
				while(result.size()<count)
				{
					const auto n=std::min(blockSize,count-static_cast<uint32_t>(result.size()));
					const auto start=std::chrono::steady_clock::now();
					auto block=hw->render(n,budget);
					const auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
					if(blockTimes.empty()) firstBlockSeconds=seconds;
					blockTimes.push_back(seconds);
					renderSeconds+=seconds;
					if(seconds>double(n)/96000) ++deadlineMisses;
					result.insert(result.end(),block.begin(),block.end());
				}
				return result;
			};
			auto snapshot=[&](const char* phase)
			{
				if(snapshotPrefix.empty()) return;
				std::ofstream out(snapshotPrefix+"-"+phase+".state");
				if(!out) throw std::runtime_error("Cannot open state snapshot");
				hw->dumpState(out);
			};
			snapshot("ready");
			if(chord.empty()) chord.push_back(note);
			for(auto n:chord) hw->sendMidi(0x90|channel,n,velocity);
			auto audio=render(384000);
			snapshot("held");
			for(auto n:chord) hw->sendMidi(0x80|channel,n,0);
			auto release=render(96000);
			snapshot("released");
			if(benchmark)
			{
				std::sort(blockTimes.begin(),blockTimes.end());
				std::cout << "setup_seconds=" << setupSeconds << " render_seconds=" << renderSeconds
					<< " realtime_factor=" << 5.0/renderSeconds << " block_frames=" << blockSize
					<< " first_block_ms=" << firstBlockSeconds*1000
					<< " block_p50_ms=" << blockTimes[blockTimes.size()/2]*1000
					<< " block_p99_ms=" << blockTimes[(blockTimes.size()-1)*99/100]*1000
					<< " block_max_ms=" << blockTimes.back()*1000
					<< " deadline_misses=" << deadlineMisses << " measured_blocks=" << blockTimes.size() << '\n';
			}
			audio.insert(audio.end(),release.begin(),release.end());
			synthLib::WavWriter writer;
			if(!writer.write(output,32,true,2,96000,audio)) throw std::runtime_error("Cannot write WAV");
			float peak=0;
			for(auto f:audio) for(auto v:f)
			{
				if(!std::isfinite(v)) throw std::runtime_error("Non-finite audio sample");
				peak=std::max(peak,std::abs(v));
			}
			std::cout << "audio_peak=" << peak << '\n';
			double bestRms=0;
			for(unsigned channel=0;channel<2;++channel)
			{
				double sum=0,squares=0;
				// Exclude initial volume ramp and the note-off tail.
				for(size_t i=96000;i<384000;++i)
				{
					const double v=audio[i][channel];
					sum+=v;squares+=v*v;
				}
				const double mean=sum/288000;
				bestRms=std::max(bestRms,std::sqrt(std::max(0.0,squares/288000-mean*mean)));
			}
			std::cout << "audio_ac_rms=" << bestRms << '\n';
			// A zero-sustain envelope can finish before the steady-state window.
			// Short windows distinguish its attack from the slow codec/DC ramp.
			double attackRms=0;
			for(unsigned channel=0;channel<2;++channel)
				for(size_t begin=960;begin<48000;begin+=960)
				{
					double sum=0,squares=0;
					for(size_t i=begin;i<begin+960;++i)
					{
						const double v=audio[i][channel];
						sum+=v;squares+=v*v;
					}
					attackRms=std::max(attackRms,std::sqrt(std::max(0.0,squares/960-std::pow(sum/960,2))));
				}
			std::cout << "audio_attack_ac_rms=" << attackRms << '\n';
			if(std::max(bestRms,attackRms)<1e-5) throw std::runtime_error("Audio validation failed: silence or DC (diagnostic WAV retained)");
		}
		hw->report(std::cout);
		return 0;
	}
	catch(const std::exception& e)
	{
		std::cerr << "nmm: " << e.what() << '\n';
		if(hw) hw->report(std::cerr);
		return 1;
	}
}

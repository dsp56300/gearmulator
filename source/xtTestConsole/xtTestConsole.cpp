#include "synthLib/wavWriter.h"

#include "xtLib/xtUc.h"
#include "xtLib/xtHardware.h"
#include "xtLib/xt.h"

#include "dsp56kEmu/jitunittests.h"

int main()
{
//	dsp56k::JitUnittests tests;

	const std::unique_ptr uc(std::make_unique<xt::Xt>(std::vector<uint8_t>(), std::string()));

	constexpr uint32_t blockSize = 64;
	std::vector<dsp56k::TWord> stereoOutput;
	stereoOutput.resize(blockSize<<1);

	synthLib::AsyncWriter writer("xtEmu_Output.wav", 40000, false);

	constexpr uint32_t period = 0x10000;

	while(!uc->isBootCompleted())
		uc->process(blockSize);

	uc->getHardware()->getDSP().thread().setLogToStdout(true);

	auto processLength = [&](const uint32_t _length)
	{
		size_t total = 0;
		while(total < _length)
		{
			uc->process(blockSize);

			auto& outs = uc->getAudioOutputs();

			for(size_t i=0; i<blockSize; ++i)
			{
				stereoOutput[(i<<1)  ] = outs[0][i];
				stereoOutput[(i<<1)+1] = outs[1][i];
			}

			writer.append([&](std::vector<dsp56k::TWord>& _wavOut)
			{
				_wavOut.insert(_wavOut.end(), stereoOutput.begin(), stereoOutput.end());
			});

			total += blockSize;
		}
	};
	
	for(size_t i=0; i<5000; ++i)
		uc->process(blockSize);

	auto sleep = [](const uint32_t _seconds = 1)
	{
		std::this_thread::sleep_for(std::chrono::seconds(_seconds));
	};

	auto sendButton = [&](const xt::ButtonType _button, const bool _pressed)
	{
		auto& u = uc->getHardware()->getUC();
		u.setButton(_button, _pressed);
	};

	auto sendEncoder = [&](const uint8_t _encoder, const int8_t _delta)
	{
		synthLib::SMidiEvent e(synthLib::MidiEventSource::Host);
		constexpr uint8_t idm = 0x26;	// RMTP
		const uint8_t uu = _encoder;	// encoder
		const uint8_t mm = 64 + _delta;	// movement
		const uint8_t checksum = (uu + mm) & 0x7f;
		e.sysex = {0xf0, 0x3e, 0x0e, 0x7f, idm, uu, mm, checksum, 0xf7};
		uc->sendMidiEvent(e);
	};

	sendButton(xt::ButtonType::Play, true);		// play/shift
	processLength(64 * 5);
	sendButton(xt::ButtonType::Power, true);	// power
	processLength(64 * 5);

	processLength(64 * 500);

	sendButton(xt::ButtonType::Power, false);	// power
	processLength(64);
	sendButton(xt::ButtonType::Play, false);	// play/shift
	processLength(64);

	processLength(64 * 500);

	sendEncoder(0x2, 1);	// encoder #3

	processLength(64 * 500);

	sendEncoder(0x4, 2);	// red encoder

	const auto& mem = uc->getHardware()->getDSP(0).dsp().memory();

//	int count = 0;

	while(true)
	{
		/*
		++count;
		if(count == 10000)
		{
			// dump wavetables from DSP RAM
			// Wavetables are stored with 64 waves per part
			// Size of one wave is stored as "multisamples" with the
			// first size being 128, the last two ones being 1
			// This is used to ensure that WTs are band limited
			std::vector<uint32_t> data;

			for(uint32_t p=0; p<8; ++p)
			{
				uint32_t offP = p * 0x4000;

				for(uint32_t wt=0; wt<64; ++wt)
				{
					uint32_t offWT = wt * 256;

					for(uint32_t i=0; i<128; ++i)
					{
						auto v = mem.get(dsp56k::MemArea_Y, 0x20000 + offP + offWT + i);
						data.push_back(v << 8);
					}
				}
			}

			synthLib::writeFile("wavetables.bin", reinterpret_cast<const uint8_t*>(data.data()), data.size() * 4);
		}
		*/
		processLength(64);
	}

	size_t repeats = 1;

	sleep();

	constexpr uint8_t notes[] = {60 - 12, 60, 60 + 12};

	for(uint8_t p=0; p<=128; ++p)
	{
		sleep();
		LOG("PROGRAM CHANGE " << static_cast<int>(p));
		sleep();
		uc->sendMidiEvent({synthLib::MidiEventSource::Host, synthLib::M_PROGRAMCHANGE, p, 0});

		processLength(128);

		for(size_t r=0; r<repeats; ++r)
		{
			for (uint8_t note : notes)
			{
				sleep();
				LOG("NOTE ON " << static_cast<int>(note));
				sleep();
				uc->sendMidiEvent({synthLib::MidiEventSource::Host, synthLib::M_NOTEON, note, 127});
				processLength(period);
				sleep();
				LOG("NOTE OFF " << static_cast<int>(note));
				sleep();
				uc->sendMidiEvent({synthLib::MidiEventSource::Host, synthLib::M_NOTEOFF, note, 127});
				processLength(period);
			}
		}
	}

	LOG("END");

	return 0;
}

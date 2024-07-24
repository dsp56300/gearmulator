#include <iostream>

#include "dsp56kEmu/threadtools.h"
#include "n2xLib/n2xhardware.h"
#include "n2xLib/n2xrom.h"
#include "synthLib/wavWriter.h"

namespace n2x
{
	class Hardware;
}

int main()
{
	const n2x::Rom rom;

	if(!rom.isValid())
	{
		std::cout << "Failed to load rom file\n";
		return -1;
	}

	std::unique_ptr<n2x::Hardware> hw;
	hw.reset(new n2x::Hardware());

	std::thread ucThread([&]()
	{
		dsp56k::ThreadTools::setCurrentThreadName("MC68331");
		dsp56k::ThreadTools::setCurrentThreadPriority(dsp56k::ThreadPriority::Highest);

		while(true)
		{
			hw->processUC();
			hw->processUC();
			hw->processUC();
			hw->processUC();
			hw->processUC();
			hw->processUC();
			hw->processUC();
			hw->processUC();
		}
	});

	constexpr uint32_t blockSize = 64;

	std::vector<dsp56k::TWord> stereoOutput;
	stereoOutput.resize(blockSize<<1);

	synthLib::AsyncWriter writer("n2xEmu_out.wav", n2x::g_samplerate, false);

	while(true)
	{
		hw->processAudio(blockSize);

		auto& outs = hw->getAudioOutputs();

		for(size_t i=0; i<blockSize; ++i)
		{
			stereoOutput[(i<<1)  ] = outs[0][i] + outs[2][i];
			stereoOutput[(i<<1)+1] = outs[1][i] + outs[3][i];
		}

		writer.append([&](std::vector<dsp56k::TWord>& _wavOut)
		{
			_wavOut.insert(_wavOut.end(), stereoOutput.begin(), stereoOutput.end());
		});
	}
}

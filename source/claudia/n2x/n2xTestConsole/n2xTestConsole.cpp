#include <iostream>

#include "n2xLib/n2xhardware.h"

#include "synthLib/consoleTools.h"

static constexpr bool g_factoryDemo = true;

namespace n2x
{
	class Hardware;
}

int main()
{
	std::unique_ptr<n2x::Hardware> hw;
	hw.reset(new n2x::Hardware());

	if(!hw->isValid())
	{
		std::cout << "Failed to load rom file\n";
		return -1;
	}

	hw->getDSPA().getDSPThread().setLogToStdout(true);
	hw->getDSPB().getDSPThread().setLogToStdout(true);

	constexpr uint32_t blockSize = 64;

	std::vector<dsp56k::TWord> left(blockSize), right(blockSize);

	synthLib::StereoWavSink wav("n2xEmu_out.wav", n2x::g_samplerate);

	uint32_t totalSamples = 0;

	while(true)
	{
		hw->processAudio(blockSize, blockSize);

		totalSamples += blockSize;

		auto seconds = [&](uint32_t _seconds)
		{
			return _seconds * (n2x::g_samplerate / blockSize) * blockSize;
		};

		if constexpr (g_factoryDemo)
		{
			// Run factory demo, press shift + osc sync
			if(totalSamples == seconds(4))
			{
				hw->setButtonState(n2x::ButtonType::Shift, true);
				hw->setButtonState(n2x::ButtonType::OscSync, true);
			}

			else if(totalSamples == seconds(5))
			{
				hw->setButtonState(n2x::ButtonType::Shift, false);
				hw->setButtonState(n2x::ButtonType::OscSync, false);
			}
		}

		auto& outs = hw->getAudioOutputs();

		for(size_t i=0; i<blockSize; ++i)
		{
			left[i] = outs[0][i] + outs[2][i];
			right[i] = outs[1][i] + outs[3][i];
		}

		wav.write(left.data(), right.data(), blockSize);
	}
}

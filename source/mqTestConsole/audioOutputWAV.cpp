#include "audioOutputWAV.h"

#include <array>

constexpr uint32_t g_blockSize = 64;

const std::string g_filename = "mq_output_" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()) + ".wav";

AudioOutputWAV::AudioOutputWAV(ProcessCallback _callback): AudioOutput(std::move(_callback)), wavWriter(g_filename, 44100)
{
	m_stereoOutput.resize(g_blockSize<<1);
}

void AudioOutputWAV::process()
{
	while(true)
	{
		const mqLib::TAudioOutputs* outputs = nullptr;
		m_processCallback(g_blockSize, outputs);

		if(!outputs)
		{
			std::this_thread::yield();
			continue;
		}

		const auto& outs = *outputs;

		for(size_t i=0; i<g_blockSize; ++i)
		{
			m_stereoOutput[i<<1] = outs[0][i];
			m_stereoOutput[(i<<1) + 1] = outs[1][i];

			if(silence && (outs[0][i] || outs[1][i]))
				silence = false;
		}

		if(!silence)
		{
			// continously write to disk if not silent anymore
			wavWriter.append([&](auto& _dst)
			{
				_dst.reserve(_dst.size() + m_stereoOutput.size());
				for (auto& d : m_stereoOutput)
					_dst.push_back(d);
			}
			);
		}
	}
}

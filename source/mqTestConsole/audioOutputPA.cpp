#include "audioOutputPA.h"

#include <thread>

#include "portaudio.h"
#include "dsp56kEmu/audio.h"
#include "dsp56kEmu/logging.h"

constexpr uint32_t g_blocksize = 256;

static int ourPortAudioCallback(const void*/* inputBuffer*/, void *outputBuffer,
                                unsigned long framesPerBuffer,
                                const PaStreamCallbackTimeInfo*/* timeInfo*/,
                                PaStreamCallbackFlags/* statusFlags*/,
                                void *userData)
{
	const auto aa = static_cast<AudioOutputPA*>(userData);
	aa->portAudioCallback(outputBuffer, framesPerBuffer);
	return 0;
}

AudioOutputPA::AudioOutputPA(ProcessCallback _callback): AudioOutput(std::move(_callback))
{
	PaError err = Pa_Initialize();

	if (err != paNoError)
	{
		LOG("Failed to initialize PortAudio");
		return;
	}

	err = Pa_OpenDefaultStream(&m_stream, 0, 2, paFloat32, 44100, g_blocksize, ourPortAudioCallback, this);
	if (err != paNoError)
		LOG("Failed to open default stream");

	err = Pa_StartStream(m_stream);
	if (err != paNoError)
		LOG("Failed to start stream");
}

void AudioOutputPA::portAudioCallback(void* _dst, uint32_t _frames) const
{
	auto out = static_cast<float*>(_dst);

	while(_frames > 0)
	{
		auto f = std::min(static_cast<uint32_t>(64), _frames);
		_frames -= f;

		const mqLib::TAudioOutputs* outputs = nullptr;
		m_processCallback(f, outputs);

		if(!outputs)
		{
			while(f > 0)
			{
				*out++ = 0.0f;
				*out++ = 0.0f;
				--f;
			}
			continue;
		}

		auto outs = *outputs;

		auto* outA = &outs[0].front();
		auto* outB = &outs[1].front();

		while(f > 0)
		{
			*out++ = dsp56k::dsp2sample<float>(*outA++);
			*out++ = dsp56k::dsp2sample<float>(*outB++);
			--f;
		}
	}
}

void AudioOutputPA::process()
{
	while(true)
	{
		std::this_thread::sleep_for(std::chrono::seconds(1));
	}
}

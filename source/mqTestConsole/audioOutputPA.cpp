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
	return aa->portAudioCallback(outputBuffer, framesPerBuffer);
}

AudioOutputPA::AudioOutputPA(const ProcessCallback& _callback, const std::string& _deviceName) : AudioOutput(_callback), Device(_deviceName)
{
	PaError err = Pa_Initialize();

	if (err != paNoError)
	{
		LOG("Failed to initialize PortAudio");
		return;
	}

	if(!Device::openDevice())
		return;

	err = Pa_StartStream(m_stream);
	if (err != paNoError)
		LOG("Failed to start stream");
}

AudioOutputPA::~AudioOutputPA()
{
	m_exit = true;

	while(!m_callbackExited)
		std::this_thread::sleep_for(std::chrono::milliseconds(10));

	Pa_StopStream(m_stream);
	Pa_CloseStream(m_stream);
	m_stream = nullptr;
	m_deviceId = -1;
}

bool AudioOutputPA::openDevice(const int _devId)
{
	PaStreamParameters p{};

	p.channelCount = 2;
	p.device = _devId;
	p.sampleFormat = paFloat32;
	p.suggestedLatency = Pa_GetDeviceInfo(_devId)->defaultHighOutputLatency;
//	const auto err = Pa_OpenDefaultStream(&m_stream, 0, 2, paFloat32, 44100, g_blocksize, ourPortAudioCallback, this);
	const auto err = Pa_OpenStream(&m_stream, nullptr, &p, 44100, g_blocksize, paNoFlag, ourPortAudioCallback, this);

	if(err != paNoError)
		LOG("Failed to open audio device named " << m_deviceName);
	return err == paNoError;
}

int AudioOutputPA::getDefaultDeviceId() const
{
	return Pa_GetDefaultOutputDevice();
}

int AudioOutputPA::portAudioCallback(void* _dst, uint32_t _frames)
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

	if(m_exit)
	{
		m_callbackExited = true;
		return paAbort;
	}
	return paContinue;
}

void AudioOutputPA::process()
{
	// WDKMS platform may behave weird, it gets a timeout and then ends the audio thread and the stream is no longer active. Restart it
	if(!Pa_IsStreamActive(m_stream))
	{
		LOG("Audio Stream inactive, restarting");
		Pa_AbortStream(m_stream);
		Pa_StartStream(m_stream);
	}
}

int AudioOutputPA::deviceIdFromName(const std::string& _devName) const
{
	const auto count = Pa_GetDeviceCount();

	for(int i=0; i<count; ++i)
	{
		const auto name = deviceNameFromId(i);

		if(name.empty())
			continue;

		if(name == _devName)
			return i;
	}
	return -1;
}

std::string AudioOutputPA::deviceNameFromId(const int _devId) const
{
	return getDeviceNameFromId(_devId);
}

std::string AudioOutputPA::getDeviceNameFromId(int _devId)
{
	const auto* di = Pa_GetDeviceInfo(_devId);
	if(!di)
		return {};
	const auto* api = Pa_GetHostApiInfo(di->hostApi);
	if(!api)
		return di->name;
	return std::string("[") + api->name + "] " + di->name;
}

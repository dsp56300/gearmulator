#include "fakeAudioDevice.h"

Array<double> FakeAudioIODevice::getAvailableSampleRates()
{
	return {44100.0, 48000.0};
}

Array<int> FakeAudioIODevice::getAvailableBufferSizes()
{
	return {64, 128, 256, 512, 1024};
}

int FakeAudioIODevice::getDefaultBufferSize()
{
	return 512;
}

String FakeAudioIODevice::open(const uint32_t& _numInputChannels, const uint32_t& _numOutputChannels, double _sampleRate, int _bufferSizeSamples)
{
	m_numInputChannels = _numInputChannels;
	m_numOutputChannels = _numOutputChannels;
	return open(BigInteger((1<<_numInputChannels) - 1), BigInteger(((1<<_numOutputChannels)-1)), _sampleRate, _bufferSizeSamples);
}

String FakeAudioIODevice::open(const BigInteger& _inputChannels, const BigInteger& _outputChannels, const double _sampleRate, const int _bufferSizeSamples)
{
	m_activeInputChannels = _inputChannels;
	m_activeOutputChannels = _outputChannels;

	m_sampleRate = _sampleRate;
	m_bufferSizeSamples = _bufferSizeSamples;
	m_isOpenFlag = true;
	return {};
}

void FakeAudioIODevice::start(AudioIODeviceCallback* _callback)
{
	if (!m_isOpenFlag)
		return;

	m_callback = _callback;
	m_isPlayingFlag = true;
	if (!m_callback)
		return;

	m_callback->audioDeviceAboutToStart(this);

	m_buffer = AudioBuffer<float>(static_cast<int>(std::max(m_numInputChannels, m_numOutputChannels)), m_bufferSizeSamples);
}

void FakeAudioIODevice::stop()
{
	m_isPlayingFlag = false;
	if (m_callback)
		m_callback->audioDeviceStopped();
}

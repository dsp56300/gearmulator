#pragma once

#include "JuceHeader.h"

class FakeAudioIODevice : public AudioIODevice
{
public:
    FakeAudioIODevice()
        : AudioIODevice("Fake Audio Device", "Fake Type"),
          m_sampleRate(44100.0),
          m_bufferSizeSamples(512),
          m_isOpenFlag(false),
          m_isPlayingFlag(false)
    {
    }

    StringArray getOutputChannelNames() override { return {"Output"}; }
    StringArray getInputChannelNames() override { return {"Input"}; }

    std::optional<BigInteger> getDefaultOutputChannels() const override { return BigInteger(2); }
    std::optional<BigInteger> getDefaultInputChannels() const override { return BigInteger(2); }

    Array<double> getAvailableSampleRates() override;
    Array<int> getAvailableBufferSizes() override;

    int getDefaultBufferSize() override;

    String open(const uint32_t& _numInputChannels,
                const uint32_t& _numOutputChannels,
                double _sampleRate,
                int _bufferSizeSamples);

	void start(AudioIODeviceCallback* _callback) override;

private:
    String open(const BigInteger& _inputChannels,
                const BigInteger& _outputChannels,
                double _sampleRate,
                int _bufferSizeSamples) override;

    void close() override { m_isOpenFlag = false; }
    bool isOpen() override { return m_isOpenFlag; }

    void stop() override;

    bool isPlaying() override { return m_isPlayingFlag; }
    String getLastError() override { return {}; }

    int getCurrentBufferSizeSamples() override { return m_bufferSizeSamples; }
    double getCurrentSampleRate() override { return m_sampleRate; }
    int getCurrentBitDepth() override { return 32; }

    BigInteger getActiveInputChannels() const override { return m_activeInputChannels; }
    BigInteger getActiveOutputChannels() const override { return m_activeOutputChannels; }

    int getOutputLatencyInSamples() override { return 0; }
    int getInputLatencyInSamples() override { return 0; }

    bool hasControlPanel() const override { return false; }
    bool showControlPanel() override { return false; }
    bool setAudioPreprocessingEnabled(bool) override { return false; }
    int getXRunCount() const noexcept override { return 0; }

public:
	void prepareProcess();

    void processAudio()
    {
		m_callback->audioDeviceIOCallbackWithContext(m_buffer.getArrayOfReadPointers(),
		                                             m_numInputChannels ? static_cast<int>(m_numInputChannels) : 2,
		                                             m_buffer.getArrayOfWritePointers(),
		                                             static_cast<int>(m_numOutputChannels),
		                                             m_buffer.getNumSamples(),
		                                             AudioIODeviceCallbackContext());
    }

private:
    double m_sampleRate;
    int m_bufferSizeSamples;
    bool m_isOpenFlag;
    bool m_isPlayingFlag;
    AudioIODeviceCallback* m_callback = nullptr;
	uint32_t m_numInputChannels = 2;
	uint32_t m_numOutputChannels = 2;
    BigInteger m_activeInputChannels{0b11};
    BigInteger m_activeOutputChannels{0b11};
	AudioBuffer<float> m_buffer;
};
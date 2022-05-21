#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "dsp56kEmu/types.h"

#include "../synthLib/audioTypes.h"

#include "../virusLib/midiOutParser.h"

namespace virusLib
{
	class DspSingle;
}

class AudioProcessor
{
public:
	AudioProcessor(uint32_t _samplerate, std::string _outputFilename, bool _terminateOnSilence, uint32_t _maxSamplecount, virusLib::DspSingle* _dsp1, virusLib::DspSingle* _dsp2);
	~AudioProcessor();

	void processBlock(uint32_t _blockSize);

	bool finished() const { return m_finished; }

private:
	void threadWriteFunc();

	// constant data
	const uint32_t m_samplerate;
	const std::string m_outputFilname;
	const bool m_terminateOnSilence;
	const uint32_t m_maxSampleCount;
	virusLib::DspSingle* const m_dsp1;
	virusLib::DspSingle* const m_dsp2;

	// runtime data
	synthLib::TAudioInputsInt m_inputs{};
	synthLib::TAudioOutputsInt m_outputs{};

	std::vector<std::vector<dsp56k::TWord>> m_outputBuffers;
	std::vector<std::vector<dsp56k::TWord>> m_inputBuffers;
	std::vector<dsp56k::TWord> m_stereoOutput;
	virusLib::MidiOutParser m_midiOut;

	uint32_t m_processedSampleCount = 0;
	uint32_t m_silenceDuration = 0;
	bool m_finished = false;
	std::unique_ptr<std::thread> m_threadWrite;
	std::mutex m_writeMutex;
};

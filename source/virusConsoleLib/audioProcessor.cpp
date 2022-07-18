#include "audioProcessor.h"

#include <vector>

#include "esaiListenerToFile.h"
#include "dsp56kEmu/types.h"

#include "../synthLib/wavWriter.h"

#include "../virusLib/dspSingle.h"

AudioProcessor::AudioProcessor(uint32_t _samplerate, std::string _outputFilename, bool _terminateOnSilence, uint32_t _maxSamplecount, virusLib::DspSingle* _dsp1, virusLib::DspSingle* _dsp2)
: m_samplerate(_samplerate)
, m_outputFilname(std::move(_outputFilename))
, m_terminateOnSilence(_terminateOnSilence)
, m_maxSampleCount(_maxSamplecount)
, m_dsp1(_dsp1)
, m_dsp2(_dsp2)
, m_writer(_outputFilename, _samplerate, _terminateOnSilence)
{
	m_outputBuffers.resize(2);
	m_inputBuffers.resize(2);
}

AudioProcessor::~AudioProcessor() = default;

void AudioProcessor::processBlock(const uint32_t _blockSize)
{
	if(m_outputBuffers[0].size() < _blockSize)
	{
		for(size_t i=0; i<m_outputBuffers.size(); ++i)
		{
			m_outputBuffers[i].resize(_blockSize);
			m_inputBuffers[i].resize(_blockSize);

			m_inputs[i] = &m_inputBuffers[i][0];
			m_outputs[i] = &m_outputBuffers[i][0];
		}

		m_stereoOutput.reserve(m_outputBuffers.size() * 2);
	}

	const bool terminateOnSilence = m_terminateOnSilence;

	auto sampleCount = static_cast<uint32_t>(m_inputBuffers[0].size());

	if(terminateOnSilence && m_writer.getSilenceDuration() >= m_samplerate * 5)
	{
		m_writer.setFinished();
		return;
	}

	if(m_maxSampleCount && m_processedSampleCount >= m_maxSampleCount)
	{
		m_writer.setFinished();
		return;
	}

	if(m_maxSampleCount && m_maxSampleCount - m_processedSampleCount < sampleCount)
	{
		sampleCount = m_maxSampleCount - m_processedSampleCount;
	}

	// reduce thread contention by waiting for the DSP to do some work first.
	// If we don't, the ESAI writeTX function will lock/unlock a mutex to inform the waiting thread (us)
	// that there is new audio data available. This costs more than 5% of performance
/*	const auto& esai = m_dsp1->getPeriphX().getEsai();

	while(esai.getAudioInputs().size() > (_blockSize>>1))
		std::this_thread::yield();
*/
	m_dsp1->processAudio(m_inputs, m_outputs, sampleCount, _blockSize);

	m_processedSampleCount += sampleCount;

	{
		m_writer.append([&](std::vector<dsp56k::TWord>& _dst)
		{
			_dst.reserve(m_stereoOutput.size() + sampleCount * 2);

			for(size_t iSrc=0; iSrc<sampleCount; ++iSrc)
			{
				_dst.push_back(m_outputs[0][iSrc]);
				_dst.push_back(m_outputs[1][iSrc]);
			}
		});
	}

	while(m_dsp1->getPeriphX().getHDI08().hasTX())
	{
		const auto word = m_dsp1->getPeriphX().getHDI08().readTX();
		m_midiOut.append(word);
	}

	while(m_dsp2 && m_dsp2->getPeriphX().getHDI08().hasTX())
		m_dsp2->getPeriphX().getHDI08().readTX();

	if(m_maxSampleCount && m_processedSampleCount >= m_maxSampleCount)
		m_writer.setFinished();
}

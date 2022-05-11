#include "audioProcessor.h"

#include <mutex>
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
{
	m_outputBuffers.resize(2);
	m_inputBuffers.resize(2);

	m_threadWrite.reset(new std::thread([this]()
	{
		threadWriteFunc();
	}));
}

AudioProcessor::~AudioProcessor()
{
	m_threadWrite->join();
	m_threadWrite.reset();
}

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

	if(terminateOnSilence && m_silenceDuration >= m_samplerate * 5)
	{
		m_finished = true;
		return;
	}

	if(m_maxSampleCount && m_processedSampleCount >= m_maxSampleCount)
	{
		m_finished = true;
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
		std::lock_guard lock(m_writeMutex);

		m_stereoOutput.reserve(m_stereoOutput.size() + sampleCount * 2);

		for(size_t iSrc=0; iSrc<sampleCount; ++iSrc)
		{
			m_stereoOutput.push_back(m_outputs[0][iSrc]);
			m_stereoOutput.push_back(m_outputs[1][iSrc]);
		}
	}

	while(m_dsp1->getPeriphX().getHDI08().hasTX())
	{
		const auto word = m_dsp1->getPeriphX().getHDI08().readTX();
		m_midiOut.append(word);
	}

	while(m_dsp2 && m_dsp2->getPeriphX().getHDI08().hasTX())
		m_dsp2->getPeriphX().getHDI08().readTX();
}

void AudioProcessor::threadWriteFunc()
{
	synthLib::WavWriter writer;

	std::vector<dsp56k::TWord> m_wordBuffer;
	m_wordBuffer.reserve(m_outputBuffers.size() * m_outputBuffers[0].size());
	std::vector<uint8_t> m_byteBuffer;
	m_byteBuffer.reserve(m_wordBuffer.capacity() * 3);

	while(!m_finished)
	{
		{
			std::lock_guard lock(m_writeMutex);
			std::swap(m_wordBuffer, m_stereoOutput);
		}

		if(!m_wordBuffer.empty())
		{
			for (const dsp56k::TWord w : m_wordBuffer)
				EsaiListenerToFile::writeWord(m_byteBuffer, w);

			if(m_terminateOnSilence)
			{
				bool isSilence = true;

				for (const dsp56k::TWord w : m_wordBuffer)
				{
					constexpr dsp56k::TWord silenceThreshold = 0x1ff;
					const bool silence = w < silenceThreshold || w >= (0xffffff - silenceThreshold);
					if(!silence)
					{
						isSilence = false;
						break;
					}
				}

				if(isSilence)
					m_silenceDuration += static_cast<uint32_t>(m_wordBuffer.size() >> 1);
				else
					m_silenceDuration = 0;
			}

			m_wordBuffer.clear();

			if(writer.write(m_outputFilname, 24, false, 2, static_cast<int>(m_samplerate), &m_byteBuffer[0], m_byteBuffer.size()))
				m_byteBuffer.clear();

		}

		std::this_thread::sleep_for(std::chrono::milliseconds(500));
	}
}

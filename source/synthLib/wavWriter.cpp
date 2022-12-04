#include "wavWriter.h"

#include "../dsp56300/source/dsp56kEmu/logging.h"

#include <map>
#include <cassert>
#include <chrono>
#include <mutex>
#include <thread>

#include "dsp56kEmu/types.h"

namespace synthLib
{
	bool WavWriter::write(const std::string & _filename, const int _bitsPerSample, const bool _isFloat, const int _channelCount, const int _samplerate, const void* _data, const size_t _dataSize)
	{
		FILE* handle = fopen(_filename.c_str(), m_existingDataSize > 0 ? "rb+" : "wb");

		if (!handle)
		{
			LOG("Failed to open file for writing: " << _filename);
			return false;
		}

		fseek(handle, 0, SEEK_END);
		const auto currentSize = ftell(handle);
		const bool append = currentSize > 0;

		if(append)
		{
			fwrite(_data, 1, _dataSize, handle);
			fseek(handle, 0, SEEK_SET);
			const auto pos = ftell(handle);
			assert(pos == 0);
		}

		m_existingDataSize += _dataSize;

		SWaveFormatHeader header{};
		SWaveFormatChunkInfo chunkInfo{};
		SWaveFormatChunkFormat fmt{};

		header.str_riff[0] = 'R';
		header.str_riff[1] = 'I';
		header.str_riff[2] = 'F';
		header.str_riff[3] = 'F';

		header.str_wave[0] = 'W';
		header.str_wave[1] = 'A';
		header.str_wave[2] = 'V';
		header.str_wave[3] = 'E';

		const size_t dataSize = m_existingDataSize;

		header.file_size = static_cast<uint32_t>(sizeof(SWaveFormatHeader) +
			sizeof(SWaveFormatChunkInfo) +
			sizeof(SWaveFormatChunkFormat) +
			sizeof(SWaveFormatChunkInfo) +
			dataSize - 8);

		fwrite(&header, 1, sizeof(header), handle);
		const auto pos = ftell(handle);
		assert(pos == sizeof(header));

		// write format
		chunkInfo.chunkName[0] = 'f';
		chunkInfo.chunkName[1] = 'm';
		chunkInfo.chunkName[2] = 't';
		chunkInfo.chunkName[3] = ' ';

		chunkInfo.chunkSize = sizeof(SWaveFormatChunkFormat);

		fwrite(&chunkInfo, 1, sizeof(chunkInfo), handle);

		const auto bytesPerSample = _bitsPerSample >> 3;
		const auto bytesPerFrame = bytesPerSample * _channelCount;

		fmt.bits_per_sample = _bitsPerSample;
		fmt.block_alignment = bytesPerFrame;
		fmt.bytes_per_sec = _samplerate * bytesPerFrame;
		fmt.num_channels = static_cast<uint16_t>(_channelCount);
		fmt.sample_rate = static_cast<uint32_t>(_samplerate);
		fmt.wave_type = _isFloat ? eFormat_IEEE_FLOAT : eFormat_PCM;

		fwrite(&fmt, 1, sizeof(fmt), handle);

		// write data
		chunkInfo.chunkName[0] = 'd';
		chunkInfo.chunkName[1] = 'a';
		chunkInfo.chunkName[2] = 't';
		chunkInfo.chunkName[3] = 'a';

		chunkInfo.chunkSize = static_cast<unsigned int>(m_existingDataSize);

		fwrite(&chunkInfo, 1, sizeof(chunkInfo), handle);

		if(!append)
			fwrite(_data, 1, m_existingDataSize, handle);

		fclose(handle);

		return true;
	}

	void WavWriter::writeWord(std::vector<uint8_t>& _dst, dsp56k::TWord _word)
	{
		const auto d = reinterpret_cast<const uint8_t*>(&_word);
		_dst.push_back(d[0]);
		_dst.push_back(d[1]);
		_dst.push_back(d[2]);
	}

	AsyncWriter::AsyncWriter(std::string _filename, uint32_t _samplerate, bool _measureSilence)
	: m_filename(std::move(_filename))
	, m_samplerate(_samplerate)
	, m_measureSilence(_measureSilence)
	{
		m_thread.reset(new std::thread([&]()
		{
			threadWriteFunc();
		}));
	}

	AsyncWriter::~AsyncWriter()
	{
		m_finished = true;

		if(m_thread)
		{
			m_thread->join();
			m_thread.reset();
		}
	}

	void AsyncWriter::append(const std::function<void(std::vector<dsp56k::TWord>&)>& _func)
	{
		std::lock_guard lock(m_writeMutex);
		_func(m_stereoOutput);
	}

	void AsyncWriter::threadWriteFunc()
	{
		synthLib::WavWriter writer;

		std::vector<dsp56k::TWord> m_wordBuffer;
		std::vector<uint8_t> m_byteBuffer;
		m_byteBuffer.reserve(m_wordBuffer.capacity() * 3);

		bool foundNonSilence = false;

		while(true)
		{
			{
				std::lock_guard lock(m_writeMutex);
				std::swap(m_wordBuffer, m_stereoOutput);
			}

			if(m_wordBuffer.empty() && m_byteBuffer.empty() && m_finished)
				break;

			if(!m_wordBuffer.empty())
			{
				for (const dsp56k::TWord w : m_wordBuffer)
					WavWriter::writeWord(m_byteBuffer, w);

				if(m_measureSilence)
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

					if(foundNonSilence && isSilence)
					{
						m_silenceDuration += static_cast<uint32_t>(m_wordBuffer.size() >> 1);
					}
					else if(!isSilence)
					{
						m_silenceDuration = 0;
						foundNonSilence = true;
					}
				}

				m_wordBuffer.clear();
			}

			if(!m_byteBuffer.empty())
			{
				if(writer.write(m_filename, 24, false, 2, static_cast<int>(m_samplerate), &m_byteBuffer[0], m_byteBuffer.size()))
				{
					m_byteBuffer.clear();
				}
				else if(m_finished)
				{
					LOG("Unable to write data to file " << m_filename << " but termination requested, file is missing data");
					break;
				}
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(500));
		}
	}
}

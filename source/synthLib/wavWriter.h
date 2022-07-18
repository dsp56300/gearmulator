#pragma once

#include <functional>
#include <memory>
#include <mutex>

#include "wavTypes.h"

#include <vector>
#include <string>
#include <thread>

#include "dsp56kEmu/types.h"

namespace synthLib
{
	class WavWriter
	{
	public:
		bool write(const std::string& _filename, int _bitsPerSample, bool isFloat, int _channelCount, int _samplerate, const void* _data, size_t _dataSize);

		template<typename T>
		bool write(const std::string& _filename, int _bitsPerSample, bool isFloat, int _channelCount, int _samplerate, const std::vector<T>& _data)
		{
			return write(_filename, _bitsPerSample, isFloat, _channelCount, _samplerate, &_data[0], sizeof(T) * _data.size());
		}

		static void writeWord(std::vector<uint8_t>& _dst, dsp56k::TWord _word);

	private:
		size_t m_existingDataSize = 0;
	};

	class AsyncWriter
	{
	public:
		AsyncWriter(std::string _filename, uint32_t _samplerate, bool _measureSilence = false);
		~AsyncWriter();

		void append(const std::function<void(std::vector<dsp56k::TWord>&)>& _func);

		void setFinished()
		{
			m_finished = true;
		}

		bool isFinished() const
		{
			return m_finished;
		}

		uint32_t getSilenceDuration() const
		{
			return m_silenceDuration;
		}

	private:
		void threadWriteFunc();

		const std::string m_filename;
		const uint32_t m_samplerate;
		const bool m_measureSilence;

		bool m_finished = false;
		std::unique_ptr<std::thread> m_thread;
		uint32_t m_silenceDuration = 0;
		std::mutex m_writeMutex;
		std::vector<dsp56k::TWord> m_stereoOutput;
	};
};

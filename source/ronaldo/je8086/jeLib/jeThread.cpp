#include "jeThread.h"

#include "je8086.h"

#include "dsp56kBase/threadtools.h"

namespace jeLib
{
	JeThread::JeThread(Je8086& _je8086) : m_je8086(_je8086)
	{
		m_thread.reset(new std::thread([this]() { threadFunc(); }));
	}

	JeThread::~JeThread()
	{
		m_exit = true;
		m_pendingJobs.push_back(ProcessJob());
		m_thread->join();
		m_thread.reset();
	}

	void JeThread::processSamples(const uint32_t _count, uint32_t _requiredLatency, std::vector<synthLib::SMidiEvent>& _midiIn, std::vector<synthLib::SMidiEvent>& _midiOut)
	{
		ProcessJob job;
		{
			std::lock_guard lock(m_mutex);
			if (!m_jobPool.empty())
			{
				job = std::move(m_jobPool.back());
				m_jobPool.pop_back();
			}
		}

		for (auto& e : _midiIn)
		{
			const auto offset = e.offset + _requiredLatency + m_inSampleOffset;
			job.midiEvents.emplace_back(offset, std::move(e));
		}

		_midiIn.clear();

		job.samplesToProcess = 0;

		m_inSampleOffset += _count;

		// add latency by allowing the DSP to process more samples
		while (_requiredLatency > m_currentLatency)
		{
			++job.samplesToProcess;
			++m_currentLatency;
		}

		for (size_t i=0; i<_count; ++i)
		{
			// remove latency by omitting new processing requests
			if (m_currentLatency > _requiredLatency)
				--m_currentLatency;
			else
				++job.samplesToProcess;
		}

		if (m_currentLatency == 0 && m_pendingJobs.empty())
		{
			processJob(job);
			m_jobPool.push_back(std::move(job));
		}
		else
		{
			m_pendingJobs.push_back(std::move(job));
		}

		{
			std::lock_guard lock(m_mutex);

			if (_midiOut.empty())
			{
				std::swap(_midiOut, m_midiOutput);
			}
			else
			{
				_midiOut.insert(_midiOut.end(), m_midiOutput.begin(), m_midiOutput.end());
				m_midiOutput.clear();
			}
		}
	}

	void JeThread::threadFunc()
	{
		dsp56k::ThreadTools::setCurrentThreadName("JE8086");
		dsp56k::ThreadTools::setCurrentThreadPriority(dsp56k::ThreadPriority::Highest);

		while (!m_exit)
		{
			auto job = m_pendingJobs.pop_front();

			if (m_exit)
				break;

			processJob(job);

			std::lock_guard lock(m_mutex);
			m_jobPool.push_back(std::move(job));
		}
	}

	void JeThread::processJob(ProcessJob& _job)
	{
		if (m_tempMidiIn.empty())
		{
			std::swap(m_tempMidiIn, _job.midiEvents);
		}
		else
		{
			m_tempMidiIn.insert(m_tempMidiIn.end(), _job.midiEvents.begin(), _job.midiEvents.end());
			_job.midiEvents.clear();
		}

		for (uint32_t i=0; i<_job.samplesToProcess; ++i)
		{
			for(auto it = m_tempMidiIn.begin(); it != m_tempMidiIn.end();)
			{
				auto& e = *it;

				if (e.first <= m_processedSampleOffset)
				{
					m_je8086.addMidiEvent(e.second);
					it = m_tempMidiIn.erase(it);
				}
				else
				{
					++it;
				}
			}

			while (m_je8086.getSampleBuffer().empty())
				m_je8086.step();

			m_audioOut.push_back(m_je8086.getSampleBuffer().front());
			m_je8086.clearSampleBuffer();

			++m_processedSampleOffset;

			m_je8086.readMidiOut(m_tempMidiOut);

			if (!m_tempMidiOut.empty())
			{
				std::lock_guard lock(m_mutex);
				if (m_midiOutput.empty())
				{
					std::swap(m_midiOutput, m_tempMidiOut);
				}
				else
				{
					m_midiOutput.insert(m_midiOutput.end(), m_tempMidiOut.begin(), m_tempMidiOut.end());
					m_tempMidiOut.clear();
				}
			}
		}

		_job.samplesToProcess = 0;
	}
}

#include "jeThread.h"

#include "je8086.h"
#include "dsp56kEmu/threadtools.h"

namespace jeLib
{
	JeThread::JeThread(Je8086& _je8086) : m_je8086(_je8086)
	{
		m_thread.reset(new std::thread([this]() { threadFunc(); }));
	}

	JeThread::~JeThread()
	{
		m_exit = true;
		m_inputSem.notify();
		m_thread->join();
		m_thread.reset();
	}

	void JeThread::processSamples(const uint32_t _count, uint32_t _requiredLatency, const std::vector<synthLib::SMidiEvent>& _midiIn, std::vector<synthLib::SMidiEvent>& _midiOut)
	{
		{
			std::lock_guard lock(m_mutex);

			for (const auto& e : _midiIn)
			{
				m_midiInput.emplace_back(e.offset + _requiredLatency + m_inSampleOffset, e);
			}
		}

		m_inSampleOffset += _count;

		// add latency by allowing the DSP to process more samples
		while (_requiredLatency > m_currentLatency)
		{
			m_inputSem.notify();
			++m_currentLatency;
		}

		for (size_t i=0; i<_count; ++i)
		{
			// remove latency by omitting new processing requests
			if (m_currentLatency > _requiredLatency)
				--m_currentLatency;
			else
				m_inputSem.notify();
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
			m_inputSem.wait();
			if (m_exit)
				break;

			processSample();
		}
	}

	void JeThread::processSample()
	{
		{
			std::lock_guard lock(m_mutex);

			for(auto it = m_midiInput.begin(); it != m_midiInput.end();)
			{
				auto& e = m_midiInput.front();

				if (e.first <= m_processedSampleOffset)
				{
					m_je8086.addMidiEvent(e.second);
					it = m_midiInput.erase(it);
				}
				else
				{
					++it;
				}
			}
		}

		while (m_je8086.getSampleBuffer().empty())
			m_je8086.step();

		m_audioOut.push_back(m_je8086.getSampleBuffer().front());
		m_je8086.clearSampleBuffer();

		++m_processedSampleOffset;

		std::lock_guard lock(m_mutex);
		m_je8086.readMidiOut(m_midiOutput);
	}
}

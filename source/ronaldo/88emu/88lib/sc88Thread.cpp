#include "sc88Thread.h"

#include "dsp56kBase/threadtools.h"

#include <algorithm>
#include <cstddef>

namespace emu88Lib
{
	Sc88Thread::Sc88Thread(Render _render, SendMidi _sendMidi, ReadMidi _readMidi, BeforeJob _beforeJob)
		: m_render(std::move(_render))
		, m_sendMidi(std::move(_sendMidi))
		, m_readMidi(std::move(_readMidi))
		, m_beforeJob(std::move(_beforeJob))
	{
		m_thread = std::make_unique<std::thread>([this] { threadFunc(); });
	}

	Sc88Thread::~Sc88Thread()
	{
		m_exit.store(true, std::memory_order_release);
		// Wake an idle consumer, but never block trying to append a sentinel to a
		// saturated queue. A full queue already guarantees that the consumer is
		// runnable; after its current callback returns it observes m_exit and joins.
		(void)m_pendingJobs.try_push_back(ProcessJob{});
		m_thread->join();
	}

	void Sc88Thread::processSamples(const uint32_t _count, const uint32_t _requiredLatency,
	                                std::vector<synthLib::SMidiEvent>& _midiIn,
	                                std::vector<synthLib::SMidiEvent>& _midiOut)
	{
		ProcessJob job;
		{
			std::lock_guard lock(m_outputMutex);
			if(!m_jobPool.empty())
			{
				job = std::move(m_jobPool.back());
				m_jobPool.pop_back();
			}
		}

		for(auto& event : _midiIn)
		{
			if(event.type == synthLib::MidiEventType::TransportDiscontinuity)
			{
				auto generation = m_outputGeneration.load(std::memory_order_relaxed);
				while(generation < event.transportGeneration &&
				      !m_outputGeneration.compare_exchange_weak(generation, event.transportGeneration,
				          std::memory_order_release, std::memory_order_relaxed))
				{
				}
			}
			job.midiEvents.emplace_back(event.offset + _requiredLatency + m_inSampleOffset, std::move(event));
		}
		_midiIn.clear();

		job.samplesToProcess = 0;
		m_inSampleOffset += _count;
		while(_requiredLatency > m_currentLatency)
		{
			++job.samplesToProcess;
			++m_currentLatency;
		}
		for(uint32_t i = 0; i < _count; ++i)
		{
			if(m_currentLatency > _requiredLatency)
				--m_currentLatency;
			else
				++job.samplesToProcess;
		}

		if(!m_pendingJobs.try_push_back(std::move(job)))
		{
			m_droppedSamples.fetch_add(job.samplesToProcess, std::memory_order_relaxed);
			m_recoveryRequested.store(true, std::memory_order_release);
		}

		std::lock_guard lock(m_outputMutex);
		if(_midiOut.empty())
			std::swap(_midiOut, m_midiOutput);
		else
		{
			_midiOut.insert(_midiOut.end(), m_midiOutput.begin(), m_midiOutput.end());
			m_midiOutput.clear();
		}
	}

	bool Sc88Thread::popSample(SampleFrame& _sample)
	{
		OutputFrame output;
		const auto requiredGeneration = m_outputGeneration.load(std::memory_order_acquire);
		while(m_audioOut.try_pop_front(output))
		{
			if(output.transportGeneration < requiredGeneration)
				continue;
			_sample = output.sample;
			return true;
		}
		return false;
	}

	void Sc88Thread::threadFunc()
	{
		dsp56k::ThreadTools::setCurrentThreadName("SC-88 family");
		dsp56k::ThreadTools::setCurrentThreadPriority(dsp56k::ThreadPriority::Highest);
		while(!m_exit.load(std::memory_order_acquire))
		{
			auto job = m_pendingJobs.pop_front();
			if(m_exit.load(std::memory_order_acquire))
				break;
			processJob(job);
			std::lock_guard lock(m_outputMutex);
			m_jobPool.emplace_back(std::move(job));
		}
	}

	void Sc88Thread::processJob(ProcessJob& _job)
	{
		const bool recoverState = m_recoveryRequested.exchange(false, std::memory_order_acq_rel);
		if(recoverState)
			recoverFromQueueOverflow();
		if(m_beforeJob)
			m_beforeJob();
		if(m_tempMidiIn.empty())
			std::swap(m_tempMidiIn, _job.midiEvents);
		else
		{
			m_tempMidiIn.insert(m_tempMidiIn.end(), _job.midiEvents.begin(), _job.midiEvents.end());
			_job.midiEvents.clear();
		}

		for(uint32_t i = 0; i < _job.samplesToProcess && !m_exit.load(std::memory_order_acquire); ++i)
		{
			for(size_t eventIndex = 0; eventIndex < m_tempMidiIn.size();)
			{
				if(m_tempMidiIn[eventIndex].first <= m_processedSampleOffset)
				{
					auto event = std::move(m_tempMidiIn[eventIndex].second);
					m_tempMidiIn.erase(m_tempMidiIn.begin() + static_cast<std::ptrdiff_t>(eventIndex));
					if(event.type == synthLib::MidiEventType::TransportDiscontinuity)
					{
						handleTransportDiscontinuity(event.transportGeneration);
						// Removing stale events can shift earlier entries behind the scan cursor.
						eventIndex = 0;
						continue;
					}
					if(isTransportBound(event) && event.transportGeneration < m_workerGeneration)
						continue;
					if(m_sendMidi)
						m_sendMidi(event);
					trackMidiActivity(event);
				}
				else
					++eventIndex;
			}
			auto sample = m_render();
			OutputFrame output{sample, m_workerGeneration};
			(void)m_audioOut.try_push_back(std::move(output));
			++m_processedSampleOffset;
			m_readMidi(m_tempMidiOut);
			if(!m_tempMidiOut.empty())
			{
				std::lock_guard lock(m_outputMutex);
				m_midiOutput.insert(m_midiOutput.end(), m_tempMidiOut.begin(), m_tempMidiOut.end());
				m_tempMidiOut.clear();
			}
		}
		_job.samplesToProcess = 0;
	}

	bool Sc88Thread::isTransportBound(const synthLib::SMidiEvent& _event)
	{
		return _event.sysex.empty() &&
		       (_event.source == synthLib::MidiEventSource::Host ||
		        _event.source == synthLib::MidiEventSource::Internal);
	}

	void Sc88Thread::handleTransportDiscontinuity(const uint32_t _generation)
	{
		m_workerGeneration = std::max(m_workerGeneration, _generation);
		for(auto it = m_tempMidiIn.begin(); it != m_tempMidiIn.end();)
		{
			if(isTransportBound(it->second) && it->second.transportGeneration < m_workerGeneration)
				it = m_tempMidiIn.erase(it);
			else
				++it;
		}
		silenceActiveChannels();
	}

	void Sc88Thread::recoverFromQueueOverflow()
	{
		m_processedSampleOffset += m_droppedSamples.exchange(0, std::memory_order_acq_rel);
		m_workerGeneration = std::max(m_workerGeneration, m_outputGeneration.load(std::memory_order_acquire));
		for(auto it = m_tempMidiIn.begin(); it != m_tempMidiIn.end();)
		{
			if(isTransportBound(it->second))
				it = m_tempMidiIn.erase(it);
			else
				++it;
		}
		silenceActiveChannels();
	}

	void Sc88Thread::silenceActiveChannels()
	{
		for(int port = 3; port >= 0; --port)
		{
			for(int channel = 15; channel >= 0; --channel)
			{
				const auto channelBit = static_cast<uint8_t>(port * 16 + channel);
				if((m_activeChannels & (uint64_t{1} << channelBit)) == 0)
					continue;
				synthLib::SMidiEvent event(synthLib::MidiEventSource::Internal,
					static_cast<uint8_t>(synthLib::M_CONTROLCHANGE | channel), synthLib::MC_ALLSOUNDOFF, 0);
				event.port = static_cast<uint8_t>(port);
				event.transportGeneration = m_workerGeneration;
				if(m_sendMidi)
					(void)m_sendMidi(event);
			}
		}
		m_activeChannels = 0;
	}

	void Sc88Thread::trackMidiActivity(const synthLib::SMidiEvent& _event)
	{
		if(!isTransportBound(_event) || _event.a < 0x80 || _event.a >= 0xf0)
			return;
		const auto status = _event.a & 0xf0;
		if(_event.port >= 4)
			return;
		const auto channelBit = static_cast<uint8_t>(_event.port * 16 + (_event.a & 0x0f));
		const auto channelMask = uint64_t{1} << channelBit;
		if(status == synthLib::M_NOTEON && _event.c != 0)
			m_activeChannels |= channelMask;
		else if(status == synthLib::M_CONTROLCHANGE &&
		        (_event.b == synthLib::MC_ALLSOUNDOFF || _event.b == synthLib::MC_ALLNOTESOFF))
			m_activeChannels &= ~channelMask;
	}
}

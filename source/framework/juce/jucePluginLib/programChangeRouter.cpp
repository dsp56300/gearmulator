#include "programChangeRouter.h"

#include "juce_core/juce_core.h"

namespace pluginLib
{
	namespace
	{
		// set while the handler loads a held program change: what it sends goes to the device ahead of the held events
		thread_local bool g_loadingHeldProgramChange = false;

		struct ScopedLoading
		{
			ScopedLoading() { g_loadingHeldProgramChange = true; }
			~ScopedLoading() { g_loadingHeldProgramChange = false; }
		};
	}

	void ProgramChangeRouter::setHandler(Handler* _handler)
	{
		std::unique_lock lock(m_handlerMutex);
		m_handler = _handler;
	}

	ProgramChangeRouter::Result ProgramChangeRouter::processMidiEvent(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>& _replacement)
	{
		if (g_loadingHeldProgramChange)
			return Result::Forward;

		if (m_holding)
		{
			std::lock_guard lock(m_holdMutex);

			if (m_holding)
			{
				// whether a program change among them is routed is decided when it is released, with the bank select
				// that is current then
				m_held.push_back(_ev);
				return isProgramChange(_ev) ? Result::Consumed : Result::Held;
			}
		}

		trackBankSelect(_ev);

		if (!isProgramChange(_ev))
			return Result::Forward;

		return routeProgramChange(_ev, _replacement);
	}

	void ProgramChangeRouter::processHeldEvents(const SendFunc& _sendToDevice)
	{
		{
			std::vector<synthLib::SMidiEvent> late;
			{
				std::lock_guard lock(m_holdMutex);
				late.swap(m_lateProgramChanges);
			}

			for (size_t i=0; i<late.size(); ++i)
			{
				if (!releaseHeldEvent(late[i], isSuperseded(late, i), _sendToDevice))
				{
					std::lock_guard lock(m_holdMutex);
					m_lateProgramChanges.insert(m_lateProgramChanges.begin(), late.begin() + static_cast<ptrdiff_t>(i), late.end());
					break;
				}
			}
		}

		while (true)
		{
			std::vector<synthLib::SMidiEvent> events;

			{
				std::lock_guard lock(m_holdMutex);

				if (m_held.empty())
				{
					m_holding = false;
					return;
				}

				events.swap(m_held);
			}

			for (size_t i=0; i<events.size(); ++i)
			{
				if (releaseHeldEvent(events[i], isSuperseded(events, i), _sendToDevice))
					continue;

				// not loadable yet, keep it and everything behind it held. Events that arrived meanwhile stay behind them
				std::lock_guard lock(m_holdMutex);
				m_held.insert(m_held.begin(), events.begin() + static_cast<ptrdiff_t>(i), events.end());
				return;
			}
		}
	}

	void ProgramChangeRouter::releaseStaleHold(const SendFunc& _sendToDevice)
	{
		if (!m_holding)
			return;

		std::vector<synthLib::SMidiEvent> events;

		{
			std::lock_guard lock(m_holdMutex);

			if (!m_holding || juce::Time::getMillisecondCounter() - m_holdStartMs < HoldTimeoutMs)
				return;

			events.swap(m_held);

			for (const auto& ev : events)
			{
				if (isProgramChange(ev))
					m_lateProgramChanges.push_back(ev);
			}

			m_holding = false;
		}

		for (const auto& ev : events)
		{
			if (isProgramChange(ev))
				continue;

			trackBankSelect(ev);
			_sendToDevice(ev);
		}
	}

	ProgramChangeRouter::Result ProgramChangeRouter::routeProgramChange(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>& _replacement)
	{
		std::shared_lock lock(m_handlerMutex);

		auto* handler = m_handler;

		if (!handler)
			return Result::Forward;

		const uint8_t channel = _ev.a & 0x0f;

		switch (handler->onProgramChange(channel, getMidiBankNumber(channel), _ev.b, _replacement))
		{
		case Handler::Result::PassThrough:
			return Result::Forward;
		case Handler::Result::Replaced:
			return Result::Consumed;
		case Handler::Result::Deferred:
			if (!startHold(_ev))
				return Result::Consumed;	// another program change got there first, this one is held behind it
			handler->onEventsHeld();
			return Result::Consumed;
		}
		return Result::Forward;
	}

	void ProgramChangeRouter::trackBankSelect(const synthLib::SMidiEvent& _ev)
	{
		if (!isBankSelect(_ev))
			return;

		auto& ps = m_partStates[_ev.a & 0x0f];

		if (_ev.b == synthLib::MC_BANKSELECTMSB)
			ps.bankMsb = _ev.c;
		else
			ps.bankLsb = _ev.c;
	}

	uint32_t ProgramChangeRouter::getMidiBankNumber(const uint8_t _channel) const
	{
		const auto& ps = m_partStates[_channel];
		return (static_cast<uint32_t>(ps.bankMsb.load()) << 7) | ps.bankLsb.load();
	}

	bool ProgramChangeRouter::startHold(const synthLib::SMidiEvent& _programChange)
	{
		std::lock_guard lock(m_holdMutex);

		m_held.push_back(_programChange);

		if (m_holding)
			return false;

		m_holdStartMs = juce::Time::getMillisecondCounter();
		m_holding = true;
		return true;
	}

	bool ProgramChangeRouter::releaseHeldEvent(const synthLib::SMidiEvent& _ev, const bool _superseded, const SendFunc& _sendToDevice)
	{
		if (!isProgramChange(_ev))
		{
			trackBankSelect(_ev);
			_sendToDevice(_ev);
			return true;
		}

		// a program change followed by another one for the same channel, with nothing in between that would see its
		// patch, does not need to load
		if (_superseded)
			return true;

		std::shared_lock lock(m_handlerMutex);

		auto* handler = m_handler;

		if (!handler)
		{
			_sendToDevice(_ev);
			return true;
		}

		const uint8_t channel = _ev.a & 0x0f;
		const auto bank = getMidiBankNumber(channel);

		std::vector<synthLib::SMidiEvent> replacement;

		switch (handler->onProgramChange(channel, bank, _ev.b, replacement))
		{
		case Handler::Result::PassThrough:
			_sendToDevice(_ev);
			return true;
		case Handler::Result::Replaced:
			for (const auto& e : replacement)
				_sendToDevice(e);
			return true;
		case Handler::Result::Deferred:
			{
				ScopedLoading loading;
				return handler->loadProgramChange(channel, bank, _ev.b);
			}
		}
		return true;
	}

	bool ProgramChangeRouter::isProgramChange(const synthLib::SMidiEvent& _ev)
	{
		return _ev.sysex.empty() && (_ev.a & 0xf0) == synthLib::M_PROGRAMCHANGE;
	}

	bool ProgramChangeRouter::isBankSelect(const synthLib::SMidiEvent& _ev)
	{
		return _ev.sysex.empty() && (_ev.a & 0xf0) == synthLib::M_CONTROLCHANGE && (_ev.b == synthLib::MC_BANKSELECTMSB || _ev.b == synthLib::MC_BANKSELECTLSB);
	}

	bool ProgramChangeRouter::isSuperseded(const std::vector<synthLib::SMidiEvent>& _events, const size_t _index)
	{
		const auto& pc = _events[_index];

		if (!isProgramChange(pc))
			return false;

		const auto channel = pc.a & 0x0f;

		for (size_t i = _index + 1; i < _events.size(); ++i)
		{
			const auto& ev = _events[i];

			if (isProgramChange(ev) && (ev.a & 0x0f) == channel)
				return true;

			// a bank select only chooses where the next program change loads from, it does not see the patch
			if (isBankSelect(ev) && (ev.a & 0x0f) == channel)
				continue;

			return false;
		}
		return false;
	}
}

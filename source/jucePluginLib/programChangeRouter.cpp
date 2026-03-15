#include "programChangeRouter.h"

#include "synthLib/midiTypes.h"

namespace pluginLib
{
	void ProgramChangeRouter::setHasBankCallback(HasBankFunc&& _func)
	{
		m_hasBankFunc = std::move(_func);
	}

	void ProgramChangeRouter::setOnProgramChangeQueued(NotifyFunc&& _func)
	{
		m_onProgramChangeQueued = std::move(_func);
	}

	bool ProgramChangeRouter::processMidiEvent(const synthLib::SMidiEvent& _ev)
	{
		if (!_ev.sysex.empty())
			return false;

		const uint8_t status = _ev.a & 0xf0;
		const uint8_t channel = _ev.a & 0x0f;

		if (channel >= MaxParts)
			return false;

		if (status == synthLib::M_CONTROLCHANGE)
		{
			const auto cc = _ev.b;
			const auto value = _ev.c;

			if (cc == synthLib::MC_BANKSELECTMSB)
			{
				m_partStates[channel].bankMsb = value;
				return false;	// bank select passes through, device may also need it
			}
			if (cc == synthLib::MC_BANKSELECTLSB)
			{
				m_partStates[channel].bankLsb = value;
				return false;	// bank select passes through
			}

			return false;
		}

		if (status == synthLib::M_PROGRAMCHANGE)
		{
			const auto& ps = m_partStates[channel];
			const uint32_t midiBankNumber = (static_cast<uint32_t>(ps.bankMsb) << 7) | ps.bankLsb;

			// check if this bank has a DataSource assigned (thread-safe lookup)
			if (!m_hasBankFunc || !m_hasBankFunc(midiBankNumber))
				return false;	// no datasource for this bank, pass through to device

			const uint32_t program = _ev.b;

			// queue the request for the UI thread to handle
			{
				std::lock_guard lock(m_pendingMutex);
				m_pendingRequests.push_back({channel, midiBankNumber, program});
			}

			if (m_onProgramChangeQueued)
				m_onProgramChangeQueued();

			return true;	// consumed — will be handled by patch manager on UI thread
		}

		return false;
	}

	std::vector<ProgramChangeRouter::ProgramChangeRequest> ProgramChangeRouter::getPendingRequests()
	{
		std::lock_guard lock(m_pendingMutex);
		return std::move(m_pendingRequests);
	}
}

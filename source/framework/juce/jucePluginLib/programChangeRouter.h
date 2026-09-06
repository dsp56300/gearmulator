#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

namespace synthLib
{
	struct SMidiEvent;
}

namespace pluginLib
{
	namespace patchDB
	{
		struct DataSource;
	}

	class ProgramChangeRouter
	{
	public:
		static constexpr uint32_t MaxParts = 16;

		struct ProgramChangeRequest
		{
			uint32_t part;
			uint32_t midiBankNumber;
			uint32_t program;
		};

		using HasBankFunc = std::function<bool(uint32_t _midiBankNumber)>;
		using NotifyFunc = std::function<void()>;

		void setHasBankCallback(HasBankFunc&& _func);
		void setOnProgramChangeQueued(NotifyFunc&& _func);

		// returns true if the event was consumed (program change will be handled by patch manager)
		bool processMidiEvent(const synthLib::SMidiEvent& _ev);

		// called from UI thread to drain pending program changes
		std::vector<ProgramChangeRequest> getPendingRequests();

	private:
		struct PartState
		{
			uint8_t bankMsb = 0;
			uint8_t bankLsb = 0;
		};

		std::array<PartState, MaxParts> m_partStates{};

		HasBankFunc m_hasBankFunc;
		NotifyFunc m_onProgramChangeQueued;

		std::mutex m_pendingMutex;
		std::vector<ProgramChangeRequest> m_pendingRequests;
	};
}

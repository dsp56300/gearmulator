#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <vector>

#include "synthLib/midiTypes.h"

namespace pluginLib
{
	// Program changes of a MIDI bank that has a data source assigned load a patch from the patch manager instead of
	// reaching the device. The device has to get that patch before the events that follow the program change, so a
	// program change the patch manager cannot load right away holds back every device-bound event behind it until it can.
	class ProgramChangeRouter
	{
	public:
		static constexpr uint32_t MaxParts = 16;

		// a program change that is held for longer gives up the order: the held events go to the device without it
		static constexpr uint32_t HoldTimeoutMs = 1000;

		class Handler
		{
		public:
			enum class Result
			{
				PassThrough,	// no data source for this bank, the device handles the program change itself
				Replaced,		// _events hold what the device needs to load the patch, sent in place of the program change
				Deferred		// the patch has to be loaded on the message thread, see loadProgramChange()
			};

			virtual ~Handler() = default;

			// Called on whatever thread received the program change: the audio thread, a MIDI input, the message thread
			virtual Result onProgramChange(uint32_t _part, uint32_t _midiBankNumber, uint32_t _program, std::vector<synthLib::SMidiEvent>& _events) = 0;

			// Message thread: load the patch of a program change that onProgramChange() deferred. What the patch manager
			// sends to the device meanwhile goes straight to it, ahead of the events held behind the program change.
			// Return false if it cannot be loaded yet, the events then stay held.
			virtual bool loadProgramChange(uint32_t _part, uint32_t _midiBankNumber, uint32_t _program) = 0;

			// Any thread: events are held now, call ProgramChangeRouter::processHeldEvents() on the message thread
			virtual void onEventsHeld() = 0;
		};

		enum class Result
		{
			Forward,	// the device gets the event as it is
			Held,		// the device gets the event later, behind a program change that is still loading
			Consumed	// a routed program change, the device gets _replacement instead, if anything
		};

		using SendFunc = std::function<void(const synthLib::SMidiEvent&)>;

		// waits for calls into the previous handler that run on other threads, so it can be destroyed afterwards
		void setHandler(Handler* _handler);

		// for every event that is about to be sent to the device, on whatever thread it arrives
		Result processMidiEvent(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>& _replacement);

		// Message thread: loads the held program changes and sends what was held behind them to the device, in order
		void processHeldEvents(const SendFunc& _sendToDevice);

		// Audio thread: if the message thread did not get to the held events for too long, send them to the device
		// anyway, the program changes among them load late then
		void releaseStaleHold(const SendFunc& _sendToDevice);

	private:
		struct PartState
		{
			std::atomic<uint8_t> bankMsb = 0;
			std::atomic<uint8_t> bankLsb = 0;
		};

		Result routeProgramChange(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>& _replacement);
		void trackBankSelect(const synthLib::SMidiEvent& _ev);
		uint32_t getMidiBankNumber(uint8_t _channel) const;
		bool startHold(const synthLib::SMidiEvent& _programChange);

		// returns false if the program change could not be loaded yet and has to stay held
		bool releaseHeldEvent(const synthLib::SMidiEvent& _ev, bool _superseded, const SendFunc& _sendToDevice);

		static bool isProgramChange(const synthLib::SMidiEvent& _ev);
		static bool isBankSelect(const synthLib::SMidiEvent& _ev);
		static bool isSuperseded(const std::vector<synthLib::SMidiEvent>& _events, size_t _index);

		std::array<PartState, MaxParts> m_partStates{};

		// held shared while the handler is called, exclusively to replace it
		std::shared_mutex m_handlerMutex;
		Handler* m_handler = nullptr;

		std::mutex m_holdMutex;
		std::atomic<bool> m_holding = false;
		uint32_t m_holdStartMs = 0;
		std::vector<synthLib::SMidiEvent> m_held;
		std::vector<synthLib::SMidiEvent> m_lateProgramChanges;	// left behind by releaseStaleHold()
	};
}

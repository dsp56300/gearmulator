#pragma once

#include "88lib/mt32/memory.h"

#include "synthLib/midiTypes.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace emu88Lib::mt32
{
	// Keeps a Memory of an MT-32 dialect board from what the player hands it - live input, the song player, an
	// editor's own edits - and from the board's answers to data requests, for the editor to show and the host to
	// save. process() runs under the processor's callback lock; the memory has a mutex of its own so that a copy
	// for the editor does not hold the audio callback for longer than that copy.
	class StateTracker
	{
	public:
		// Forgets everything and starts over with the board's power-on state
		void setModel(DeviceModel _model);
		DeviceModel model() const;
		void reset();

		// A message to the board. Anything that does not touch its memory passes without taking the lock.
		void process(const synthLib::SMidiEvent& _event);
		// A data set from the board, in answer to a data request
		void processReply(const synthLib::SMidiEvent& _event);

		// Changes with every change of the memory
		uint64_t revision() const { return m_revision.load(std::memory_order_acquire); }
		Memory memory() const;
		Memory memory(uint64_t& _revision) const;

		// What a host saves: the SysEx that set a freshly reset board to this state, see Memory::createRestore().
		// Restoring is sending them to the board and to process(), which brings the memory here back too.
		std::vector<Message> createRestore() const;

	private:
		void changed();

		mutable std::mutex m_mutex;
		Memory m_memory{DeviceModel::Cm32l};
		std::atomic<uint64_t> m_revision{1};
	};
}

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <memory>
#include <vector>
#include <array>

#include "synthLib/deviceTypes.h"
#include "synthLib/midiTypes.h"

namespace wLib
{
	using SysEx = synthLib::SysexBuffer;
	using Responses = synthLib::SysexBufferList;

	class State
	{
	public:
		// Saves the state as it will be once the next process() call has applied the edits that wait for it, see
		// synthLib::Device::getStateWithPendingMidi(). They go to a copy that sends nothing to the device, which gets
		// them from that call
		template<typename TState>
		static bool getStateWithPendingMidi(const TState& _state, std::vector<uint8_t>& _out, const synthLib::StateType _type, const std::vector<synthLib::SMidiEvent>& _pendingMidi)
		{
			const auto hasSysex = std::any_of(_pendingMidi.begin(), _pendingMidi.end(), [](const synthLib::SMidiEvent& _e)
			{
				return !_e.sysex.empty();
			});

			if (!hasSysex)
				return _state.getState(_out, _type);

			// on the heap, a state is big
			const auto copy = std::make_unique<TState>(_state);
			copy->m_offline = true;

			Responses unused;
			for (const auto& e : _pendingMidi)
			{
				if (!e.sysex.empty())
					copy->receive(unused, e.sysex, TState::Origin::External);
			}
			return copy->getState(_out, _type);
		}

	protected:
		// a copy that only builds a state, see getStateWithPendingMidi(): it must send nothing to the device
		bool m_offline = false;

		template<size_t Size> static bool convertTo(std::array<uint8_t, Size>& _dst, const SysEx& _data)
		{
			if(_data.size() != Size)
				return false;
			std::copy(_data.begin(), _data.end(), _dst.begin());
			return true;
		}

		template<size_t Size> static SysEx convertTo(const std::array<uint8_t, Size>& _src)
		{
			SysEx dst;
			dst.insert(dst.begin(), _src.begin(), _src.end());
			return dst;
		}

		template<size_t Size> static void updateChecksum(std::array<uint8_t, Size>& _src, uint32_t _startIndex)
		{
			uint8_t& c = _src[_src.size() - 2];
			c = 0;
			for(size_t i = _startIndex; i<_src.size()-2; ++i)
				c += _src[i];
			c &= 0x7f;
		}

	};
}

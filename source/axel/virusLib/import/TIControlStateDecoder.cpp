#include "TIControlStateDecoder.h"

#include <iterator>

namespace virusLib
{
	namespace
	{
		bool findMarker(uint32_t& _offset, const synthLib::SysexBuffer& _data)
		{
			constexpr char marker[] = "MIDI";
			constexpr size_t markerSize = sizeof(marker) - 1;

			if(_data.size() < static_cast<size_t>(_offset) + markerSize)
				return false;

			for(size_t i = _offset; i <= _data.size() - markerSize; ++i)
			{
				bool valid = true;
				for(size_t j = 0; j < markerSize; ++j)
				{
					if(static_cast<char>(_data[i + j]) == marker[j])
						continue;
					valid = false;
					break;
				}

				if(valid)
				{
					_offset = static_cast<uint32_t>(i);
					return true;
				}
			}
			return false;
		}
	}

	bool TIControlStateDecoder::decode(std::vector<synthLib::SMidiEvent>& _events, const synthLib::SysexBuffer& _state)
	{
		if(_state.size() < 8)
			return false;

		uint32_t searchPos = 0;
		uint32_t numFound = 0;

		auto readLen = [&_state](const size_t _offset) -> uint32_t
		{
			if(_offset + 4 > _state.size())
				return 0;
			return
				(static_cast<uint32_t>(_state[_offset+0]) << 24) |
				(static_cast<uint32_t>(_state[_offset+1]) << 16) |
				(static_cast<uint32_t>(_state[_offset+2]) << 8) |
				(static_cast<uint32_t>(_state[_offset+3]));
		};

		while(searchPos <= _state.size() - 4)
		{
			uint32_t markerPos = searchPos;
			if(!findMarker(markerPos, _state))
				break;

			size_t readPos = static_cast<size_t>(markerPos) + 4;
			searchPos = markerPos + 4;

			if(readPos + 4 > _state.size())
				continue;

			const auto dataLen = readLen(readPos);
			readPos += 4;
			if(dataLen > _state.size() - readPos)
				continue;

			const auto dataEnd = readPos + dataLen;
			if(readPos + 4 > dataEnd)
				continue;

			const auto controllerAssignmentsLen = readLen(readPos);
			readPos += 4;
			if(controllerAssignmentsLen > dataEnd - readPos)
				continue;
			readPos += controllerAssignmentsLen;

			std::vector<synthLib::SMidiEvent> blockEvents;
			uint32_t blockSysexCount = 0;
			bool valid = true;

			while(readPos < dataEnd)
			{
				if(readPos + 4 > dataEnd)
				{
					valid = false;
					break;
				}

				const auto midiDataLen = readLen(readPos);
				readPos += 4;
				if(!midiDataLen)
					break;

				if(midiDataLen > dataEnd - readPos)
				{
					valid = false;
					break;
				}

				synthLib::SMidiEvent& e = blockEvents.emplace_back(synthLib::MidiEventSource::Internal);

				e.sysex.assign(_state.begin() + readPos, _state.begin() + readPos + midiDataLen);

				if(e.sysex.front() != 0xf0)
				{
					if(e.sysex.size() > 3)
					{
						valid = false;
						break;
					}
					e.a = e.sysex[0];
					if(e.sysex.size() > 1)
						e.b = e.sysex[1];
					if(e.sysex.size() > 2)
						e.c = e.sysex[2];

					e.sysex.clear();
				}

				readPos += midiDataLen;

				if(!e.sysex.empty())
					++blockSysexCount;
			}

			if(valid)
			{
				_events.insert(_events.end(), std::make_move_iterator(blockEvents.begin()), std::make_move_iterator(blockEvents.end()));
				numFound += blockSysexCount;
			}

			searchPos = static_cast<uint32_t>(dataEnd);
		}

		return numFound > 0;
	}
}

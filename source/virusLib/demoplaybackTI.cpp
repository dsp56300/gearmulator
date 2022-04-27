#include "demoplaybackTI.h"

#include <cstdint>
#include <cstdio>
#include <vector>

#include "microcontroller.h"
#include "../synthLib/midiTypes.h"

namespace virusLib
{
	bool DemoPlaybackTI::loadFile(const std::string& _filename)
	{
		auto* hFile = fopen(_filename.c_str(), "rb");
		if(!hFile)
			return false;

		fseek(hFile, 0, SEEK_END);
		const size_t size = ftell(hFile);
		fseek(hFile, 0, SEEK_SET);

		std::vector<uint8_t> data;
		data.resize(size);
		const auto numRead = fread(&data[0], 1, size, hFile);
		fclose(hFile);
		if(numRead != size)
			return false;

		constexpr uint8_t key[] = "demoDrums Mixed                 ";
		constexpr size_t keySize = std::size(key) - 1;

		for(size_t i=0; i<data.size() - keySize; ++i)
		{
			for(size_t s=0; s<keySize; ++s)
			{
				if(data[i+s] != key[s])
					break;

				if(s == keySize - 1)
				{
					data.erase(data.begin(), data.begin() + static_cast<std::vector<uint8_t>::difference_type>(i + keySize));
					parseData(data);
					setTimeScale(10.89f);
					return true;
				}
			}
		}

		return false;
	}

	void DemoPlaybackTI::parseData(const std::vector<uint8_t>& _data)
	{
		// skip 2 unknown bytes at startup
		for(size_t i=2; i<_data.size();)
		{
			if(!parseChunk(_data, i))
				break;

			i += 3 + m_chunks.back().data.size();
		}
	}

	bool DemoPlaybackTI::parseChunk(const std::vector<uint8_t>& _data, const size_t _offset)
	{
		Chunk chunk;
		chunk.deltaTime = static_cast<uint32_t>(_data[_offset]) << 8 | _data[_offset+1];

		const auto size = _data[_offset+2];

		if(_offset + 3 + size > _data.size())
			return false;

		chunk.data.resize(size);
		memcpy(&chunk.data[0], &_data[_offset+3], size);

		m_chunks.push_back(chunk);
		return true;
	}

	bool DemoPlaybackTI::processEvent(const Chunk& _chunk)
	{
		const auto& data = _chunk.data;

		for(size_t i=0; i<data.size();)
		{
			if(data[i] == 0xff)
				return false;	// end

			if(data[i] == 0xf4)
			{
				writeRawData(data);
				m_remainingPresetBytes = 512 - static_cast<int32_t>(data.size());
				break;
			}
			if(m_remainingPresetBytes > 0)
			{
				writeRawData(data);
				m_remainingPresetBytes -= static_cast<int32_t>(data.size());
				break;
			}

			auto sendMidiEvent = [&](const size_t _offset)
			{
				synthLib::SMidiEvent ev;
				ev.a = data[_offset];
				ev.b = data.size() > _offset + 1 ? data[_offset + 1] : 0;
				ev.c = data.size() > _offset + 2 ? data[_offset + 2] : 0;
				m_mc.sendMIDI(ev);
				m_mc.sendPendingMidiEvents(std::numeric_limits<uint32_t>::max());
			};

			switch (data[i] & 0xf0)
			{
			case synthLib::M_NOTEOFF:
			case synthLib::M_NOTEON:
			case synthLib::M_POLYPRESSURE:
			case synthLib::M_CONTROLCHANGE:
			case synthLib::M_PROGRAMCHANGE:
			case synthLib::M_PITCHBEND:
				sendMidiEvent(i);
				i += 3;
				break;
			case synthLib::M_AFTERTOUCH:
				sendMidiEvent(i);
				i += 2;
				break;
			default:
				assert(false && "unknown event type");
			}
		}

		return true;
	}
}

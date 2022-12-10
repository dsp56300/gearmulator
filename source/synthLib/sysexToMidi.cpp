#include "sysexToMidi.h"

#include <fstream>

namespace synthLib
{
	constexpr uint16_t g_ppq = 96;
	constexpr uint16_t g_beatsBetweenMessages = 1;

	bool SysexToMidi::write(const char* _filename, const std::vector<std::vector<uint8_t>>& _messages)
	{
		std::ofstream f(_filename, std::ios::binary);
		if(!f.is_open())
			return false;
		try
		{
			write(f, _messages);
		}
		catch (...)
		{
			return false;
		}
		return true;
	}

	void SysexToMidi::write(std::ostream& _dst, const std::vector<std::vector<uint8_t>>& _messages)
	{
		write(_dst, "MThd");	// header
		writeUInt32(_dst, 6);	// chunk length = 6
		writeUInt16(_dst, 0);	// format = single multi-channel track
		writeUInt16(_dst, 1);	// number of tracks = 1
		writeUInt16(_dst, 96);	// ticks per quarter note = 96

		// track chunk
		write(_dst, "MTrk");
		const auto trackChunkBegin = _dst.tellp();
		writeUInt32(_dst, 0);		// will be replaced later

		for (const auto& message : _messages)
		{
			writeVarLen(_dst, g_ppq * g_beatsBetweenMessages);				// delta time
			writeUInt8(_dst, message.front());								// f0 comes first...
			writeVarLen(_dst, static_cast<uint32_t>(message.size() - 1));		// ...then the length
			_dst.write(reinterpret_cast<const char*>(&message[1]), static_cast<std::streamsize>(message.size() - 1));
		}
		const auto newPos = _dst.tellp();
		const auto trackChunkLength = newPos - trackChunkBegin;

		writeBuf(_dst, {0xff,0x2f,0x00});	// end of track

		_dst.seekp(trackChunkBegin);
		writeUInt32(_dst, static_cast<uint32_t>(trackChunkLength));
	}

	void SysexToMidi::writeBuf(std::ostream& _dst, const std::vector<uint8_t>& _data)
	{
		_dst.write(reinterpret_cast<const char*>(&_data[0]), static_cast<std::streamsize>(_data.size()));
	}

	void SysexToMidi::writeUInt8(std::ostream& _dst, uint8_t _data)
	{
		_dst.write(reinterpret_cast<const char*>(&_data), 1);
	}

	void SysexToMidi::writeUInt32(std::ostream& _dst, const uint32_t& _data)
	{
		writeBuf(_dst, 
		{
			static_cast<unsigned char>((_data >> 24) & 0xff),
			static_cast<unsigned char>((_data >> 16) & 0xff),
			static_cast<unsigned char>((_data >> 8) & 0xff),
			static_cast<unsigned char>(_data & 0xff)
		});
	}

	void SysexToMidi::writeUInt16(std::ostream& _dst, const uint16_t& _data)
	{
		writeBuf(_dst,
		{
			static_cast<unsigned char>((_data >> 8) & 0xff),
			static_cast<unsigned char>(_data & 0xff)
		});
	}

	void SysexToMidi::write(std::ostream& _dst, const std::string& _data)
	{
		_dst << _data;
	}

	void SysexToMidi::writeVarLen(std::ostream& _dst, uint32_t _len)
	{
		auto buffer = _len & 0x7f;

		while ( (_len >>= 7) )
		{
			buffer <<= 8;
			buffer |= ((_len & 0x7f) | 0x80);
		}

		while (true)
		{
			const auto byte = static_cast<uint8_t>(buffer & 0xff);
			writeUInt8(_dst, byte);
			if ((byte & 0x80) == 0)
				break;
			buffer >>= 8;
		}
	}
}

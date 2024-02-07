#include "midiFileToRomData.h"

#include "dsp56kEmu/logging.h"

#include "../synthLib/os.h"
#include "../synthLib/midiToSysex.h"

namespace virusLib
{
	bool MidiFileToRomData::load(const std::string& _filename)
	{
		std::vector<uint8_t> sysex;

		if(!synthLib::MidiToSysex::readFile(sysex, _filename.c_str()))
			return false;

		return load(sysex);
	}

	bool MidiFileToRomData::load(const std::vector<uint8_t>& _fileData)
	{
		std::vector<std::vector<uint8_t>> packets;

		synthLib::MidiToSysex::splitMultipleSysex(packets, _fileData);

		return add(packets);
	}

	bool MidiFileToRomData::add(const std::vector<Packet>& _packets)
	{
		for (const auto& packet : _packets)
		{
			if(!add(packet))
				return false;
			if(isComplete())
				return true;
		}
		return false;
	}

	bool MidiFileToRomData::add(const Packet& _packet)
	{
		if(isComplete())
			return isValid();

		if(_packet.size() < 11)
			return isValid();

		const auto cmd = _packet[5];

		switch (cmd)
		{
		case 0x50:	// Virus A
		case 0x53:	// Virus B OS
		case 0x55:	// Virus B Demo
		case 0x57:	// Virus C
			{
				const auto msb = _packet[6];	// packet number MSB
				const auto lsb = _packet[7];	// packet number LSB

				uint8_t checksum = 0;
				for(size_t i=5; i<_packet.size()-2; ++i)
					checksum += _packet[i];
				checksum &= 0x7f;

				if(checksum != _packet[_packet.size()-2])
				{
					LOG("Packet MSB " << static_cast<int>(msb) << " LSB " << static_cast<int>(lsb) << " is invalid, wrong checksum");
					m_valid = false;
					return false;
				}

				if(!processPacket(_packet, msb, lsb))
				{
					m_valid = false;
					return false;
				}
				return isValid();
			}
		default:
			// skip unknown packets
			return true;
		}
	}

	bool MidiFileToRomData::setCompleted()
	{
		m_complete = true;
		if(!toBinary(m_data))
			m_valid = false;
		return isComplete();
	}

	bool MidiFileToRomData::toBinary(std::vector<uint8_t>& _binary) const
	{
		if(!isComplete())
			return false;

		for (const auto& p : m_packets)
		{
			// midi bytes in a sysex frame can only carry 7 bit, not 8. They've chosen the easy way that costs more storage
			// They transfer only one nibble of a ROM byte in one midi byte to ensure that the most significant nibble is
			// always zero. By concating two nibbles together we get one ROM byte
			for(size_t s=8; s<p.size()-2; s += 2)
			{
				const uint8_t a = p[s];
				const uint8_t b = p[s+1];
				if(a > 0xf || b > 0xf)
				{
					LOG("Invalid data, high nibble must be 0");
					return false;
				}
				_binary.push_back(static_cast<uint8_t>(b << 4) | a);
			}
		}
		return true;
	}
	bool MidiFileToRomData::processPacket(const Packet& _packet, uint8_t msb, uint8_t lsb)
	{
//		LOG("Got Packet " << static_cast<int>(msb) << " " << static_cast<int>(lsb) << ", size " << _packet.size());

		auto packetInvalid = [&]()
		{
			LOG("Packet invalid, expected packet index " << static_cast<int>(m_expectedMSB) << " " << static_cast<int>(m_expectedLSB) << " but got " << static_cast<int>(msb) << " " << static_cast<int>(lsb));
			return false;
		};

		auto matchExpected = [&]()
		{
			return msb == m_expectedMSB && lsb == m_expectedLSB;
		};

		if(msb == 127 && m_expectedLSB > 3)
		{
			if(m_expectedSector == 0xff)
			{
				m_firstSector = m_expectedSector = lsb;
			}

			if(lsb != m_expectedSector)
			{
				if(lsb == 127 || lsb == 126)
					return setCompleted();
				return packetInvalid();
			}
			m_expectedLSB = m_expectedMSB = 0;
			++m_expectedSector;
//			addPacket(_packet);
			return true;
		}

		if(!matchExpected())
			return packetInvalid();

		addPacket(_packet);
		++m_expectedLSB;

		if(m_expectedLSB > 3 && msb != 127)
		{
			m_expectedLSB = 0;
			++m_expectedMSB;
		}
		return true;
	}
}

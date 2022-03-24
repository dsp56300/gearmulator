#include "demopacketvalidator.h"

#include <cassert>

#include "dsp56kEmu/logging.h"

namespace virusLib
{
	bool DemoPacketValidator::add(const Packet& _packet)
	{
		if(isComplete())
			return isValid();

		if(_packet.size() < 11)
			return isValid();

		const auto cmd = _packet[5];

		switch (cmd)
		{
		case 0x50:	// Virus A second
		case 0x55:	// Virus B second
		case 0x57:	// Virus C second
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
				
				auto packetInvalid = [&]()
				{
					LOG("Packet invalid, expected packet index " << static_cast<int>(m_expectedMSB) << " " << static_cast<int>(m_expectedLSB) << " but got " << static_cast<int>(msb) << " " << static_cast<int>(lsb));
					m_valid = false;
					return false;
				};

				auto matchExpected = [&]()
				{
					return msb == m_expectedMSB && lsb == m_expectedLSB;
				};

				if(msb == 127 && lsb >= 3)
				{
					if(lsb == 3)
					{
						if(!matchExpected())
							return packetInvalid();

						m_packets.push_back(_packet);

						m_expectedLSB = ++m_offset;
					}
					else if(lsb < 126)
					{
						if(!matchExpected())
							return packetInvalid();
						m_expectedLSB = 0;
						m_expectedMSB = 0;
					}
					else if(lsb == 127)
					{
						if(m_expectedMSB != msb)
						{
							LOG("Terminating too soon, missing packets");
							m_valid = false;
							return false;
						}

						m_complete = true;
						if(!toBinary(m_data))
							m_valid = false;
						return isComplete();
					}
				}
				else
				{
					if(!matchExpected())
						return packetInvalid();

					m_packets.push_back(_packet);

					if(lsb == 3)
					{
						++m_expectedMSB;
						m_expectedLSB = 0;
					}
					else
						++m_expectedLSB;
				}
			}
			break;
		default:
			// skip unknown packets
			return true;
		}

		return false;
	}

	bool DemoPacketValidator::toBinary(std::vector<uint8_t>& _binary) const
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
}

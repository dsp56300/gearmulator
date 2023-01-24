#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace virusLib
{
	class MidiFileToRomData
	{
	public:
		using Packet = std::vector<uint8_t>;

		MidiFileToRomData(const uint8_t _expectedFirstSector = 0xff) : m_expectedSector(_expectedFirstSector)
		{
		}

		bool load(const std::string& _filename);

		bool add(const std::vector<Packet>& _packets);
		bool add(const Packet& _packet);

		bool isValid() const { return m_valid; }
		bool isComplete() const { return isValid() && m_complete; }

		const std::vector<uint8_t>& getData() const { return m_data; }

		size_t getPacketCount() const { return m_packets.size(); }

		uint8_t getFirstSector() const { return m_firstSector; }
		
	private:
		void addPacket(const Packet& _packet)
		{
			m_packets.push_back(_packet);
		}
		bool setCompleted();

		bool processPacket(const Packet& _packet, uint8_t _msb, uint8_t _lsb);

		bool toBinary(std::vector<uint8_t>& _binary) const;

		std::vector<Packet> m_packets;
		std::vector<uint8_t> m_data;

		bool m_valid = true;
		bool m_complete = false;

		uint8_t m_expectedMSB = 127;
		uint8_t m_expectedLSB = 8;
		uint8_t m_expectedSector;
		uint8_t m_firstSector = 0xff;
	};
}

#pragma once

#include <cstdint>
#include <vector>

namespace virusLib
{
	class DemoPacketValidator
	{
	public:
		using Packet = std::vector<uint8_t>;

		bool add(const Packet& _packet);

		bool isValid() const { return m_valid; }
		bool isComplete() const { return isValid() && m_complete; }

		const std::vector<uint8_t>& getData() const { return m_data; }

	private:
		bool toBinary(std::vector<uint8_t>& _binary) const;

		std::vector<Packet> m_packets;
		std::vector<uint8_t> m_data;

		uint8_t m_expectedMSB = 127;
		uint8_t m_expectedLSB = 8;
		uint8_t m_offset = 8;

		bool m_valid = true;
		bool m_complete = false;
	};
}

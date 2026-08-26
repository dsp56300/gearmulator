#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

namespace hwLib
{
	class Am29f
	{
	public:
		struct BusCycle
		{
			uint16_t addr;
			uint8_t data;
		};

		struct Command
		{
			std::vector<BusCycle> cycles;
		};

		enum class CommandType
		{
			Invalid = -1,
			ChipErase,
			SectorErase,
			Program,
		};

		explicit Am29f(uint8_t* _buffer, size_t _size, bool _useWriteEnable, bool _bitreversedCmdAddr);
		virtual ~Am29f() = default;

		void writeEnable(bool _writeEnable)
		{
			m_writeEnable = _writeEnable;
		}

		void write(uint32_t _addr, uint16_t _data);

		bool eraseSector(uint32_t _addr, size_t _sizeInKb) const;

		virtual bool eraseSector(const uint32_t _addr) const
		{
			return eraseSector4MbitTopBoot(_addr);
		}

		bool eraseSector1Mbit(uint32_t _addr) const;
		bool eraseSector2MbitTopBoot(uint32_t _addr) const;
		bool eraseSector4MbitTopBoot(uint32_t _addr) const;

	private:
		bool writeEnabled() const
		{
			return !m_useWriteEnable || m_writeEnable;
		}

		static constexpr uint16_t bitreverse(uint16_t _x)
		{
			_x = ((_x & 0xaaaau) >> 1) | static_cast<uint16_t>((_x & 0x5555u) << 1);
			_x = ((_x & 0xccccu) >> 2) | static_cast<uint16_t>((_x & 0x3333u) << 2);
			_x = ((_x & 0xf0f0u) >> 4) | static_cast<uint16_t>((_x & 0x0f0fu) << 4);

			return ((_x & 0xff00) >> 8) | static_cast<uint16_t>((_x & 0x00ff) << 8);
		}

		void execCommand(CommandType _command, uint32_t _addr, uint16_t _data) const;

		uint8_t* m_buffer;
		const size_t m_size;
		const bool m_useWriteEnable;
		const bool m_bitreverseCmdAddr;

		std::vector<Command> m_commands;
		bool m_writeEnable = false;
		uint32_t m_currentBusCycle = 0;
		int32_t m_currentCommand = -1;
	};
}
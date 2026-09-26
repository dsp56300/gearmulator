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
			Autoselect,
			UnlockBypass,
		};

		// _bitreversedCmdAddr: the command cycles arrive at $AAA/$554 instead of $555/$2AA. That is a bit reversed
		// address bus, and just as well a word mode chip on a 16 bit bus that the host addresses in bytes
		explicit Am29f(uint8_t* _buffer, size_t _size, bool _useWriteEnable, bool _bitreversedCmdAddr, bool _byteWide = false);
		virtual ~Am29f() = default;

		void writeEnable(bool _writeEnable)
		{
			m_writeEnable = _writeEnable;
		}

		void setIds(const uint8_t _manufacturer, const uint8_t _device)
		{
			m_manufacturerId = _manufacturer;
			m_deviceId = _device;
		}

		// returns true if the chip is in autoselect mode, in which case reads return the chip ids instead of memory content.
		// _addr is the host's byte offset, in word mode the ids are the low bytes of words 0 and 1 of a big endian bus
		bool readAutoselect(uint32_t _addr, uint8_t& _result) const;

		void write(uint32_t _addr, uint16_t _data);

		bool eraseSector(uint32_t _addr, size_t _sizeInKb) const;

		virtual bool eraseSector(const uint32_t _addr) const
		{
			return eraseSector4MbitTopBoot(_addr);
		}

		// sector maps of the datasheets, any address within a sector selects it
		bool eraseSector1Mbit(uint32_t _addr) const;
		bool eraseSector2MbitTopBoot(uint32_t _addr) const;
		bool eraseSector4MbitTopBoot(uint32_t _addr) const;
		bool eraseSector8MbitBottomBoot(uint32_t _addr) const;

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
		const bool m_byteWide;
		const uint16_t m_cmdAddrMask;	// the address bits a command cycle decodes

		std::vector<Command> m_commands;
		bool m_writeEnable = false;
		bool m_autoselect = false;
		bool m_unlockBypass = false;
		uint8_t m_bypassCommand = 0;
		uint8_t m_manufacturerId = 0x01;
		uint8_t m_deviceId = 0;
		uint32_t m_currentBusCycle = 0;
		int32_t m_currentCommand = -1;
	};
}
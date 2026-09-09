#include "sc8850_submcu.h"

namespace emu88Lib
{
	Sc8850SubMcu::Sc8850SubMcu()
	{
		reset();
	}

	void Sc8850SubMcu::reset()
	{
		m_bootState = BootState::VersionHigh;
		m_toHost.clear();
		m_pendingPackets.clear();
		m_uploadedProgram.clear();
		m_uploadedProgram.reserve(ProgramSize);
		m_midiOut.clear();
		m_outputPacket.fill(0);
		m_outputPacketSize = 0;
		m_configuration = 0;
		m_transmitIrqPending = false;
		m_receiveRequestOutstanding = false;
		m_transmitRequestOutstanding = false;
		m_enumerationDelay = 0;
		m_ready = false;
		m_cables.fill({});

		// Resident monitor version, then the monitor's sync byte. The SH only
		// records the version; its update decision follows the FB/FC/FE exchange.
		enqueueBoot(0xe0, 0x00);
		enqueueBoot(0xf0, 0x00);
		enqueueBoot(0x00, 0xfb);
	}

	void Sc8850SubMcu::enqueueBoot(const uint8_t _tag, const uint8_t _value)
	{
		m_toHost.push_back({_tag, _value, false});
	}

	void Sc8850SubMcu::enqueueStatus(const uint8_t _value)
	{
		m_toHost.push_back({0x00, _value, true});
	}

	uint8_t Sc8850SubMcu::hostRead(const uint32_t _address)
	{
		const bool receiveChannel = _address >= 0x00580000;
		const bool statusPort = (_address & 1) != 0;
		if(!receiveChannel)
		{
			// Channel zero is SH -> UIPC. The HLE consumes writes immediately,
			// therefore IBF is never observed set by the SH.
			return statusPort ? 0x00 : 0xff;
		}

		if(statusPort)
		{
			if(m_toHost.empty())
				return 0x00;
			const auto& value = m_toHost.front();
			return static_cast<uint8_t>(value.tag | (value.packetStart ? 0x04 : 0x00) | 0x01);
		}

		if(m_toHost.empty())
			return 0xff;

		m_receiveRequestOutstanding = false;
		const MailboxByte value = m_toHost.front();
		m_toHost.pop_front();
		if(m_bootState == BootState::FinalReply && value.value == 0xff)
			enterRuntime();

		kickInterrupts();
		return value.value;
	}

	void Sc8850SubMcu::hostWrite(const uint32_t _address, const uint8_t _value)
	{
		if(_address >= 0x00580000)
			return;

		switch(m_bootState)
		{
		case BootState::VersionHigh:
			enqueueBoot(0x00, 0xfc);
			m_bootState = BootState::VersionLow;
			return;
		case BootState::VersionLow:
			// The external program is held in volatile SRAM, so a cold virtual
			// power-on always requests the same 32 KiB upload as the real unit.
			enqueueBoot(0x00, 0xfe);
			m_bootState = BootState::Program;
			return;
		case BootState::Program:
			m_uploadedProgram.push_back(_value);
			if(m_uploadedProgram.size() == ProgramSize)
			{
				enqueueBoot(0x00, 0xf0);
				m_bootState = BootState::Configuration;
			}
			return;
		case BootState::Configuration:
			m_configuration = _value;
			enqueueBoot(0x00, 0xff);
			m_bootState = BootState::FinalReply;
			return;
		case BootState::FinalReply:
			return;
		case BootState::Runtime:
			break;
		}
		m_transmitRequestOutstanding = false;

		// PutUsbMidi marks the first byte by writing it through the A0=1
		// alias, then writes the remaining three through A0=0.
		if(_address & 1)
			m_outputPacketSize = 0;
		if(m_outputPacketSize < m_outputPacket.size())
			m_outputPacket[m_outputPacketSize++] = _value;
		if(m_outputPacketSize == m_outputPacket.size())
		{
			consumeOutputPacket();
			m_outputPacketSize = 0;
		}
		else
		{
			// Firmware advances the remaining bytes of this packet one at a
			// time. A completed packet does not manufacture an extra poll: the
			// board requests another interrupt only if its output ring is nonempty.
			requestTransmit();
		}
	}

	void Sc8850SubMcu::enterRuntime()
	{
		m_bootState = BootState::Runtime;
		// The real MCU initializes its downloaded program and enumerates before
		// reporting configured. Deferring the event is important: delivering it
		// inside the SH's final boot-monitor read can pre-empt the loader before
		// it has published the runtime queue pointers.
		m_enumerationDelay = EnumerationDelayTicks;
	}

	void Sc8850SubMcu::tick()
	{
		if(m_bootState != BootState::Runtime || m_ready)
			return;
		if(m_enumerationDelay && --m_enumerationDelay)
			return;
		m_ready = true;
		// Tag zero, packet start, payload zero is the stock USB-ready event.
		enqueueStatus(0x00);
		while(!m_pendingPackets.empty())
		{
			enqueueUsbPacket(m_pendingPackets.front());
			m_pendingPackets.pop_front();
		}
		kickInterrupts();
	}

	void Sc8850SubMcu::kickInterrupts()
	{
		if(m_bootState != BootState::Runtime || !m_ready)
			return;
		if(!m_toHost.empty() && !m_receiveRequestOutstanding && receiveInterrupt && receiveInterrupt())
			m_receiveRequestOutstanding = true;
		if(m_transmitIrqPending && !m_transmitRequestOutstanding && transmitInterrupt && transmitInterrupt())
		{
			m_transmitIrqPending = false;
			m_transmitRequestOutstanding = true;
		}
	}

	void Sc8850SubMcu::requestTransmit()
	{
		if(m_bootState != BootState::Runtime || !m_ready || m_transmitIrqPending || m_transmitRequestOutstanding)
			return;
		m_transmitIrqPending = true;
		kickInterrupts();
	}

	void Sc8850SubMcu::midiIn(const uint8_t _value)
	{
		consumeMidiByte(0, _value);
	}

	void Sc8850SubMcu::midiIn(const uint8_t* const _data, const size_t _size)
	{
		midiIn(0, _data, _size);
	}

	void Sc8850SubMcu::midiIn(const uint8_t _cable, const uint8_t* const _data, const size_t _size)
	{
		const auto cable = static_cast<uint8_t>(_cable % CableCount);
		for(size_t i = 0; i < _size; ++i)
			consumeMidiByte(cable, _data[i]);
	}

	void Sc8850SubMcu::enqueueUsbPacket(const std::array<uint8_t, 4>& _packet)
	{
		if(m_bootState != BootState::Runtime || !m_ready)
		{
			m_pendingPackets.push_back(_packet);
			return;
		}
		for(size_t i = 0; i < _packet.size(); ++i)
			m_toHost.push_back({0x50, _packet[i], i == 0});
		kickInterrupts();
	}

	void Sc8850SubMcu::emitInputPacket(const uint8_t _cable, const uint8_t _cin,
	                                const uint8_t _a, const uint8_t _b, const uint8_t _c)
	{
		// USB-MIDI event packet: byte 0 is (cable << 4) | CIN. The firmware
		// routes the cable number to part group A-D.
		enqueueUsbPacket({static_cast<uint8_t>(((_cable & 0x0f) << 4) | (_cin & 0x0f)), _a, _b, _c});
	}

	uint8_t Sc8850SubMcu::midiDataLength(const uint8_t _status)
	{
		if(_status < 0x80)
			return 0;
		if(_status < 0xf0)
			return ((_status & 0xe0) == 0xc0) ? 1 : 2;
		switch(_status)
		{
		case 0xf1:
		case 0xf3: return 1;
		case 0xf2: return 2;
		default: return 0;
		}
	}

	void Sc8850SubMcu::consumeMidiByte(const uint8_t _cable, const uint8_t _value)
	{
		auto& parser = m_cables[_cable];

		if(_value >= 0xf8)
		{
			emitInputPacket(_cable, 0x0f, _value);
			return;
		}

		if(parser.inSysex)
		{
			if(_value == 0xf7)
			{
				parser.sysexChunk[parser.sysexChunkSize++] = _value;
				finishSysex(_cable);
				return;
			}
			if(_value < 0x80)
			{
				parser.sysexChunk[parser.sysexChunkSize++] = _value;
				if(parser.sysexChunkSize == parser.sysexChunk.size())
				{
					emitInputPacket(_cable, 0x04, parser.sysexChunk[0], parser.sysexChunk[1], parser.sysexChunk[2]);
					parser.sysexChunkSize = 0;
				}
				return;
			}
			// A non-realtime status aborts the unterminated SysEx message.
			parser.inSysex = false;
			parser.sysexChunkSize = 0;
		}

		if(_value & 0x80)
		{
			parser.messageDataSize = 0;
			parser.messageStatus = _value;
			if(_value == 0xf0)
			{
				parser.runningStatus = 0;
				parser.inSysex = true;
				parser.sysexChunk[0] = _value;
				parser.sysexChunkSize = 1;
				return;
			}
			if(_value >= 0xf0)
				parser.runningStatus = 0;
			else
				parser.runningStatus = _value;
			parser.messageDataExpected = midiDataLength(_value);
			if(!parser.messageDataExpected)
			{
				const uint8_t cin = _value == 0xf7 ? 0x05 : 0x0f;
				emitInputPacket(_cable, cin, _value);
				parser.messageStatus = 0;
			}
			return;
		}

		if(!parser.messageStatus)
		{
			if(!parser.runningStatus)
				return;
			parser.messageStatus = parser.runningStatus;
			parser.messageDataExpected = midiDataLength(parser.messageStatus);
		}
		if(parser.messageDataSize < parser.messageData.size())
			parser.messageData[parser.messageDataSize++] = _value;
		if(parser.messageDataSize == parser.messageDataExpected)
			finishMidiMessage(_cable);
	}

	void Sc8850SubMcu::finishMidiMessage(const uint8_t _cable)
	{
		auto& parser = m_cables[_cable];
		uint8_t cin = static_cast<uint8_t>(parser.messageStatus >> 4);
		if(parser.messageStatus >= 0xf0)
			cin = parser.messageDataExpected == 2 ? 0x03 : 0x02;
		// USB-MIDI packets are always four bytes, but unused payload bytes must
		// be deterministic padding rather than stale data from the preceding
		// three-byte message.
		emitInputPacket(_cable, cin, parser.messageStatus, parser.messageData[0],
		                parser.messageDataExpected > 1 ? parser.messageData[1] : 0);
		parser.messageDataSize = 0;
		parser.messageStatus = parser.runningStatus;
		parser.messageDataExpected = midiDataLength(parser.messageStatus);
	}

	void Sc8850SubMcu::finishSysex(const uint8_t _cable)
	{
		auto& parser = m_cables[_cable];
		const uint8_t cin = static_cast<uint8_t>(0x04 + parser.sysexChunkSize);
		emitInputPacket(_cable, cin, parser.sysexChunk[0],
		                parser.sysexChunkSize > 1 ? parser.sysexChunk[1] : 0,
		                parser.sysexChunkSize > 2 ? parser.sysexChunk[2] : 0);
		parser.inSysex = false;
		parser.sysexChunkSize = 0;
	}

	uint8_t Sc8850SubMcu::usbMidiLength(const uint8_t _cin)
	{
		switch(_cin & 0x0f)
		{
		case 0x02:
		case 0x06:
		case 0x0c:
		case 0x0d: return 2;
		case 0x03:
		case 0x04:
		case 0x07:
		case 0x08:
		case 0x09:
		case 0x0a:
		case 0x0b:
		case 0x0e: return 3;
		case 0x05:
		case 0x0f: return 1;
		default: return 0;
		}
	}

	void Sc8850SubMcu::consumeOutputPacket()
	{
		const uint8_t length = usbMidiLength(m_outputPacket[0]);
		for(uint8_t i = 0; i < length; ++i)
			m_midiOut.push_back(m_outputPacket[1 + i]);
	}

	void Sc8850SubMcu::readMidiOut(std::vector<uint8_t>& _output)
	{
		_output.insert(_output.end(), m_midiOut.begin(), m_midiOut.end());
		m_midiOut.clear();
	}
}

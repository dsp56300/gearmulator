#include "88lib/mcu/sc88_submcu.h"

#include <algorithm>
#include <cstddef>

namespace emu88Lib
{
	namespace
	{
		// Sub-MCU mailbox opcodes.
		constexpr uint8_t OpSysExContinue = 0xe7;	// next chunk of a split bulk transfer
		constexpr uint8_t OpSysExError    = 0xe0;	// bad SysEx -> "Check Sum Error"
		constexpr uint8_t OpSysExUniNonRt = 0xee;	// F0 7E ... — GM System On
		constexpr uint8_t OpSysExUniRt    = 0xef;	// F0 7F ... — Master Volume

		// The biggest transfer the firmware itself will take: 00:0daa abandons
		// once the accumulated staged count reaches 0x8a, and the first chunk
		// contributes len-7. Anything the assembly buffer holds beyond the
		// corresponding wire size can only be discarded, so the buffer is
		// bounded — the real MCU's RAM is a few hundred bytes total.
		constexpr size_t MaxSysExBytes = 0x100;

		// The firmware's own name for a parameter address: an opcode family per
		// address block, OR the high nibble of the middle address byte, with
		// the low nibble in the channel bits. Its transmitter turns an opcode
		// back into the block with a table (SC-88 01:04BA, SC-88VL 01:04C2,
		// Pro 01:0680) - this is that table inverted. The Pro firmware adds
		// 22-27, 2A and 2B, and its ISR maps a raw 2x transfer (opcode 0xEA)
		// onto the same opcodes (table 00:0B8E, RQ1 at 00:0E62). The 24-27
		// blocks take an opcode each, B1-B4, and it ORs the nibble into those
		// all the same.
		uint8_t addressOpcode(const uint8_t _block, const uint8_t _middle)
		{
			uint8_t family;
			switch(_block)
			{
			case 0x00: family = 0x20; break;	// system
			case 0x08: family = 0x28; break;
			case 0x0c: family = 0x30; break;
			case 0x0f: family = 0x38; break;
			case 0x10: family = 0x10; break;	// display
			case 0x18: family = 0x18; break;
			case 0x20: family = 0x40; break;	// user tone / drum
			case 0x21: family = 0x50; break;
			case 0x22: family = 0xa0; break;
			case 0x23: family = 0xb0; break;
			case 0x24: family = 0xb1; break;
			case 0x25: family = 0xb2; break;
			case 0x26: family = 0xb3; break;
			case 0x27: family = 0xb4; break;
			case 0x28: family = 0x48; break;
			case 0x29: family = 0x58; break;
			case 0x2a: family = 0xa8; break;
			case 0x2b: family = 0xb8; break;
			case 0x40: family = 0x60; break;	// patch A
			case 0x41: family = 0x80; break;	// drum setup A
			case 0x48: family = 0x68; break;	// bulk
			case 0x49: family = 0x88; break;
			case 0x50: family = 0x70; break;	// patch B
			case 0x51: family = 0x90; break;	// drum setup B
			case 0x58: family = 0x78; break;
			case 0x59: family = 0x98; break;
			default: return 0;
			}
			return static_cast<uint8_t>(family | (_middle >> 4));
		}
	}

	Sc88SubMcu::Sc88SubMcu(Sink _sink, const uint16_t _stageOffset, const uint16_t _stageCapacity)
		: m_sink(std::move(_sink))
		, m_stageOffset(_stageOffset)
		, m_stageCapacity(_stageCapacity)
	{
	}

	void Sc88SubMcu::reset()
	{
		m_sources = {};
		m_outputQueue.clear();
		m_output = synthLib::MidiBufferParser{synthLib::MidiEventSource::Device};
		m_outputPhase = m_outputPause = 0;
	}

	void Sc88SubMcu::startOutput(SharedRam& _ram)
	{
		// Both H8 firmwares wrap the transmit ring from 0xc0 to 0x24.
		_ram[TxRead] = _ram[TxWrite] = 0x24;
		m_outputQueue.clear();
		m_outputPhase = m_outputPause = 0;
	}

	void Sc88SubMcu::commitOutput(const SharedRam& _ram, const uint8_t _write)
	{
		// A length byte precedes each packet; records are padded to four bytes.
		// Snapshot on publication: the receive mailbox also stages data in RAM.
		auto pos = _ram[TxWrite];
		if(pos < 0x24 || pos >= 0xc0 || _write < 0x24 || _write >= 0xc0 || pos == _write)
			return;
		const unsigned available = (_write + 0x9c - pos) % 0x9c;
		const unsigned length = _ram[pos];
		if(!length || ((length + 4) & ~3u) != available)
			return;
		OutputPacket packet;
		packet.end = _write;
		for(unsigned i = 0; i < length; ++i)
		{
			if(++pos == 0xc0)
				pos = 0x24;
			packet.bytes.push_back(_ram[pos]);
		}
		m_outputQueue.emplace_back(std::move(packet));
	}

	void Sc88SubMcu::clockOutput(SharedRam& _ram, const uint32_t _sampleRate)
	{
		if(m_outputPause)
		{
			--m_outputPause;
			return;
		}
		if(m_outputQueue.empty())
		{
			m_outputPhase = 0;
			return;
		}
		auto& packet = m_outputQueue.front();
		if(packet.offset == 0 && packet.bytes.size() == 3 && packet.bytes[0] == 0xff && packet.bytes[1] == 0)
		{
			// Firmware inserts FF 00 2D between bulk packets. It is a sub-MCU
			// delay command, not MIDI System Reset. Use milliseconds for the
			// delay until the physical controller's timer unit is measured.
			m_outputPause = packet.bytes[2] * _sampleRate / 1000;
			packet.offset = packet.bytes.size();
		}
		else
		{
			m_outputPhase += 3125; // 31250 baud, ten bits per byte
			if(m_outputPhase < _sampleRate)
				return;
			m_outputPhase -= _sampleRate;
			m_output.write(packet.bytes[packet.offset++]);
		}
		if(packet.offset == packet.bytes.size())
		{
			_ram[TxRead] = packet.end;
			m_outputQueue.pop_front();
			// E1 wakes the firmware task sleeping on transmit-ring space
			// (SC-88 IRQ2 handler 00:0cc7, event mask 0x40).
			Record ready;
			ready.command = 0xe1;
			ready.wireBytes = 0;
			m_sink(std::move(ready));
		}
	}

	void Sc88SubMcu::midiIn(const uint8_t _source, const uint8_t _byte)
	{
		if(_source >= SourceCount)
			return;
		auto& s = m_sources[_source];

		// Realtime bytes may appear anywhere, including inside a SysEx, and
		// must not disturb any assembly state. The SC-88 does not sequence, and
		// no mailbox opcode for them has been identified — the sub-MCU swallows
		// them.
		if(_byte >= 0xf8)
			return;

		++s.bytesThisMessage;

		if(_byte >= 0x80)
		{
			// Any non-realtime status terminates a SysEx in progress; only F7
			// makes it a valid message.
			if(s.inSysEx)
			{
				s.inSysEx = false;
				if(_byte == 0xf7 && !s.sysExOverrun)
				{
					endOfSysEx(_source, s);
					s.sysEx.clear();
					return;
				}
				s.sysEx.clear();
			}

			if(_byte == 0xf0)
			{
				s.inSysEx = true;
				s.sysExOverrun = false;
				s.sysEx.clear();
				s.sysEx.push_back(_byte);
				s.runningStatus = 0;	// system status clears running status
				s.bytesThisMessage = 1;
				return;
			}

			if(_byte >= 0xf1)
			{
				// System common (F1-F6): tune request, song position/select.
				// Clears running status; no mailbox record identified.
				s.runningStatus = 0;
				s.have = 0;
				s.bytesThisMessage = 0;
				return;
			}

			// Channel status.
			s.runningStatus = _byte;
			s.have = 0;
			s.bytesThisMessage = 1;
			return;
		}

		// Data byte.
		if(s.inSysEx)
		{
			if(s.sysEx.size() >= MaxSysExBytes)
				s.sysExOverrun = true;
			else
				s.sysEx.push_back(_byte);
			return;
		}

		if(!s.runningStatus)
			return;	// stray data with no status — the wire parser drops it

		s.data[s.have++] = _byte;

		const uint8_t status = s.runningStatus & 0xf0;
		const uint8_t need = (status == 0xc0 || status == 0xd0) ? 1 : 2;
		if(s.have >= need)
			voiceMessage(_source, s);
	}

	// A complete channel voice message: opcode = (status >> 4) - 7.
	//
	//   0x8 note off        -> 1     0xC program change -> 5
	//   0x9 note on         -> 2     0xD channel press. -> 6
	//   0xA poly pressure   -> 3     0xE pitch bend     -> 7
	//   0xB control change  -> 4
	//
	void Sc88SubMcu::voiceMessage(const uint8_t _source, Source& _s)
	{
		const uint8_t status = _s.runningStatus & 0xf0;

		Record r;
		r.command = static_cast<uint8_t>((status >> 4) - 7);
		r.channel = static_cast<uint8_t>((_s.runningStatus & 0x0f) | (_source << 4));
		r.param1  = _s.data[0];
		r.param2  = _s.have > 1 ? _s.data[1] : 0;
		// The real wire cost of THIS message: 2-3 bytes with a status byte,
		// one less under running status.
		r.wireBytes = std::max<uint8_t>(_s.bytesThisMessage, 1);

		_s.have = 0;
		_s.bytesThisMessage = 0;	// a following running-status message has no status byte

		m_sink(std::move(r));
	}

	// A complete, F7-terminated SysEx. The sub-MCU does the framing, header
	// match and checksum itself and hands over only the decoded transfer.
	void Sc88SubMcu::endOfSysEx(const uint8_t _source, Source& _s)
	{
		const auto& sx = _s.sysEx;

		if(sx.size() < 3)
			return;

		// The two universal messages travel as raw transfers, like the XG one
		// below: the CPU's handlers read the staged bytes themselves. They skip
		// the 7E/7F, check the device ID and sub-IDs and queue the values -
		// SC-88 00:0E52 (Master Volume) / 00:0E70 (GM), Pro 00:0F10 / 00:0F49,
		// which also wants the exact length, 6 or 4. A bare record never
		// reaches them: it is queued as is, and the Pro's task ignores a GM
		// System On that lacks the 09 its handler would have added.
		if(sx[1] == 0x7e || sx[1] == 0x7f)
		{
			if(sx[1] == 0x7e && sx.size() >= 5 && sx[3] == 0x09)
				rawTransfer(_source, OpSysExUniNonRt, sx);
			else if(sx[1] == 0x7f && sx.size() >= 7 && sx[3] == 0x04 && sx[4] == 0x01)
				rawTransfer(_source, OpSysExUniRt, sx);
			return;
		}

		// EC is the Pro firmware's Yamaha parser. It validates the 10/4C
		// header and enables its own compatibility mode on XG System On.
		if(sx[1] == 0x43)
		{
			if(sx.size() >= 8 && sx[2] == 0x10 && sx[3] == 0x4c)
				rawTransfer(_source, 0xec, sx);
			return;
		}

		// Roland DT1 and RQ1.
		if(sx[1] != 0x41)
			return;

		// F0 41 <dev> 42 <cmd> ... The display blocks (Display Letter 0x10,
		// Display Dot Data 0x10 01.., bulk 0x18) travel under model ID 0x45 -
		// the firmware's own dump builder patches 0x42 to 0x45 for exactly
		// those blocks (00:1476) - and the CPU dispatches a decoded transfer on
		// the address high byte alone, so they take the same bulk path.
		if(sx.size() < 6)
			return;
		const bool displayModel = sx[3] == 0x45 && sx.size() >= 7 && (sx[5] & 0xf0) == 0x10;
		if(sx[3] != 0x42 && !displayModel)
			return;

		if(sx[4] == 0x11)
		{
			if(sx[3] == 0x42)
				request(_source, sx);
			return;
		}
		if(sx[4] != 0x12)
			return;

		decodedTransfer(_source, sx);
	}

	// A Roland RQ1, F0 41 <dev> 42 11 <a1> <a2> <a3> <s1> <s2> <s3> <sum>.
	// The answer is entirely the CPU's: the sub-MCU names the parameter and
	// stages the request, and the firmware reads the value and queues the DT1
	// to the transmit ring itself.
	//
	// The record is a bulk one with bit 7 of the length set, which the ISR
	// takes as a request (SC-88 00:0d32, Pro 00:0d70). That path reads the
	// low address byte and the size from the staged bytes, which keep the
	// 41 <dev> 42 11 header in front:
	//   00:0df3  ADD.B   #H'06,R3      ; staged[6]: a3
	//   00:0df9  MOV.B   @R3+,R1
	//   00:0dfb  MOV.B   @R3+,R6       ; staged[7]: s1, 2 and up dropped
	//   00:0dfd  MOV.W   @R3,R4        ; staged[8..9]: s2 s3
	// and queues opcode / channel / 0x80 | a3 / size as an ordinary record,
	// with the size in a second word from 0x100 up (00:0e21). The opcode and
	// the channel nibble have to name a1 and a2, the way the firmware's own
	// transmitter reads them back to address the reply.
	//
	// The CPU looks the opcode up in its request table (SC-88 00:1663, table
	// 01:0D4C; Pro 00:18B4, table 01:10C4); the bulk blocks are answered by
	// the transmitter instead. Sizes 1 to 0x89 are answered, from the first
	// byte of a parameter on, in whole parameters; the CPU ignores the rest,
	// so the sub-MCU checks neither sizes nor parameter boundaries. The A/B
	// module is the CPU's choice too: 40 and 50 share a table entry, the
	// consumer picks the module from the opcode and the input source
	// (00:15ce) and the transmitter turns the reply address back for IN B
	// (00:11ac).
	void Sc88SubMcu::request(const uint8_t _source, const std::vector<uint8_t>& _sysEx)
	{
		constexpr size_t RQ1Size = 12;	// F0 through the checksum
		constexpr size_t AddrOffset = 5;
		if(_sysEx.size() != RQ1Size)
			return;

		uint8_t sum = 0;
		for(size_t i = AddrOffset; i < _sysEx.size(); ++i)
			sum = static_cast<uint8_t>(sum + _sysEx[i]);
		if((sum & 0x7f) != 0)
		{
			Record e;
			e.command   = OpSysExError;
			e.channel   = static_cast<uint8_t>(_source << 4);
			e.param1    = 3;	// Check Sum Error, as for a DT1
			e.wireBytes = static_cast<uint8_t>(RQ1Size + 1);
			m_sink(std::move(e));
			return;
		}

		const uint8_t middle = _sysEx[AddrOffset + 1];
		const uint8_t command = addressOpcode(_sysEx[AddrOffset], middle);
		if(!command)
			return;

		Record r;
		r.command   = command;
		r.channel   = static_cast<uint8_t>(0x80 | (_source << 4) | (middle & 0x0f));
		r.param1    = static_cast<uint8_t>(0x80 | (RQ1Size - 1));
		r.param2    = static_cast<uint8_t>(m_stageOffset);
		r.payload.assign(_sysEx.begin() + 1, _sysEx.end());
		r.wireBytes = static_cast<uint8_t>(RQ1Size + 1);	// and the F7
		m_sink(std::move(r));
	}

	void Sc88SubMcu::rawTransfer(const uint8_t _source, const uint8_t _command,
	                            const std::vector<uint8_t>& _sysEx)
	{
		const auto length = _sysEx.size() - 1; // manufacturer through final data/checksum
		if(length > std::min<size_t>(0x7f, m_stageCapacity))
			return;
		Record record;
		record.command = _command;
		record.channel = static_cast<uint8_t>(0x80 | (_source << 4));
		record.param1 = static_cast<uint8_t>(length);
		record.param2 = static_cast<uint8_t>(m_stageOffset);
		record.payload.assign(_sysEx.begin() + 1, _sysEx.end());
		record.wireBytes = static_cast<uint8_t>(_sysEx.size() + 1);
		m_sink(std::move(record));
	}

	void Sc88SubMcu::decodedTransfer(const uint8_t _source, const std::vector<uint8_t>& _sysEx)
	{
		const uint8_t src = static_cast<uint8_t>(_source << 4);

		// F0 41 <dev> 42 12 <a1> <a2> <a3> <data...> <sum> (the F7 terminated
		// the assembly and is not in the buffer).
		constexpr size_t DT1AddrOffset = 5;
		if(_sysEx.size() < DT1AddrOffset + 4)
			return;

		// The unit ignores a message whose checksum does not add up, and the
		// sub-MCU says so: a non-bulk record with opcode 0xE0, whose handler at
		// 00:1822 turns param1 into a panel notification. The code map was
		// established by sweeping the mailbox (--probe-midi-cmd, opcode 0xE0):
		//   0 = Hard Error, 1 = MIDI Off Line, 2 = MIDI Buff. Full,
		//   3 = Check Sum Error, 4 = No INSTRUMENT
		uint8_t sum = 0;
		for(size_t i = DT1AddrOffset; i < _sysEx.size(); ++i)
			sum = static_cast<uint8_t>(sum + _sysEx[i]);
		if((sum & 0x7f) != 0)
		{
			Record e;
			e.command   = OpSysExError;
			e.channel   = src;
			e.param1    = 3;	// Check Sum Error
			e.wireBytes = static_cast<uint8_t>(std::min<size_t>(_sysEx.size() + 1, 255));
			m_sink(std::move(e));
			return;
		}

		// Address + data + checksum. The checksum byte stays in: the firmware's
		// own byte count includes it (it is what makes the Display Letter
		// routine copy exactly the characters that were sent).
		const size_t body = _sysEx.size() - DT1AddrOffset;

		uint8_t command = _sysEx[DT1AddrOffset];	// address high byte
		uint8_t source = src;

		// The blocks below 0x10 use the 0x20 mailbox family. Below 0x20 the
		// CPU's bulk task takes only its display records, 0x10 and 0x12, and
		// drops any other opcode (SC-88 00:188F, Pro 00:1BF7); from 0x20 up it
		// hands the staged address to the parameter parser (SC-88 00:18DE ->
		// 00:3640, Pro 00:1C4A -> 00:5106), which handles those blocks itself:
		// 00 system - 00 00 7F (mode) and 00 01 00..1F (per-part input
		// assignment) - and 08 and 0C. On the Pro 0C 20 04 is the SC-88 mode
		// that SELECT+ALL toggles (00:5517, table 01:49EC, handler 00:559D).
		// Keep the staged address intact.
		if(command < 0x10)
			command = 0x20;

		// The main firmware routes patch/drum parameters to the A or B part
		// bank by the record's INPUT SOURCE alone — its apply path (00:4E18 on
		// the Pro) folds address blocks 0x50-0x5F onto the 0x40-0x4F handlers
		// with the module bit discarded, and a 0x40-block DT1 arriving on
		// MIDI IN B lands on the B parts. So the documented "module B" address
		// block (50 = Patch B, 51 = Drum B, 58/59 = bulk B — how the MIDI
		// Power Pro discs address B parts from IN A) has to be translated by
		// the sub-MCU into the module-A address tagged with the B source, or
		// it edits the A parts instead. The translated address byte changes
		// the running checksum, so the trailing sum byte is rebalanced.
		uint8_t addressDelta = 0;
		if(command >= 0x50 && command <= 0x5f)
		{
			command = static_cast<uint8_t>(command - 0x10);
			source = 1 << 4;	// MIDI IN B
			addressDelta = 0x10;
		}

		// Opcodes below 0xe0 carry a 4-byte sub-MCU header ahead of the payload,
		// and the length field counts 7 more than the firmware will parse:
		//   00:0d57  CMP:E.B #0xe0, R0
		//   00:0d59  BCC.B   0x0d64      ; >= 0xe0: take param1/param2 as given
		//   00:0d5b  SUB.B   #0x07, r6   ; else length -= 7
		//   00:0d5e  SUB.B   #0x04, r2   ; copy 4 fewer bytes...
		//   00:0d61  ADD.B   #0x04, r3   ; ...from 4 bytes further in
		// so the copy that reaches the parser is staged[4 .. 4+N).
		constexpr size_t HeaderBytes = 4;

		// param1 and param2 are both only SEVEN bits wide:
		//   00:0d00  TST.B  R3          ; param2 bit 7 -> a different path
		//   00:0d02  BMI.W  0x0dd2
		//   00:0d32  BCLR.B #7, R2      ; param1 bit 7 -> an RQ1, see request()
		//   00:0d34  BNE.W  0x0df3
		// so a transfer of more than 0x7f staged bytes has to be split, and
		// every chunk has to sit at the same low offset in the shared RAM. The
		// SC-88 demo files hit this constantly: their user-instrument dumps
		// (address blocks 0x48/0x49) stage 136 bytes, and losing them is why
		// the panel reports "No INSTRUMENT" for the tones they define.
		//
		// The firmware's own continuation protocol:
		//   * channel bit 6 = "more to come". 00:0d79 tests bit 14 of the
		//     opcode word (= channel bit 6) and 00:0d9b then saves the
		//     destination pointer and leaves without queueing the record.
		//   * follow-up chunks use opcode 0xe7, which enters at 00:0da2,
		//     restores that saved destination and adds to the running length.
		//   * being >= 0xe0, a continuation skips the header adjustment at
		//     00:0d57, so its param1 is an exact byte count and param2 points
		//     straight at the bytes.
		constexpr size_t MaxChunk = 0x7f;
		const size_t chunkMax = std::min(MaxChunk, static_cast<size_t>(m_stageCapacity));

		const size_t len = HeaderBytes + body;

		// 00:0daa abandons the transfer once the accumulated count reaches
		// 0x8a, and the first chunk contributes len-7 of it, so the unit
		// itself cannot take more than this either.
		if(len > 0x8a + 7 - 1)
			return;

		// Header + address + data + checksum, as one block to be handed over
		// in chunks. Only the first chunk carries the header.
		std::vector<uint8_t> staged;
		staged.reserve(len);
		staged.insert(staged.end(), HeaderBytes, uint8_t(0));
		staged.insert(staged.end(), _sysEx.begin() + DT1AddrOffset, _sysEx.end());
		if(addressDelta)
		{
			staged[HeaderBytes] = command;
			staged.back() = static_cast<uint8_t>((staged.back() + addressDelta) & 0x7f);
		}

		size_t sent  = 0;
		bool   first = true;

		while(sent < staged.size())
		{
			const size_t take = std::min(staged.size() - sent, chunkMax);

			Record r;
			// Bit 7 of the channel byte selects the bulk path. The ISR tests
			// it at
			//   00:0c14  SWAP.B R0
			//   00:0c16  BMI.W  0x0cf0
			// and only that branch reaches the payload copy loop; without it
			// the record is queued as a bare 4-byte message and the payload is
			// ignored. Bits 4-5 are the input source.
			r.command = first ? command : OpSysExContinue;
			r.channel = static_cast<uint8_t>(0x80 | source | (sent + take < staged.size() ? 0x40 : 0x00));
			r.param1  = static_cast<uint8_t>(take);
			r.param2  = static_cast<uint8_t>(m_stageOffset);
			r.payload.assign(staged.begin() + sent, staged.begin() + sent + take);
			// Paced by its wire cost: that is both what the wire does and what
			// gives the ISR time to copy a chunk out before the next
			// overwrites it. The staged block is not what went down the cable
			// though - the five-byte preamble replaced the four-byte header
			// and the F7 is not staged at all - so the first and the last
			// chunk carry those. Under-counting them delivers a burst of
			// short DT1s a fifth faster than 31250 baud allows, which is
			// enough to drain the firmware's ten-buffer pool while it is
			// still busy with a GS reset; the exhausted path (00:0cb5, R2
			// odd) then dies of an address error.
			size_t wire = take;
			if(first)
				wire += DT1AddrOffset - HeaderBytes;
			if(sent + take >= staged.size())
				++wire;	// F7
			r.wireBytes = static_cast<uint8_t>(std::min<size_t>(wire, 255));
			m_sink(std::move(r));

			sent += take;
			first = false;
		}
	}
}

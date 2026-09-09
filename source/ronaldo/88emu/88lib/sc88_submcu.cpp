#include "sc88_submcu.h"

#include <algorithm>

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
	}

	Sc88SubMcu::Sc88SubMcu(Sink _sink, const uint16_t _stageOffset, const uint16_t _stageCapacity)
		: m_sink(std::move(_sink))
		, m_stageOffset(_stageOffset)
		, m_stageCapacity(_stageCapacity)
	{
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
	// Opcodes 1/2 follow the NukedSC55 reference. Panel observations confirm
	// 4/5; 3/6/7 follow the encoding but have no panel-visible confirmation.
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
		const uint8_t src = static_cast<uint8_t>(_source << 4);

		if(sx.size() < 3)
			return;

		// The two universal messages skip the bulk path entirely — each is
		// reduced to one value and posted as a plain 4-byte record.
		if(sx[1] == 0x7e || sx[1] == 0x7f)
		{
			Record u;
			if(sx[1] == 0x7e && sx.size() >= 5 && sx[3] == 0x09)
				u.command = OpSysExUniNonRt, u.param1 = sx[4];
			else if(sx[1] == 0x7f && sx.size() >= 7 && sx[3] == 0x04 && sx[4] == 0x01)
				u.command = OpSysExUniRt, u.param1 = sx[6];	// MSB; the LSB is unused
			else
				return;

			// Non-bulk: channel bit 7 clear, so the record is taken as is.
			u.channel   = src;
			u.wireBytes = static_cast<uint8_t>(std::min<size_t>(sx.size() + 1, 255));
			m_sink(std::move(u));
			return;
		}

		// Only Roland is decoded further.
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

		// RQ1 (0x11) is deliberately *not* forwarded. The mailbox record for a
		// decoded transfer has no field that distinguishes a request from a
		// write — the sub-MCU header that might carry one is never read by the
		// CPU (verified: only 0F:0014+ is, from the copy loop at 00:0d6c) — so
		// forwarding an RQ1 through this path makes the firmware apply the
		// request's *size* bytes as parameter data. Dropping it is correct
		// until the reply path is found; the unit cannot answer anyway.
		if(sx[4] != 0x12)
			return;

		decodedTransfer(_source, sx);
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
		//   00:0d32  BCLR.B #7, R2      ; param1 bit 7 -> continuation chunk
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

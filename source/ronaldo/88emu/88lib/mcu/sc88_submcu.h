#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

#include "synthLib/midiBufferParser.h"

namespace emu88Lib
{
	// High-level model of the SC-88 / SC-88Pro MIDI sub-MCU.
	//
	// The main CPU never sees a MIDI byte: a separate MCU parses the two DIN
	// inputs and the COMPUTER port and hands the H8 decoded records through
	// the page-0x0F mailbox. This class is that MCU's
	// interface — it consumes raw stream bytes per input source, keeps the
	// per-source running-status and SysEx assembly state the real chip has to
	// keep, and emits complete mailbox records through a sink. The mailbox
	// mechanics themselves (staging RAM, IRQ2, the ack-on-read handshake and
	// receive pacing) stay in the board, which owns the shared memory. The
	// transmit ring is consumed here at DIN MIDI speed.
	//
	// What the record shapes reproduce, all read out of the H8 ISR:
	//   * channel voice: opcode = (status >> 4) - 7, params = the data bytes
	//   * Roland DT1: a decoded transfer — opcode = address high byte, payload
	//     staged in shared RAM with the 4-byte sub-MCU header, split into
	//     <= 0x7f chunks with 0xE7 continuations (both pointers are 7-bit)
	//   * universal GM System On / Master Volume: bare records 0xEE / 0xEF
	//   * a DT1 whose checksum does not add up: record 0xE0, which is where
	//     the firmware's "Check Sum Error" notification comes from
	//   * RQ1: ignored until the request command mapping is verified
	//   * Yamaha XG parameter changes: raw header under opcode 0xEC
	class Sc88SubMcu
	{
	public:
		// One mailbox record, in exactly the form the ISR at 00:0bc2 reads.
		struct Record
		{
			uint8_t wireBytes = 3;	// how long this took to arrive on the wire
			uint8_t command = 0;	// mailbox opcode
			uint8_t channel = 0;	// bits 0-3 channel, 4-5 input source, 6 more-
									// to-come, 7 bulk path
			uint8_t param1  = 0;	// data 1, or staged payload length for bulk
			uint8_t param2  = 0;	// data 2, or shared-RAM pointer for bulk

			// Bulk payload, staged into the shared RAM when this record is
			// posted rather than up front: param2 is only seven bits wide
			// (00:0d00 diverts a pointer with bit 7 set), so every chunk of a
			// split transfer has to be placed at the same low offset, one at a
			// time. Empty for ordinary channel messages.
			std::vector<uint8_t> payload;
		};

		using Sink = std::function<void(Record&&)>;

		static constexpr uint8_t SourceCount = 3;	// IN A, IN B, COMPUTER

		// _stageOffset/_stageCapacity describe the shared-RAM window the board
		// gives bulk payloads (clear of the mailbox registers).
		Sc88SubMcu(Sink _sink, uint16_t _stageOffset, uint16_t _stageCapacity);
		void reset();

		static constexpr uint8_t TxRead = 0xd4;
		static constexpr uint8_t TxWrite = 0xd5;
		using SharedRam = std::array<uint8_t, 0x100>;
		void startOutput(SharedRam& _ram);
		void commitOutput(const SharedRam& _ram, uint8_t _write);
		void clockOutput(SharedRam& _ram, uint32_t _sampleRate);
		void readMidiOut(std::vector<synthLib::SMidiEvent>& _events) { m_output.getEvents(_events); }

		// One raw wire byte from an input source. Complete messages come out
		// through the sink.
		void midiIn(uint8_t _source, uint8_t _byte);

	private:
		// Per-source parser state — three independent streams on the real
		// hardware, each with its own running status and SysEx assembly.
		struct Source
		{
			uint8_t runningStatus = 0;
			uint8_t data[2] = {0, 0};
			uint8_t have = 0;
			uint8_t bytesThisMessage = 0;	// wire cost incl. running-status omission
			bool inSysEx = false;
			bool sysExOverrun = false;
			std::vector<uint8_t> sysEx;
		};

		void voiceMessage(uint8_t _source, Source& _s);
		void endOfSysEx(uint8_t _source, Source& _s);
		void rawTransfer(uint8_t _source, uint8_t _command, const std::vector<uint8_t>& _sysEx);
		void decodedTransfer(uint8_t _source, const std::vector<uint8_t>& _sysEx);

		struct OutputPacket
		{
			std::vector<uint8_t> bytes;
			uint8_t end = 0;
			size_t offset = 0;
		};
		std::deque<OutputPacket> m_outputQueue;
		synthLib::MidiBufferParser m_output{synthLib::MidiEventSource::Device};
		uint32_t m_outputPhase = 0;
		uint32_t m_outputPause = 0;

		Sink m_sink;
		const uint16_t m_stageOffset;
		const uint16_t m_stageCapacity;
		std::array<Source, SourceCount> m_sources;
	};
}

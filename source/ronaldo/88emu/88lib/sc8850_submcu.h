#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

namespace emu88Lib
{
	// High-level model of the SC-8850's M37640E8 UIPC/USB controller.
	//
	// The original MCU owns the USB device endpoint and exchanges tagged bytes
	// with the SH7016 through two mailbox channels. Its downloaded 32 KiB
	// program implements USB-MIDI, so executing the M740 instruction set would
	// add no useful synthesis behaviour. This class preserves the observable
	// boot-loader, mailbox and USB-MIDI packet behaviour instead.
	class Sc8850SubMcu
	{
	public:
		using InterruptCallback = std::function<bool()>;

		static constexpr size_t ProgramSize = 0x8000;
		static constexpr uint32_t EnumerationDelayTicks = 4410;

		// The SC-8850's USB-MIDI interface exposes four virtual cables, one
		// per 16-part group A-D. The cable number rides in the high nibble of
		// a USB-MIDI event packet's first byte.
		static constexpr size_t CableCount = 4;

		Sc8850SubMcu();

		void reset();
		uint8_t hostRead(uint32_t _address);
		void hostWrite(uint32_t _address, uint8_t _value);

		void midiIn(uint8_t _value);
		void midiIn(const uint8_t* _data, size_t _size);
		void midiIn(uint8_t _cable, const uint8_t* _data, size_t _size);
		void readMidiOut(std::vector<uint8_t>& _output);
		void tick();

		// Retry level-sensitive mailbox notifications after the SH enables the
		// corresponding gate-array source.
		void kickInterrupts();
		void requestTransmit();

		bool bootComplete() const { return m_bootState == BootState::Runtime; }
		bool ready() const { return m_ready; }
		size_t inputBacklog() const { return m_toHost.size() + m_pendingPackets.size(); }
		const std::vector<uint8_t>& uploadedProgram() const { return m_uploadedProgram; }

		InterruptCallback receiveInterrupt;
		InterruptCallback transmitInterrupt;

	private:
		struct MailboxByte
		{
			uint8_t tag = 0;
			uint8_t value = 0;
			bool packetStart = false;
		};

		enum class BootState : uint8_t
		{
			VersionHigh,
			VersionLow,
			Program,
			Configuration,
			FinalReply,
			Runtime
		};

		// Each virtual cable is an independent MIDI stream with its own
		// running status and SysEx assembly, exactly as on the wire.
		struct CableParser
		{
			bool inSysex = false;
			std::array<uint8_t, 3> sysexChunk{};
			uint8_t sysexChunkSize = 0;
			uint8_t runningStatus = 0;
			uint8_t messageStatus = 0;
			std::array<uint8_t, 2> messageData{};
			uint8_t messageDataSize = 0;
			uint8_t messageDataExpected = 0;
		};

		void enqueueBoot(uint8_t _tag, uint8_t _value);
		void enqueueStatus(uint8_t _value);
		void enterRuntime();
		void enqueueUsbPacket(const std::array<uint8_t, 4>& _packet);
		void emitInputPacket(uint8_t _cable, uint8_t _cin, uint8_t _a, uint8_t _b = 0, uint8_t _c = 0);
		void consumeOutputPacket();
		void consumeMidiByte(uint8_t _cable, uint8_t _value);
		void finishMidiMessage(uint8_t _cable);
		void finishSysex(uint8_t _cable);
		static uint8_t midiDataLength(uint8_t _status);
		static uint8_t usbMidiLength(uint8_t _cin);

		BootState m_bootState = BootState::VersionHigh;
		std::deque<MailboxByte> m_toHost;
		std::deque<std::array<uint8_t, 4>> m_pendingPackets;
		std::vector<uint8_t> m_uploadedProgram;
		std::vector<uint8_t> m_midiOut;
		std::array<uint8_t, 4> m_outputPacket{};
		uint8_t m_outputPacketSize = 0;
		uint8_t m_configuration = 0;
		bool m_transmitIrqPending = false;
		bool m_receiveRequestOutstanding = false;
		bool m_transmitRequestOutstanding = false;
		uint32_t m_enumerationDelay = 0;
		bool m_ready = false;

		std::array<CableParser, CableCount> m_cables{};
	};
}

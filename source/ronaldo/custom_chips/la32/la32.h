/*
 * la32Lib — the LA32 partial synthesizer (Roland R15229896 / Fujitsu MB87136A).
 *
 * Derived from Nuked-MT32's la32.cpp/la32.h, Copyright (C) 2024, 2025 nukeykt,
 * GPL-2.0-or-later; see la32.cpp for the full notice.
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace la32Lib
{
	// The chip's internal lookup ROMs, tables filled from their closed forms when the first
	// chip is constructed; see la32.cpp.
	uint32_t exp(uint32_t _index);	// 0..512
	uint32_t logsin(uint32_t _index);	// 0..511

	// One envelope ramp as the host programs it. r0 is ramp A, r2 is ramp B: on a synth partial
	// A offsets the cutoff and B the amplitude, on a PCM partial A scales the first wave and B
	// the second. Packed: target in [15:8], down in [7], rate in [6:0].
	struct Ramp
	{
		uint8_t target = 0;	// level the ramp runs to, compared against the counter's top 8 bits
		bool down = false;	// direction; a ramp pointed away from its target completes at once
		uint8_t rate = 0;	// exponent [6:3], mantissa [2:0]; 0 holds the level and raises no event
	};

	// One PCM read's window, a nibble of r4: size in [2:0], loop in [3].
	struct WaveWindow
	{
		uint8_t sizeLog2 = 0;	// length as log2 of 2048-sample pages
		bool loop = false;		// wrap instead of ending with a boundary event
	};

	// The host-visible registers of one partial, unpacked from the five packed words r0-r4.
	struct VoiceRegisters
	{
		std::array<Ramp, 2> ramp{};	// r0 = ramp A, r2 = ramp B

		// r3: the pitch, a straight log2. An octave of 0xF marks the partial inactive.
		uint8_t octave = 0;		// [15:12]
		uint16_t fraction = 0;	// [11:0], in 1/4096 octave

		// r4 low byte, meaning the same for both partial types
		bool isPcm = false;		// [7]
		bool ring = false;		// [5] output becomes the product with the previous slot's output
		uint8_t outputPair = 0;	// [4:3] which of the four stereo bus pairs to add to
		uint8_t pan = 0;		// [2:0] left gain (pan * 73) >> 2 of 128; 7 also mutes the right side

		// r1 and the high byte of r4 mean different things per partial type. Both views are
		// decoded from the same words, so only the one matching isPcm is meaningful.
		struct Pcm
		{
			std::array<uint8_t, 2> page{};	// r1 [15:8] wave 1 page, [7:0] wave 2 page, 4 KiB units
			bool interpolate = false;		// r4 [6] one wave read at two adjacent samples, else two waves
			std::array<WaveWindow, 2> wave{};	// r4 [15:12] wave 1, [11:8] wave 2
		} pcm;

		struct Synth
		{
			uint8_t pulseWidth = 0;		// r1 [7:0]
			uint8_t cutoff = 0;			// r1 [15:8]
			bool sawtooth = false;		// r4 [6], else square
			uint8_t resonance = 0;		// r4 [12:8]
			uint8_t resonanceDecay = 0;	// r4 [15:13], selects how fast the resonant peak decays
		} synth;
	};

	// What the chip keeps per partial between slots.
	struct VoiceState
	{
		std::array<uint32_t, 2> rampCounter{};	// 26 bits each, level in [25:18]
		uint32_t phase = 0;						// 26 bits; PCM sample position is phase >> 8
		std::array<bool, 2> pcmEnded{};			// a one-shot read ran past its window and is muted
	};

	struct Voice
	{
		VoiceRegisters regs;
		VoiceState state;
	};

	// The three modulation offsets a group of eight slots shares, in the order of the r5 words.
	enum ModulationIndex : size_t
	{
		ModPulseWidth,
		ModCutoff,
		ModAmplitude,
	};

	// r5 is not per partial: each group of eight slots owns eight words. The host writes three
	// modulation targets into words 0-2; the chip low-passes them into words 5-7 and applies the
	// result to every synth partial of the group. Words 3 and 4 are unused.
	struct ModulationGroup
	{
		std::array<uint16_t, 3> target{};	// pulse width, cutoff, amplitude offsets
		std::array<uint16_t, 3> state{};	// the smoothed values, 15 bits, rewritten by the chip
	};

	// The four global registers at 1C0h-1C3h.
	struct Config
	{
		// 1C0h, two bits per group of eight slots. In scheduling mode 0 they set how many
		// adjacent slots share one active flag: 0 = each its own, 1 = pairs, 2 = quads, 3 = all
		// eight. Bit 1 also forces the flag to come from the pitch register in the other modes.
		std::array<uint8_t, 4> grouping{};
		uint8_t scheduleMode = 0;	// 1C1h [5:4]; the MT-32 family only uses 0
		uint8_t activeSource = 0;	// 1C1h [3:2]; source of the active flag in modes 1-3
		bool pcmFormat = false;		// 1C1h [6]; decode the 14-bit log sample format
		uint8_t rampBPreset = 0;	// 1C3h [7:4]; level ramp B starts from when a partial switches on
	};

	struct PcmSample;

	// What one frame of a ramp yields: the 14-bit level the synthesis uses, and the event.
	struct RampStep
	{
		uint32_t level;
		bool reached;
	};

	class LA32
	{
	public:
		using Outputs = std::array<int32_t, 8>;
		using IrqCallback = std::function<void(bool)>;

		LA32();

		void reset();
		void setPcmRom(std::vector<uint8_t> _rom);
		void setPcmRom(const uint8_t* _data, size_t _size);
		void setRomAddressXor(uint32_t _mask);
		// The address range the PCM ROM answers on; reads outside it return _openBus (the D-550
		// maps its 512 KiB ROM at 40000-BFFFF and its pulled-up bus reads FFFF there, a full-scale
		// negative sample).
		void setRomWindow(uint32_t _base, uint32_t _size, uint16_t _openBus = 0xffff);
		void setIrqCallback(IrqCallback _callback);

		uint8_t read(uint32_t _offset) const;
		// A high-byte write commits the word and acknowledges INT: the line drops at cycle
		// _lineCycle of the current frame (32 and above: the next frame) and the event latch is
		// free again from _guardCycle on; (0, 0) is immediate. A negative line cycle leaves the
		// acknowledge to a separate acknowledgeIrq() call by a host that knows the access time.
		void write(uint32_t _offset, uint8_t _data, int32_t _lineCycle = 0, uint32_t _guardCycle = 0);
		void acknowledgeIrq(uint32_t _lineCycle = 0, uint32_t _guardCycle = 0);
		// A write from the CPU while the frame is being stepped slot by slot (the board runs the
		// CPU for slot k's time before stepSlot() computes slot k): the word is seen from the
		// next cycle on, except that one written during cycle 31 is not seen by cycle 0 of the
		// next frame but from cycle 1; INT drops on the next cycle and the event latch stays
		// taken for three (measured on the D-550 bus).
		void writeFromBus(uint32_t _offset, uint8_t _data);
		uint32_t cycle() const { return m_cycle; }
		void stepSlot();
		Outputs currentOutput() const;
		Outputs renderSample();

		uint8_t sh3() const { return static_cast<uint8_t>((m_cycle >> 4) & 1); }
		bool irqPending() const { return m_irqLine; }
		const Config& config() const { return m_config; }
		const Voice& voice(const unsigned _slot) const { return m_voices[_slot & 31]; }
		const ModulationGroup& modulationGroup(const unsigned _group) const { return m_modulation[_group & 3]; }

		// The slot scheduling decisions, pure so the tests can check them exhaustively.
		static uint8_t nextInactiveHistory(uint8_t _history, uint32_t _slot, const Config& _config, bool _pitchInactive);
		static bool signFlip(uint32_t _slot, uint8_t _scheduleMode);

		// The conversion layer between the packed bus words and the structures above.
		static void unpackVoiceRegister(VoiceRegisters& _regs, unsigned _register, uint16_t _word);
		static uint16_t packVoiceRegister(const VoiceRegisters& _regs, unsigned _register);

	private:
		void writeControlRegister(unsigned _index, uint8_t _data);
		void writeModulationWord(unsigned _slot, uint16_t _word);
		void updateInactive();
		void updateSlot();
		static RampStep stepRamp(const Ramp& _ramp, uint32_t& _counter, bool _inactive, uint32_t _inactiveLevel);
		int32_t computeVoicePcm(Voice& _voice, const uint32_t (&_tv)[2], uint32_t _phase, bool _signFlip);
		int32_t computeVoiceSynth(const VoiceRegisters& _regs, const uint32_t (&_tv)[2], uint32_t _phase, bool _signFlip);
		int32_t mixWaves(uint32_t _w1, uint32_t _w2, bool _ring);
		void addToBus(int32_t _value, uint8_t _pan, uint8_t _pair);
		PcmSample readSample(uint32_t _address, bool _muted) const;
		void queuePcmEvent(bool _ended1, bool _ended2);
		uint8_t readPcm(uint32_t _address) const;
		void setIrq(bool _state);
		void commitWord(uint32_t _offset, uint16_t _word);

		std::vector<uint8_t> m_pcmRom;
		IrqCallback m_irqCallback;
		uint32_t m_romAddressXor = 0;
		uint32_t m_romWindowBase = 0;
		uint32_t m_romWindowSize = 0;		// 0: the ROM mirrors over the whole address space
		uint16_t m_romOpenBus = 0xffff;

		std::array<Voice, 32> m_voices{};
		std::array<ModulationGroup, 4> m_modulation{};
		Config m_config;

		uint32_t m_cycle = 0;				// the slot being computed, 0-31
		uint8_t m_lowByteLatch = 0;			// the low byte of the word being written
		struct PendingWrite
		{
			uint32_t offset;
			uint16_t word;
			uint8_t slotsToGo;	// stepSlot() calls before the word is seen
		};
		std::array<PendingWrite, 8> m_pendingWrites{};
		size_t m_pendingWriteCount = 0;
		uint8_t m_inactiveHistory = 0;		// active flags of the last eight slots, bit 0 = current
		int32_t m_prevOutput = 0;			// the previous slot's output, for ring modulation
		std::array<uint16_t, 3> m_groupMod{};	// the current group's smoothed r5 words, latched
		Outputs m_summing{};				// the frame being summed
		Outputs m_finished{};				// the last completed frame
		bool m_irqLine = false;		// the INT pin
		bool m_irqPending = false;	// the event latch is taken (stays set a few cycles past the line release)
		bool m_ackPending = false;	// an acknowledge waits for m_ackCycle
		uint32_t m_ackCycle = 0;
		uint32_t m_ackLineCycle = 0;
		uint8_t m_irqStatus = 0;
	};
}

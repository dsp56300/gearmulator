// LA32 chip core, based on the unfinished one by nukeykt

#include "la32.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace la32Lib
{
	template<typename T> static bool bit(const T _value, const unsigned _bit)
	{
		return ((_value >> _bit) & 1) != 0;
	}

	// The low _bits of _value as a signed number.
	static int32_t signExtend(const uint32_t _value, const unsigned _bits)
	{
		return static_cast<int32_t>(_value << (32 - _bits)) >> (32 - _bits);
	}

	// The chip has no negator: it inverts every bit of a _bits wide value on its way to the
	// adder. The result is the one's complement, -x - 1, one LSB below the true negative, and
	// with a carry-in of 1 it becomes an exact subtraction.
	static uint32_t negated(const uint32_t _value, const unsigned _bits)
	{
		const uint32_t mask = _bits >= 32 ? 0xffffffffu : (1u << _bits) - 1;
		return _value ^ mask;
	}

	// The log-sine ROM stores attenuations; the level a sine contributes is the complement.
	static uint32_t sineLevel(const uint32_t _logsin)
	{
		return _logsin ^ 0x3fff;
	}

	uint32_t logsin(const uint32_t _index)
	{
		constexpr double pi = 3.14159265358979323846;
		const double v = std::floor(0.5 - std::log2(std::sin((_index + 0.5) / 1024.0 * pi)) * 1024.0);
		return static_cast<uint32_t>(std::min(v, 8191.0));
	}


LA32::LA32()
{
	reset();
}

void LA32::reset()
{
	m_voices = {};
	m_modulation = {};
	m_config = {};
	m_cycle = 0;
	m_lowByteLatch = 0;
	m_inactiveHistory = 0;
	m_prevOutput = 0;
	m_groupMod.fill(0);
	m_summing.fill(0);
	m_finished.fill(0);
	m_irqStatus = 0;
	setIrq(false);
}

void LA32::setPcmRom(std::vector<uint8_t> _rom)
{
	m_pcmRom = std::move(_rom);
}

void LA32::setPcmRom(const uint8_t* const _data, const size_t _size)
{
	m_pcmRom.assign(_data, _data + _size);
}

void LA32::setRomAddressXor(const uint32_t _mask)
{
	m_romAddressXor = _mask & 0xfffff;
}

void LA32::setIrqCallback(IrqCallback _callback)
{
	m_irqCallback = std::move(_callback);
}

uint8_t LA32::readPcm(const uint32_t _address) const
{
	if (m_pcmRom.empty())
		return 0xff;
	return m_pcmRom[(_address ^ m_romAddressXor) % m_pcmRom.size()];
}

void LA32::setIrq(const bool _state)
{
	m_irqPending = _state;
	if (m_irqCallback)
		m_irqCallback(_state);
}

void LA32::stepSlot()
{
	updateSlot();
}

LA32::Outputs LA32::currentOutput() const
{
	Outputs result{};
	for (size_t output = 0; output < result.size(); ++output)
		result[output] = std::clamp(m_finished[output], -32768, 32767);
	return result;
}

LA32::Outputs LA32::renderSample()
{
	for (uint32_t slot = 0; slot < 32; ++slot)
		stepSlot();

	return currentOutput();
}

// Address bits [8:6] select a register block, [5:1] the slot and [0] the byte half. The low
// byte is latched, the high byte commits the word and acknowledges the interrupt. Block 7 is
// the four control bytes, block 6 exists on the bus but stores nothing.
void LA32::write(const uint32_t _offset, const uint8_t _data)
{
	if ((_offset & 0x1c0) == 0x1c0)
	{
		writeControlRegister(_offset & 3, _data);
		return;
	}

	if ((_offset & 1) == 0)
	{
		m_lowByteLatch = _data;
		return;
	}

	const uint32_t reg = (_offset >> 6) & 7;
	const uint32_t slot = (_offset >> 1) & 31;
	const auto word = static_cast<uint16_t>((_data << 8) | m_lowByteLatch);

	if (reg < 5)
		unpackVoiceRegister(m_voices[slot].regs, reg, word);
	else if (reg == 5)
		writeModulationWord(slot, word);

	if (m_irqPending)
		setIrq(false);
}

uint8_t LA32::read(const uint32_t _offset) const
{
	if ((_offset & 0x1c0) != 0x1c0)
		return m_irqStatus;
	return 0xff;
}

void LA32::writeControlRegister(const unsigned _index, const uint8_t _data)
{
	switch (_index)
	{
		case 0:
			for (unsigned group = 0; group < 4; ++group)
				m_config.grouping[group] = (_data >> (group * 2)) & 3;
			break;
		case 1:
			m_config.scheduleMode = (_data >> 4) & 3;
			m_config.activeSource = (_data >> 2) & 3;
			m_config.pcmFormat = (_data & 0x40) != 0;
			break;
		case 2:
			// Accepted, nothing in the chip reads it back.
			break;
		case 3:
			m_config.rampBPreset = _data >> 4;
			break;
	}
}

void LA32::writeModulationWord(const unsigned _slot, const uint16_t _word)
{
	auto& group = m_modulation[(_slot >> 3) & 3];
	const unsigned index = _slot & 7;
	if (index < 3)
		group.target[index] = _word;
	else if (index >= 5)
		group.state[index - 5] = _word;
}

void LA32::unpackVoiceRegister(VoiceRegisters& _regs, const unsigned _register, const uint16_t _word)
{
	const auto unpackRamp = [](const uint16_t _w)
	{
		Ramp r;
		r.target = static_cast<uint8_t>(_w >> 8);
		r.down = (_w & 0x80) != 0;
		r.rate = _w & 0x7f;
		return r;
	};
	const auto unpackWindow = [](const uint32_t _nibble)
	{
		WaveWindow w;
		w.sizeLog2 = _nibble & 7;
		w.loop = (_nibble & 8) != 0;
		return w;
	};

	switch (_register)
	{
		case 0:
			_regs.ramp[0] = unpackRamp(_word);
			break;
		case 1:
			_regs.pcm.page[0] = static_cast<uint8_t>(_word >> 8);
			_regs.pcm.page[1] = static_cast<uint8_t>(_word);
			_regs.synth.cutoff = static_cast<uint8_t>(_word >> 8);
			_regs.synth.pulseWidth = static_cast<uint8_t>(_word);
			break;
		case 2:
			_regs.ramp[1] = unpackRamp(_word);
			break;
		case 3:
			_regs.octave = _word >> 12;
			_regs.fraction = _word & 0xfff;
			break;
		case 4:
			_regs.isPcm = (_word & 0x80) != 0;
			_regs.ring = (_word & 0x20) != 0;
			_regs.outputPair = (_word >> 3) & 3;
			_regs.pan = _word & 7;
			_regs.pcm.interpolate = (_word & 0x40) != 0;
			_regs.pcm.wave[0] = unpackWindow(_word >> 12);
			_regs.pcm.wave[1] = unpackWindow((_word >> 8) & 15);
			_regs.synth.sawtooth = (_word & 0x40) != 0;
			_regs.synth.resonance = (_word >> 8) & 31;
			_regs.synth.resonanceDecay = (_word >> 13) & 7;
			break;
	}
}

uint16_t LA32::packVoiceRegister(const VoiceRegisters& _regs, const unsigned _register)
{
	const auto packRamp = [](const Ramp& _r)
	{
		return static_cast<uint16_t>((_r.target << 8) | (_r.down ? 0x80 : 0) | (_r.rate & 0x7f));
	};

	switch (_register)
	{
		case 0: return packRamp(_regs.ramp[0]);
		case 1: return static_cast<uint16_t>((_regs.synth.cutoff << 8) | _regs.synth.pulseWidth);
		case 2: return packRamp(_regs.ramp[1]);
		case 3: return static_cast<uint16_t>((_regs.octave << 12) | (_regs.fraction & 0xfff));
		case 4:
			return static_cast<uint16_t>((_regs.synth.resonanceDecay << 13) | (_regs.synth.resonance << 8)
				| (_regs.isPcm ? 0x80 : 0) | (_regs.synth.sawtooth ? 0x40 : 0) | (_regs.ring ? 0x20 : 0)
				| (_regs.outputPair << 3) | (_regs.pan & 7));
		default: return 0;
	}
}

// The chip's multiplier: a signed 14-bit value times a signed 8-bit one.
static int32_t mul14x8(const int32_t _a, const int32_t _b)
{
	return signExtend(static_cast<uint32_t>(_a), 14) * signExtend(static_cast<uint32_t>(_b), 8);
}

// The exp ROM as the chip interpolates it: 4096 * 2^(x/512), rounded, so the mantissa carries
// its leading bit and index 512 yields the 8192 the interpolation past the last entry runs into.
inline uint32_t exp(const uint32_t _index)
{
	return static_cast<uint32_t>(std::floor(4096.0 * std::exp2(_index / 512.0) + 0.5));
}

// 2^(v/4096) scaled by 8, in 26 bits: a 4-bit integer part over a 12-bit fraction, the
// fraction resolved by the exp ROM with linear interpolation over its three low bits.
static uint32_t exp2Fixed(const uint32_t _v)
{
	const uint32_t index = (_v >> 3) & 511;
	const uint32_t integer = (_v >> 12) & 15;
	const uint32_t low = _v & 7;

	const uint32_t m0 = exp(index);
	const uint32_t mantissa = m0 + (((exp(index + 1) - m0) * low) >> 3);

	return (mantissa << integer) >> 9;
}

// The active flags live in an eight-bit history that normally shifts once per slot, taking the
// slot's own pitch register (octave F = inactive) as the new bit. The configuration can slow the
// shift to every 2^k slots, so a run of adjacent slots shares one flag, and in scheduling modes
// 1-3 part of the frame replays the history instead of looking at the pitch register.
uint8_t LA32::nextInactiveHistory(const uint8_t _history, const uint32_t _slot, const Config& _config, const bool _pitchInactive)
{
	const uint32_t grouping = _config.grouping[(_slot >> 3) & 3];
	const uint32_t source = _config.activeSource;

	// The slots that take their flag from the pitch register.
	bool direct = true;
	switch (_config.scheduleMode)
	{
		case 1: direct = (_slot & 0x18) == 0; break;
		case 2: direct = (_slot & 0x08) == 0; break;
		case 3: direct = (_slot & 0x10) == 0; break;
		default: break;
	}

	uint32_t periodLog2;
	uint32_t replayTap = 0;	// 0 = the pitch register, else how many slots back the history is read
	if (direct)
	{
		switch (_config.scheduleMode)
		{
			case 0: periodLog2 = grouping; break;
			case 3: periodLog2 = (source + 1) & 3; break;
			default: periodLog2 = source; break;
		}
	}
	else
	{
		if (source == 3)
			return _history;	// the history holds
		periodLog2 = source;
		static constexpr uint32_t taps[] = {7, 3, 1, 0};
		replayTap = (grouping & 2) ? 0 : taps[source];
	}

	if ((_slot & ((1u << periodLog2) - 1)) != 0)
		return _history;

	const bool bit = replayTap ? ((_history >> replayTap) & 1) != 0 : _pitchInactive;
	return static_cast<uint8_t>((_history << 1) | (bit ? 1 : 0));
}

void LA32::updateInactive()
{
	m_inactiveHistory = nextInactiveHistory(m_inactiveHistory, m_cycle, m_config, m_voices[m_cycle].regs.octave == 0xf);
}

// Whether a slot's waveforms are inverted. The pattern repeats every four slots, two slots
// ahead of the slot counter; scheduling mode 3 alters it at the frame's ends.
bool LA32::signFlip(const uint32_t _slot, const uint8_t _scheduleMode)
{
	const uint32_t slot = (_slot + 2) & 31;
	const bool bit1 = (slot & 2) != 0;
	const bool bit2 = (slot & 4) != 0;
	if (_scheduleMode == 3 && (slot < 2 || slot >= 18))
		return !bit1;
	return !(bit1 ^ bit2);
}

// ---- Log-domain arithmetic --------------------------------------------------------------------
//
// Levels, gains and sample magnitudes travel through the chip as 14-bit log values with 3FFFh as
// unity and 0 as silence. The silicon combines them on a 14-bit adder and looks at its carry.

// One adder result: the low 14 bits and the carry out.
struct Sum14
{
	uint32_t low;
	bool carry;
};

static Sum14 add14(const uint32_t _a, const uint32_t _b, const uint32_t _carryIn)
{
	const uint32_t sum = _a + _b + _carryIn;
	return {sum & 0x3fff, ((sum >> 14) & 1) != 0};
}

// The sum only when it carried, silence otherwise.
static uint32_t gated(const Sum14& _sum)
{
	return _sum.carry ? _sum.low : 0;
}

// A log-domain multiply: a + b + 1 - 4000h, clipped at silence.
static uint32_t logMul(const uint32_t _a, const uint32_t _b)
{
	return gated(add14(_a, _b, 1));
}

// A result too small for its sign bit to be applied: the sign flip is skipped for these.
static bool tooQuietForSign(const Sum14& _sum)
{
	return (_sum.low & 0x3800) == 0 || !_sum.carry;
}

// Two offset-binary values (2000h = zero) summed on the adder, back to offset binary with
// saturation at both ends.
static uint32_t addOffsetSaturating(const uint32_t _a, const uint32_t _b)
{
	const auto sum = add14(_a, _b, 0);
	uint32_t result = sum.low & 0x1fff;
	if (sum.carry && (sum.low & 0x2000) != 0)
		result |= 0x1fff;
	if (!sum.carry && (sum.low & 0x2000) == 0)
		result = 0;
	if (sum.carry)
		result |= 0x2000;
	return result;
}

// The base-page bits a PCM window keeps: its low sizeLog2 bits come from the page counter.
static uint32_t pageMask(const uint32_t _sizeLog2)
{
	return (127 << _sizeLog2) & 127;
}

// A 15-bit r5 modulation word as a 14-bit log gain; 7FFEh is unity.
static constexpr uint32_t kModUnity = 0x7ffe;
static uint32_t modGain(const uint32_t _word)
{
	return (_word >> 1) & 0x3ffe;
}

// A 14-bit log level to a linear sample, negated for a negative one.
static uint32_t toLinear(const uint32_t _log, const bool _negative)
{
	const uint32_t linear = exp2Fixed(_log << 2);
	return _negative ? negated(linear, 32) : linear;
}

// The log-sine ROM holds a quarter wave; an 11-bit index folds onto it.
static uint32_t foldedLogsin(const uint32_t _index)
{
	uint32_t quarter = _index & 0x1ff;
	if (_index & 0x200)
		quarter ^= 0x1ff;
	return logsin(quarter);
}

// ---- PCM reads --------------------------------------------------------------------------------

// ROM byte address of a sample: the position within its 2048-sample page, the page from the
// base with its low size bits replaced by the page counter, and A19 from the base's top bit.
// _pageMask holds the base bits that survive, 127 << sizeLog2.
static uint32_t pcmAddress(const uint32_t _ph, const uint32_t _page, const uint32_t _pageMask)
{
	uint32_t address = (_ph & 0x7ff) << 1;
	address |= (_pageMask & _page) << 12;
	address |= ((_pageMask ^ 127) & (_ph >> 11)) << 12;
	if (_page & 0x80)
		address |= 0x80000;
	return address;
}

// The page counter running past a one-shot's window ends the read: it goes silent and its
// boundary event is queued. Returns whether the read is muted.
static bool advanceWindow(const uint32_t _ph, const uint32_t _pageMask, const bool _loop, bool& _ended, bool& _eventPending)
{
	const bool boundary = ((_ph >> 11) & _pageMask) != 0 && !_loop;
	if (boundary && !_ended)
	{
		_ended = true;
		_eventPending = true;
	}
	return _ended;
}

struct PcmSample
{
	uint32_t log;		// 14-bit log magnitude, 0 while muted
	bool negative;
};

// Two ROM bytes make one sample: the first carries the sign and log bits [12:6], the second
// bit 13 and bits [5:0]. Without the format bit the magnitude collapses to a single level.
PcmSample LA32::readSample(const uint32_t _address, const bool _muted) const
{
	const uint8_t first = readPcm(_address);
	const uint8_t second = readPcm(_address | 1);
	PcmSample sample{0, (first & 0x80) != 0};
	if (_muted)
		return sample;
	if (m_config.pcmFormat)
		sample.log = (second & 0x3f) | ((first & 0x7f) << 6) | ((second & 0x40) << 7);
	else if (first & 0x3f)
		sample.log = 0x2000;
	return sample;
}

// Hands one pending PCM boundary event to the interrupt latch when it is free, read 1 first.
// The status carries the slot number plus one: the address pipeline runs a slot behind.
void LA32::queuePcmEvent(VoiceState& _state)
{
	if (m_irqPending || !(_state.pcmEventPending[0] || _state.pcmEventPending[1]))
		return;
	if (_state.pcmEventPending[0])
	{
		_state.pcmEventPending[0] = false;
		m_irqStatus = (m_cycle + 1) & 31;
	}
	else
	{
		_state.pcmEventPending[1] = false;
		m_irqStatus = ((m_cycle + 1) & 31) | 0x20;
	}
	setIrq(true);
}

// ---- Synth cutoff -----------------------------------------------------------------------------

// The 15-bit cutoff sum clipped to 13 bits, the form that scales the amplitude.
static uint32_t clipTo13Bits(const Sum14& _sum)
{
	if ((_sum.low & 0x2000) != 0 || _sum.carry)
		return 0x1fff;
	return _sum.low & 0x1fff;
}

// The same sum less its 2000h offset, clipped to 0..1BFFh and put back over the offset: the
// form that shapes the resonance.
static uint32_t cutoffForResonance(const Sum14& _sum)
{
	uint32_t cutoff = _sum.low & 0x1fff;
	if (_sum.carry || (_sum.low & 0x3c00) == 0x3c00)
		cutoff = 0x1bff;
	if (!_sum.carry && (_sum.low & 0x2000) == 0)
		cutoff = 0;
	return cutoff | 0x2000;
}

// ---- Synth resonance --------------------------------------------------------------------------

// The resonant component is shaped by multiplying a phase ramp with the cutoff's linear value in
// two halves (the products of its low and high 7 bits), summed at two widths. The range flags
// say whether the sum fell below or above the window the sine lookup covers.
struct ResonanceWave
{
	int32_t productLow;		// ramp * cutoff bits [10:4]
	int32_t productHigh;	// ramp * cutoff bits [17:11]
	uint32_t sum16;			// the 16-bit sum and its carry
	bool carry16;
	uint32_t sum18;			// the same sum kept at 18 bits
	bool carry18;
	bool below;			// small enough to fit the lookup window
	bool above;			// large enough to have run past it
	uint32_t sineIndex;		// 11-bit index into the sine, [10] is its half
	uint32_t logsin;
};

static ResonanceWave resonanceWave(const uint32_t _ramp, const uint32_t _cutoffLinear)
{
	ResonanceWave w{};
	w.productLow = mul14x8(_ramp, (_cutoffLinear >> 4) & 127);
	w.productHigh = mul14x8(_ramp, (_cutoffLinear >> 11) & 127);

	const uint32_t sum16 = ((w.productLow >> 8) & 0xffff) + ((w.productHigh >> 1) & 0xffff);
	w.sum16 = sum16 & 0xffff;
	w.carry16 = ((sum16 >> 16) & 1) != 0;
	const uint32_t sum18 = ((w.productLow >> 8) & 0x3ffff) + ((w.productHigh >> 1) & 0x3ffff);
	w.sum18 = sum18 & 0x3ffff;
	w.carry18 = ((sum18 >> 18) & 1) != 0;

	const uint32_t highBits = (w.productHigh >> 8) & 0x1e00;
	w.below = !(w.carry16 || (w.sum16 & 0xfc00) != 0 || highBits != 0);
	w.above = !(!w.carry16 && !(bit(w.productHigh >> 8, 12) ^ bit(w.productLow >> 8, 15)))
		&& highBits == 0x1e00 && (w.sum16 & 0xfc00) == 0xfc00;

	w.sineIndex = (w.sum16 >> 1) & 0x7ff;
	w.logsin = foldedLogsin(w.sineIndex);
	return w;
}

// Sums the two waves of a slot into its output. With ring set, the slot's output becomes the
// product of that sum and the previous slot's output instead, so ring pairs are adjacent slots.
int32_t LA32::mixWaves(const uint32_t _w1, const uint32_t _w2, const bool _ring)
{
	// The linear values carry five fractional bits; the mixer takes them as 15-bit signed.
	const int32_t a = signExtend(_w1 >> 5, 15);
	const int32_t b = signExtend(_w2 >> 5, 15);
	const int32_t half = (b >> 1) + (a >> 1) + (a & 1);	// the rounded half sum

	if (!_ring)
	{
		m_prevOutput = half;
		return half;
	}

	// Ring modulation: the previous slot's output times the sum at full width, which the 8-bit
	// multiplier does in two parts, the sum's top 7 bits with its sign and then its low 7 bits.
	const int32_t full = (a >> 1) + b + (a & 1);
	int32_t high = (full >> 7) & 127;
	if (full & 0x2000)
		high |= 128;
	const int32_t product = (mul14x8(m_prevOutput, high) >> 6) + (mul14x8(m_prevOutput, full & 127) >> 13);
	m_prevOutput = half;
	return product;
}

// PCM partial: one or two ROM reads per slot. With interpolate set the second read is the next
// sample of the same wave and the two are blended on the phase's low bits; otherwise it is a
// second wave with its own page, window and gain (ramp B). Raises the one-shot boundary events.
int32_t LA32::computeVoicePcm(Voice& _voice, const uint32_t (&_tv)[2], const uint32_t _phase, const bool _signFlip)
{
	const VoiceRegisters::Pcm& pcm = _voice.regs.pcm;
	VoiceState& state = _voice.state;
	const bool interpolate = pcm.interpolate;

	// The ramp levels as gains: each lands one step below its level through the unity constant.
	const uint32_t gain1 = logMul(_tv[0], modGain(kModUnity));
	const uint32_t gain2 = logMul(_tv[1], modGain(kModUnity));

	uint32_t ph = (_phase >> 8) & 0x3ffff;

	// Read 1
	uint32_t mask = pageMask(pcm.wave[0].sizeLog2);
	const bool muted1 = advanceWindow(ph, mask, pcm.wave[0].loop, state.pcmEnded[0], state.pcmEventPending[0]);
	const uint32_t address1 = pcmAddress(ph, pcm.page[0], mask);

	// Read 2
	uint32_t page2 = pcm.page[0];
	bool loop2 = pcm.wave[0].loop;
	if (interpolate)
		ph = (ph + 1) & 0x3ffff;
	else
	{
		mask = pageMask(pcm.wave[1].sizeLog2);
		loop2 = pcm.wave[1].loop;
		page2 = pcm.page[1];
	}
	const bool muted2 = advanceWindow(ph, mask, loop2, state.pcmEnded[1], state.pcmEventPending[1]);

	// A completed one-shot stays silent even after the firmware sets the loop bit to acknowledge
	// its boundary; the event waits while another event owns the shared interrupt latch.
	queuePcmEvent(state);

	const uint32_t address2 = pcmAddress(ph, page2, mask);

	// The sample gain has no carry-in, so it sits one step below logMul.
	const PcmSample sample1 = readSample(address1, muted1);
	const Sum14 level1 = add14(sample1.log, gain1, 0);
	const bool quiet1 = tooQuietForSign(level1) && interpolate;
	const uint32_t w1 = toLinear(gated(level1), !quiet1 && (_signFlip ^ sample1.negative));

	const PcmSample sample2 = readSample(address2, muted2);
	const Sum14 level2 = add14(sample2.log, interpolate ? gain1 : gain2, 0);
	const bool quiet2 = tooQuietForSign(level2);
	const uint32_t w2 = toLinear(gated(level2), !quiet2 && (_signFlip ^ sample2.negative));

	if (!interpolate)
		return mixWaves(w1, w2, _voice.regs.ring);

	const uint32_t interp = (_phase >> 1) & 127;
	const int32_t o1 = mul14x8(static_cast<int32_t>(w1) >> 6, interp ^ 127) >> 7;
	const int32_t o2 = mul14x8(static_cast<int32_t>(w2) >> 6, interp) >> 7;
	m_prevOutput = o1 + o2;
	return m_prevOutput;
}

// Synth partial: a square or sawtooth with pulse width, cutoff and resonance. The output is the
// sum of two log-sine reads: wave 1 is the base waveform, wave 2 the resonant component riding
// on it. Ramp A offsets the cutoff, ramp B the amplitude, and the group's smoothed r5 words
// offset pulse width, cutoff and amplitude on top.
int32_t LA32::computeVoiceSynth(const VoiceRegisters& _regs, const uint32_t (&_tv)[2], const uint32_t _phase, const bool _signFlip)
{
	const VoiceRegisters::Synth& synth = _regs.synth;
	const bool sawtooth = synth.sawtooth;
	const bool ring = _regs.ring;

	// Pulse width and cutoff: the register byte scaled into the offset-binary domain plus the
	// group's modulation word.
	const uint32_t pulseWidth = addOffsetSaturating(static_cast<uint32_t>(synth.pulseWidth) << 6, modGain(m_groupMod[ModPulseWidth]));
	const uint32_t pulseThreshold = (pulseWidth >> 2) | 0x2000;

	const uint32_t cutoffBase = addOffsetSaturating(static_cast<uint32_t>(synth.cutoff) << 6, modGain(m_groupMod[ModCutoff]));

	// Ramp A rides on the cutoff. Two clips of the 15-bit sum follow: one to 13 bits that feeds
	// the amplitude (a low cutoff also attenuates), one to 1BFFh over the 2000h offset that
	// shapes the resonance.
	const Sum14 cutoffSum = add14(cutoffBase, _tv[0] & 0x3fff, 0);

	const uint32_t cutoffClipped = clipTo13Bits(cutoffSum);
	const uint32_t cutoff = cutoffForResonance(cutoffSum);

	// The cutoff as a linear value, and the resonance ramp's offset: the complement of whichever
	// of pulse threshold and cutoff is lower, made linear.
	const uint32_t cutoffLinear = exp2Fixed(((cutoff & 0x1fff) << 2) | 0x8000);

	const bool cutoffAbovePulse = add14(cutoff, negated(pulseThreshold, 14), 1).carry;	// cutoff - pulseThreshold has no borrow
	const uint32_t lower = cutoffAbovePulse ? pulseThreshold : cutoff;
	const uint32_t resonanceOffset = (exp2Fixed(((negated(lower, 12) | 0x1000) << 2) | 0x8003) >> 4) & 0x7fff;

	// Amplitude: ramp B times the group's amplitude word (unity on the odd slot of a ring pair),
	// then times the clipped cutoff.
	const uint32_t ampWord = (ring && (m_cycle & 1) != 0) ? kModUnity : m_groupMod[ModAmplitude];
	const uint32_t amplitude = logMul(_tv[1] & 0x3fff, modGain(ampWord));
	const uint32_t ampCutoff = logMul(amplitude, cutoffClipped << 1);

	// Two ramps run from the phase: the sawtooth position, and the inverted phase offset by the
	// resonance so its segment boundary (bit 16) moves with the cutoff.
	uint32_t sawPosition = (_phase >> 6) & 0xfff;
	if (_phase & 0x40000)
		sawPosition |= 0x3000;

	const uint32_t invertedPhase = negated((_phase >> 3) & 0xffff, 16);
	const uint32_t offsetPhase = (invertedPhase + resonanceOffset + 0x10001) & 0x1ffff;
	const bool secondSegment = (offsetPhase & 0x10000) != 0;

	// The resonant wave computed from both ramps; which range flag counts flips per segment.
	const ResonanceWave resA = resonanceWave(sawPosition, cutoffLinear);
	const ResonanceWave resB = resonanceWave(offsetPhase >> 3, cutoffLinear);
	const bool outOfRangeA = secondSegment ? resA.above : resA.below;
	const bool outOfRangeB = secondSegment ? resB.above : resB.below;

	// The base waveform: the phase as a sine index. A square only uses one half.
	uint32_t baseIndex = (invertedPhase >> 5) & 0x7ff;
	if (baseIndex & 0x200)
		baseIndex ^= 0x400;
	baseIndex ^= 0x200;
	if (!sawtooth)
		baseIndex &= ~0x400;
	const bool baseNegative = (baseIndex & 0x400) != 0;
	const uint32_t baseLogsin = foldedLogsin(baseIndex);

	// The resonance gain from the cutoff: full, unless the cutoff sits within 400h above its
	// 2000h middle, where it follows a quarter sine up from zero.
	uint32_t resonanceGainIndex = 0x1ff;
	if ((cutoff & 0x1c00) == 0)
		resonanceGainIndex = (cutoff >> 1) & 0x1ff;
	const uint32_t resonanceGain = logsin(resonanceGainIndex);

	const uint32_t segmentSineIndex = secondSegment ? resB.sineIndex : resA.sineIndex;
	const bool resonanceNegative = !(baseNegative ^ ((segmentSineIndex & 0x400) == 0));

	// Wave 1: the base waveform (a square is all ones) at the amplitude.
	const uint32_t baseWave = sawtooth ? sineLevel(baseLogsin) : 0x3fff;
	const uint32_t wave1Level = logMul(baseWave, ampCutoff);

	// The resonance level: the register's 5 bits with a 2000h floor whenever it is nonzero,
	// through the cutoff gain, the amplitude and the segment's resonant wave.
	uint32_t resonanceLevel = static_cast<uint32_t>(synth.resonance) << 8;
	if (synth.resonance)
		resonanceLevel |= 0x2000;
	const uint32_t resonanceAmp = logMul(resonanceLevel, sineLevel(resonanceGain));
	const uint32_t resonanceAtAmp = logMul(resonanceAmp, wave1Level);

	const uint32_t termA = outOfRangeA ? (resA.logsin ^ 0x1fff) << 1 : (secondSegment ? 0x3fff : sineLevel(resA.logsin));
	const uint32_t resonanceShaped = logMul(resonanceAtAmp, termA);

	// Wave 1 also carries the resonant waves that are out of range, or unity when neither is.
	uint32_t rangeTerms = 0;
	if (outOfRangeB)
		rangeTerms |= sineLevel(resB.logsin);
	if (outOfRangeA)
		rangeTerms |= sineLevel(resA.logsin);
	if (!outOfRangeA && !outOfRangeB)
		rangeTerms |= 0x3fff;
	const uint32_t wave1 = logMul(wave1Level, rangeTerms);

	// The resonant peak decays along the cycle: the decay factor selected by the register times
	// the segment's ramp position, complemented into a log attenuation.
	static const uint32_t decayFactors[] = {127, 64, 48, 32, 20, 12, 8, 4};
	uint32_t decayFactor = decayFactors[synth.resonanceDecay];
	if (secondSegment)
		decayFactor ^= 255;

	uint32_t decayPosition = 0;
	if (secondSegment)
	{
		decayPosition |= (resB.sum18 >> 5) & 0x1ff;
		const uint32_t high = (resB.productHigh >> 8) & 0xffff;
		const bool sameSign = !(((high & 0x1000) != 0) ^ (((resB.productLow >> 8) & 0x8000) != 0));
		const bool overflowed = !(sameSign && !resB.carry18);
		const bool useUpper = overflowed && (high & 0x800) != 0;
		decayPosition |= (useUpper ? (resB.sum18 >> 14) & 15 : 0) << 9;
	}
	else
	{
		decayPosition |= (resA.sum18 >> 5) & 0x1ff;
		const uint32_t high = (resA.productHigh >> 8) & 0xffff;
		const bool inRange = !(resA.carry18 || (high & 0x800) != 0);
		decayPosition |= (inRange ? (resA.sum18 >> 14) & 15 : 15) << 9;
	}

	const int32_t decayProduct = mul14x8(decayPosition, decayFactor);
	const bool decaySmall = (decayProduct >> 15) == 0;
	const uint32_t decayInverse = negated((static_cast<uint32_t>(decayProduct) >> 1) & 0x3fff, 14);
	uint32_t decayAttenuation = decayInverse & 0x3ff;
	if (decaySmall)
		decayAttenuation |= decayInverse & 0x3c00;
	const uint32_t resonanceDecayed = logMul(resonanceShaped, decayAttenuation);

	// Wave 2: the decayed resonance shaped by the other ramp's wave.
	const uint32_t termB = outOfRangeB ? (resB.logsin ^ 0x1fff) << 1 : (secondSegment ? sineLevel(resB.logsin) : 0x3fff);
	const Sum14 wave2Sum = add14(resonanceDecayed, termB, 1);
	const uint32_t wave2 = gated(wave2Sum);
	const bool quiet2 = tooQuietForSign(wave2Sum);

	const uint32_t w1 = toLinear(wave1, _signFlip ^ (baseNegative ^ secondSegment));
	const uint32_t w2 = toLinear(wave2, !quiet2 && (_signFlip ^ resonanceNegative));

	return mixWaves(w1, w2, ring);
}

// Pans a slot's output onto its bus pair: the left side gets value * gain / 128 with the gain
// (pan * 73) >> 2, the right side the remainder, rounded. Pan 7 leaves the right side out.
void LA32::addToBus(const int32_t _value, const uint8_t _pan, const uint8_t _pair)
{
	const int32_t left = mul14x8(_value, (_pan * 73) >> 2);
	m_summing[_pair] += left >> 7;

	if (_pan == 7)
		return;
	// value - left / 128, rounded: the complement's bit 6 is the rounding carry.
	const int32_t leftNegated = static_cast<int32_t>(negated(static_cast<uint32_t>(left), 32));
	const int32_t right = _value + (leftNegated >> 7) + ((leftNegated >> 6) & 1);
	m_summing[_pair | 4] += right;
}

// One step of a group smoother, state += (target/2 - state) / 128, as the silicon does it on
// the 14-bit adder: target/4 - state/2 by complement and carry-in, its top 8 bits sign-extended
// by the adder's carry, added back to the 15-bit state.
static uint16_t smoothTowards(const uint32_t _state, const uint32_t _target)
{
	const uint32_t difference = ((_target >> 2) & 0x3ffe) + negated((_state >> 1) & 0x3fff, 14) + 1;
	const bool positive = (difference & 0x4000) != 0;

	uint32_t step = (difference >> 6) & 0xff;
	if (!positive)
		step |= 0xff00;

	uint32_t next = _state + step;
	if (positive)
		next += 1;
	return static_cast<uint16_t>(next & 0x7fff);
}

// Advances one ramp by a frame. The 26-bit counter carries the 8-bit level in [25:18]; the step
// is 8 * 2^(rate/8) counter units, negated for a descending ramp. The ramp completes as soon as
// the new level is at or past its target in the direction of travel, which is at once for a
// ramp pointed away from it; the counter then snaps to the target and the event is reported
// every frame until the firmware writes a rate of 0. An inactive partial holds its counters.
RampStep LA32::stepRamp(const Ramp& _ramp, uint32_t& _counter, const bool _inactive, const uint32_t _inactiveLevel)
{
	const uint32_t target = _ramp.target;
	uint32_t step = exp2Fixed(static_cast<uint32_t>(_ramp.rate) << 9);
	const bool hold = _ramp.rate == 0;
	if (hold)
		step &= ~8;
	else if (_ramp.down)
		step = negated(step, 26);

	const uint32_t sum = _counter + step;
	const bool atTarget = ((sum >> 18) & 0xff) >= target;

	bool stop;
	if ((step & 0x2000000) != 0)
		stop = (sum & 0x4000000) == 0 || !atTarget;
	else
		stop = (sum & 0x4000000) != 0 || atTarget;

	const bool reached = !_inactive && !hold && stop;

	uint32_t result = sum & 0xffff;
	if (!reached && !_inactive)
		result |= sum & 0x3ff0000;
	if (_inactive)
		result |= _inactiveLevel;
	if (reached)
		result |= target << 18;

	_counter = result & 0x3ffffff;

	uint32_t level = _counter;
	if (reached)
		level &= ~0xffff;
	return {level >> 12, reached};
}

void LA32::updateSlot()
{
	updateInactive();

	if (m_cycle == 0)
	{
		m_finished = m_summing;
		m_summing.fill(0);
	}

	Voice& voice = m_voices[m_cycle];
	const VoiceRegisters& regs = voice.regs;
	VoiceState& state = voice.state;
	const bool inactive = (m_inactiveHistory & 1) != 0;

	// The two ramps. An inactive partial holds ramp B at the configured preset level.
	uint32_t tv_value[2];
	for (uint32_t i = 0; i < 2; i++)
	{
		const uint32_t inactiveLevel = i == 1 ? static_cast<uint32_t>(m_config.rampBPreset) << 22 : 0;
		const RampStep step = stepRamp(regs.ramp[i], state.rampCounter[i], inactive, inactiveLevel);
		tv_value[i] = step.level;

		if (step.reached && !m_irqPending)
		{
			m_irqStatus = 0x80 | m_cycle | (i << 5);
			setIrq(true);
		}
	}

	// wave gen

	uint32_t p = exp2Fixed((static_cast<uint32_t>(regs.octave) << 12) | regs.fraction);
	uint32_t phase = state.phase;

	phase += p;

	if (inactive)
	{
		phase = 0;
		state.pcmEnded = {};
		state.pcmEventPending = {};
	}

	state.phase = phase & 0x3ffffff;

	const bool sign_flip = signFlip(m_cycle, m_config.scheduleMode);

	const int32_t value = regs.isPcm
		? computeVoicePcm(voice, tv_value, phase, sign_flip)
		: computeVoiceSynth(regs, tv_value, phase, sign_flip);

	addToBus(value, regs.pan, regs.outputPair);

	// Slots 1-3 of a group step one smoother each, starting from the value latched for the
	// group; slot 7 latches the next group's smoothed values for its eight partials.
	const uint32_t slotInGroup = m_cycle & 7;
	if (slotInGroup >= 1 && slotInGroup <= 3)
	{
		const uint32_t index = slotInGroup - 1;
		ModulationGroup& group = m_modulation[m_cycle >> 3];
		group.state[index] = smoothTowards(m_groupMod[index], group.target[index]);
	}
	if (slotInGroup == 7)
	{
		const ModulationGroup& next = m_modulation[((m_cycle >> 3) + 1) & 3];
		for (size_t index = 0; index < m_groupMod.size(); ++index)
			m_groupMod[index] = next.state[index] & 0x7fff;
	}

	m_cycle = (m_cycle + 1) & 31;
}

}

#include "n2xstate.h"

#include <cassert>

#include "n2xhardware.h"
#include "synthLib/midiToSysex.h"
#include "synthLib/midiTypes.h"

namespace n2x
{
	static constexpr uint8_t g_singleDefault[] =
	{
		72,		// O2Pitch
		67,		// O2PitchFine
		64,		// Mix
		100,	// Cutoff
		10,		// Resonance
		0,		// FilterEnvAmount
		0,		// PW
		0,		// FmDepth
		0,		// FilterEnvA
		10,		// FilterEnvD
		100,	// FilterEnvS
		10,		// FilterEnvR
		0,		// AmpEnvA
		10,		// AmpEnvD
		100,	// AmpEnvS
		0,		// AmpEnvR
		0,		// Portamento
		127,	// Gain
		0,		// ModEnvA
		0,		// ModEnvD
		0,		// ModEnvLevel
		10,		// Lfo1Rate
		0,		// Lfo1Level
		10,		// Lfo2Rate
		0,		// ArpRange
		0,		// O2PitchSens
		0,		// O2PitchFineSens
		0,		// MixSens
		0,		// CutoffSens
		0,		// ResonanceSens
		0,		// FilterEnvAmountSens
		0,		// PWSens
		0,		// FmDepthSens
		0,		// FilterEnvASens
		0,		// FilterEnvDSens
		0,		// FilterEnvSSens
		0,		// FilterEnvRSens
		0,		// AmpEnvASens
		0,		// AmpEnvDSens
		0,		// AmpEnvSSens
		0,		// AmpEnvRSens
		0,		// PortamentoSens
		0,		// GainSens
		0,		// ModEnvASens
		0,		// ModEnvDSens
		0,		// ModEnvLevelSens
		0,		// Lfo1RateSens
		0,		// Lfo1LevelSens
		0,		// Lfo2RateSens
		0,		// ArpRangeSens
		0,		// O1Waveform
		0,		// O2Waveform
		0,		// Sync/RM/Dist
		0,		// FilterType
		1,		// O2Keytrack
		0,		// FilterKeytrack
		0,		// Lfo1Waveform
		0,		// Lfo1Dest
		0,		// VoiceMode
		0,		// ModWheelDest
		0,		// Unison
		0,		// ModEnvDest
		0,		// Auto
		0,		// FilterVelocity
		2,		// OctaveShift
		0,		// Lfo2Dest
	};

	static_assert(std::size(g_singleDefault) == g_singleDataSize/2);

	using MultiDefaultData = std::array<uint8_t, ((g_multiDataSize - g_singleDataSize * 4)>>1)>;

	MultiDefaultData createMultiDefaultData()
	{
		uint32_t i=0;

		MultiDefaultData multi{};

		auto set4 = [&](const uint8_t _a, const uint8_t _b, const uint8_t _c, const uint8_t _d)
		{
			multi[i++] = _a; multi[i++] = _b; multi[i++] = _c; multi[i++] = _d;
		};

		set4( 0, 1, 2, 3);	// MIDI channel
		set4( 0, 0, 0, 0);	// LFO 1 Sync
		set4( 0, 0, 0, 0);	// LFO 2 Sync
		set4( 0, 0, 0, 0);	// Filter Env Trigger
		set4( 0, 1, 2, 3);	// Filter Env Trigger Midi Channel
		set4(23,23,23,23);	// Filter Env Trigger Note Number
		set4( 0, 0, 0, 0);	// Amp Env Trigger
		set4( 0, 1, 2, 3);	// Amp Env Trigger Midi Channel
		set4(23,23,23,23);	// Amp Env Trigger Note Number
		set4( 0, 0, 0, 0);	// Morph Trigger
		set4( 0, 1, 2, 3);	// Morph Trigger Midi Channel
		set4(23,23,23,23);	// Morph Trigger Note Number
		multi[i++] = 2;		// Bend Range
		multi[i++] = 2;		// Unison Detune
		multi[i++] = 0;		// Out Mode A&B (lower nibble) and C&D (upper nibble)
		multi[i++] = 15;	// Global Midi Channel
		multi[i++] = 0;		// Program Change Enable
		multi[i++] = 1;		// Midi Control
		multi[i++] = 0;		// Master Tune
		multi[i++] = 0;		// Pedal Type
		multi[i++] = 1;		// Local Control
		multi[i++] = 2;		// Keyboard Octave Shift
		multi[i++] = 0;		// Selected Channel
		multi[i++] = 0;		// Arp Midi Out
		set4(1,0,0,0);		// Channel Active
		set4(0,0,0,0);		// Program Select
		set4(0,0,0,0);		// Bank Select
		set4(7,7,7,7);		// Channel Pressure Amount
		set4(0,0,0,0);		// Channel Pressure Dest
		set4(7,7,7,7);		// Expression Pedal Amount
		set4(0,0,0,0);		// Expression Pedal Dest
		multi[i++] = 0;		// Keyboard Split
		multi[i++] = 64;	// Split Point

		assert(i == multi.size());

		return multi;
	}

	static const MultiDefaultData g_multiDefault = createMultiDefaultData();

	State::State(Hardware& _hardware) : m_hardware(_hardware)
	{
		for(uint8_t i=0; i<static_cast<uint8_t>(m_singles.size()); ++i)
			createDefaultSingle(m_singles[i], i);
		createDefaultMulti(m_multi);

		receive(m_multi);
		for (const auto& single : m_singles)
			receive(single);
	}

	bool State::getState(std::vector<uint8_t>& _state)
	{
		_state.insert(_state.end(), m_multi.begin(), m_multi.end());
		for (const auto& single : m_singles)
			_state.insert(_state.end(), single.begin(), single.end());
		return true;
	}

	bool State::setState(const std::vector<uint8_t>& _state)
	{
		std::vector<std::vector<uint8_t>> msgs;
		synthLib::MidiToSysex::splitMultipleSysex(msgs, _state);

		for (auto& msg : msgs)
		{
			synthLib::SMidiEvent e;
			e.source = synthLib::MidiEventSource::Host;
			e.sysex = std::move(msg);
			receive(e);
		}

		return false;
	}

	bool State::receive(const synthLib::SMidiEvent& _ev)
	{
		auto& sysex = _ev.sysex;

		if(sysex.size() <= SysexIndex::IdxMsgSpec)
			return false;

		const auto bank = sysex[SysexIndex::IdxMsgType];
		const auto prog = sysex[SysexIndex::IdxMsgSpec];

		if(sysex.size() == g_singleDumpSize)
		{
			if(bank != SysexByte::SingleDumpBankEditBuffer)
				return false;
			if(prog > m_singles.size())
				return false;
			std::copy(sysex.begin(), sysex.end(), m_singles[prog].begin());
			send(_ev);
			return true;
		}

		if(sysex.size() == g_multiDumpSize)
		{
			if(bank != SysexByte::MultiDumpBankEditBuffer)
				return false;
			if(prog != 0)
				return false;
			std::copy(sysex.begin(), sysex.end(), m_multi.begin());
			send(_ev);
			return true;
		}

		if(sysex.size() == g_patchRequestSize)
			return false;

		return false;
	}

	void State::createDefaultSingle(SingleDump& _single, const uint8_t _program, const uint8_t _bank/* = n2x::SingleDumpBankEditBuffer*/)
	{
		createHeader(_single, _bank, _program);

		uint32_t o = IdxMsgSpec + 1;

		for (const auto b : g_singleDefault)
		{
			_single[o++] = b & 0xf;
			_single[o++] = (b>>4) & 0xf;
		}
	}

	// ReSharper disable once CppParameterMayBeConstPtrOrRef
	void State::copySingleToMulti(MultiDump& _multi, const SingleDump& _single, const uint8_t _index)
	{
		uint32_t i = SysexIndex::IdxMsgSpec + 1;
		i += g_singleDataSize * _index;
		std::copy(_single.begin() + g_sysexHeaderSize, _single.end() - g_sysexFooterSize, _multi.begin() + i);
	}

	void State::createDefaultMulti(MultiDump& _multi, const uint8_t _bank/* = SysexByte::MultiDumpBankEditBuffer*/)
	{
		createHeader(_multi, _bank, 0);

		SingleDump single;
		createDefaultSingle(single, 0);

		copySingleToMulti(_multi, single, 0);
		copySingleToMulti(_multi, single, 1);
		copySingleToMulti(_multi, single, 2);
		copySingleToMulti(_multi, single, 3);

		uint32_t i = SysexIndex::IdxMsgSpec + 1 + 4 * g_singleDataSize;
		assert(i == 264 * 2 + g_sysexHeaderSize);

		for (const auto b : g_multiDefault)
		{
			_multi[i++] = b & 0xf;
			_multi[i++] = (b>>4) & 0xf;
		}
		assert(i + g_sysexFooterSize == g_multiDumpSize);
	}

	void State::send(const synthLib::SMidiEvent& _e) const
	{
		if(_e.source == synthLib::MidiEventSource::Plugin)
			return;
		m_hardware.sendMidi(_e);
	}

	template<size_t Size>
	void State::createHeader(std::array<uint8_t, Size>& _buffer, uint8_t _msgType, uint8_t _msgSpec)
	{
		_buffer.fill(0);

		_buffer[0] = 0xf0;

		_buffer[SysexIndex::IdxClavia] = SysexByte::IdClavia;
		_buffer[SysexIndex::IdxDevice] = SysexByte::DefaultDeviceId;
		_buffer[SysexIndex::IdxN2x] = SysexByte::IdN2X;
		_buffer[SysexIndex::IdxMsgType] = _msgType;
		_buffer[SysexIndex::IdxMsgSpec] = _msgSpec;

		_buffer.back() = 0xf7;
	}
}
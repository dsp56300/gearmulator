#include "n2xstate.h"

#include <cassert>

#include "n2xhardware.h"
#include "synthLib/midiToSysex.h"
#include "synthLib/midiTranslator.h"
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

	const std::unordered_map<ControlChange, SingleParam> g_controllerMap =
	{
		{ControlChange::CCGain					, SingleParam::Gain				},
		{ControlChange::CCOctaveShift			, SingleParam::OctaveShift		},
		{ControlChange::CCModWheelDest			, SingleParam::ModWheelDest		},
		{ControlChange::CCUnison				, SingleParam::Unison			},
		{ControlChange::CCVoiceMode				, SingleParam::VoiceMode		},
		{ControlChange::CCAuto					, SingleParam::Auto				},
		{ControlChange::CCPortamento			, SingleParam::Portamento		},
		{ControlChange::CCLfo1Rate				, SingleParam::Lfo1Rate			},
		{ControlChange::CCLfo1Waveform			, SingleParam::Lfo1Waveform		},
		{ControlChange::CCLfo1Dest				, SingleParam::Lfo1Dest			},
		{ControlChange::CCLfo1Level				, SingleParam::Lfo1Level		},
		{ControlChange::CCLfo2Rate				, SingleParam::Lfo2Rate			},
		{ControlChange::CCLfo2Dest				, SingleParam::Lfo2Dest			},
		{ControlChange::CCArpRange				, SingleParam::ArpRange			},
		{ControlChange::CCModEnvA				, SingleParam::ModEnvA			},
		{ControlChange::CCModEnvD				, SingleParam::ModEnvD			},
		{ControlChange::CCModEnvDest			, SingleParam::ModEnvDest		},
		{ControlChange::CCModEnvLevel			, SingleParam::ModEnvLevel		},
		{ControlChange::CCO1Waveform			, SingleParam::O1Waveform		},
		{ControlChange::CCO2Waveform			, SingleParam::O2Waveform		},
		{ControlChange::CCO2Pitch				, SingleParam::O2Pitch			},
		{ControlChange::CCO2PitchFine			, SingleParam::O2PitchFine		},
		{ControlChange::CCFmDepth				, SingleParam::FmDepth			},
		{ControlChange::CCO2Keytrack			, SingleParam::O2Keytrack		},
		{ControlChange::CCPW					, SingleParam::PW				},
		{ControlChange::CCSync					, SingleParam::Sync				},
		{ControlChange::CCMix					, SingleParam::Mix				},
		{ControlChange::CCAmpEnvA				, SingleParam::AmpEnvA			},
		{ControlChange::CCAmpEnvD				, SingleParam::AmpEnvD			},
		{ControlChange::CCAmpEnvS				, SingleParam::AmpEnvS			},
		{ControlChange::CCAmpEnvR				, SingleParam::AmpEnvR			},
		{ControlChange::CCFilterEnvA			, SingleParam::FilterEnvA		},
		{ControlChange::CCFilterEnvD			, SingleParam::FilterEnvD		},
		{ControlChange::CCFilterEnvS			, SingleParam::FilterEnvS		},
		{ControlChange::CCFilterEnvR			, SingleParam::FilterEnvR		},
		{ControlChange::CCFilterType			, SingleParam::FilterType		},
		{ControlChange::CCCutoff				, SingleParam::Cutoff			},
		{ControlChange::CCResonance				, SingleParam::Resonance		},
		{ControlChange::CCFilterEnvAmount		, SingleParam::FilterEnvAmount	},
		{ControlChange::CCFilterVelocity		, SingleParam::FilterVelocity	},
		{ControlChange::CCFilterKeytrack		, SingleParam::FilterKeytrack	},
		{ControlChange::CCDistortion			, SingleParam::Distortion		}
	};

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

	State::State(Hardware* _hardware, synthLib::MidiTranslator* _midiTranslator) : m_hardware(_hardware), m_midiTranslator(_midiTranslator)
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
		updateMultiFromSingles();
		if(m_multi[g_multiDumpSize - 1] == 0xf7)
			_state.insert(_state.end(), m_multi.begin(), m_multi.end() - g_nameLength);
		else
			_state.insert(_state.end(), m_multi.begin(), m_multi.end());
		for (const auto it : m_knobStates)
		{
			auto knobSysex = createKnobSysex(it.first, it.second);
			_state.insert(_state.end(), knobSysex.begin(), knobSysex.end());
		}
		return true;
	}

	bool State::setState(const std::vector<uint8_t>& _state)
	{
		std::vector<std::vector<uint8_t>> msgs;
		synthLib::MidiToSysex::splitMultipleSysex(msgs, _state);

		for (auto& msg : msgs)
			receive(msg, synthLib::MidiEventSource::Host);

		return false;
	}

	bool State::receive(std::vector<synthLib::SMidiEvent>& _responses, const synthLib::SMidiEvent& _ev)
	{
		if(_ev.sysex.empty())
		{
			return receiveNonSysex(_ev);
		}

		auto& sysex = _ev.sysex;

		if(sysex.size() <= SysexIndex::IdxMsgSpec)
			return false;

		const auto bank = sysex[SysexIndex::IdxMsgType];
		const auto prog = sysex[SysexIndex::IdxMsgSpec];

		if(isSingleDump(sysex))
		{
			if(bank != SysexByte::SingleDumpBankEditBuffer)
				return false;
			if(prog > m_singles.size())
				return false;
			m_singles[prog].fill(0);
			std::copy(sysex.begin(), sysex.end(), m_singles[prog].begin());
			send(_ev);
			return true;
		}

		if(isMultiDump(sysex))
		{
			if(bank != SysexByte::MultiDumpBankEditBuffer)
				return false;
			if(prog != 0)
				return false;
			m_multi.fill(0);
			std::copy(sysex.begin(), sysex.end(), m_multi.begin());

			if (m_midiTranslator)
			{
				// As we need support for individual midi channels for the editor to adjus each part separately but
				// knobs can only be modified via midi CC, we tell the device that parts 0-3 are on midi channels 0-3 even if
				// they are not. The midi translator will translate regular midi messages and the editor uses messages that are
				// not translated

				m_midiTranslator->clear();

				MultiDump dump = m_multi;
				for (uint8_t i = 0; i < 4; ++i)
				{
					const auto ch = getPartMidiChannel(dump, i);
					setPartMidiChannel(dump, i, i);
					m_midiTranslator->addTargetChannel(ch, i);
				}

				synthLib::SMidiEvent e;
				e.sysex.assign(dump.begin(), dump.end());
				send(e);
			}
			else
			{
				send(_ev);
			}

			return true;
		}

		if (bank == SysexByte::MultiRequestBankEditBuffer)
		{
			_responses.emplace_back(synthLib::MidiEventSource::Internal);
			_responses.back().sysex.assign(m_multi.begin(), m_multi.end());
			_responses.back().sysex = validateDump(_responses.back().sysex);
			return true;
		}

		if(bank == n2x::SysexByte::EmuSetPotPosition)
		{
			KnobType knobType;
			uint8_t knobValue;

			if(parseKnobSysex(knobType, knobValue, sysex))
			{
				if(m_hardware)
					m_hardware->setKnobPosition(knobType, knobValue);
				m_knobStates[knobType] = knobValue;
				return true;
			}
		}
		else if(bank == SysexByte::EmuGetPotsPosition)
		{
			for (const auto it : m_knobStates)
			{
				_responses.emplace_back(synthLib::MidiEventSource::Internal);
				_responses.back().sysex = createKnobSysex(it.first, it.second);
			}
			return true;
		}
		else if (bank == SysexByte::EmuSetPartCC)
		{
			synthLib::SMidiEvent e;
			auto part = sysex[5];
			e.a = sysex[6];
			e.b = sysex[7];
			e.c = sysex[8];
			e.source = _ev.source;
			e.offset = _ev.offset;
			changeSingleParameter(part, e);
		}

		return false;
	}

	bool State::receive(const std::vector<uint8_t>& _data, synthLib::MidiEventSource _source)
	{
		synthLib::SMidiEvent e;
		e.sysex = _data;
		e.source = _source;
		return receive(e);
	}

	bool State::receiveNonSysex(const synthLib::SMidiEvent& _ev)
	{
		switch (_ev.a & 0xf0)
		{
		case synthLib::M_CONTROLCHANGE:
			{
				const auto parts = getPartsForMidiChannel(_ev);
				if(parts.empty())
					return false;
				for (const auto part : parts)
				{
					if (!changeSingleParameter(part, _ev))
						return false;
				}
				return true;
			}
		default:
			return false;
		}
	}

	bool State::changeSingleParameter(const uint8_t _part, const synthLib::SMidiEvent& _ev)
	{
		const auto cc = static_cast<ControlChange>(_ev.b);
		const auto it = g_controllerMap.find(cc);
		if(it == g_controllerMap.end())
			return false;
		const SingleParam param = it->second;
		const auto offset = getOffsetInSingleDump(param);
		switch (param)
		{
		case SingleParam::Sync:
			// this can either be sync or distortion, they end up in the same midi byte
			switch(cc)
			{
			case ControlChange::CCSync:
				{
					auto v = unpackNibbles(m_singles[_part], offset);
					v &= ~0x3;
					v |= _ev.c & 0x3;
					packNibbles(m_singles[_part], offset, v);
				}
				return true;
			case ControlChange::CCDistortion:
				{
					auto v = unpackNibbles(m_singles[_part], offset);
					v &= ~(1<<4);
					v |= _ev.c << 4;
					packNibbles(m_singles[_part], offset, v);
				}
				return true;
			default:
				assert(false && "unexpected control change type");
				return false;
			}
			break;
		default:
			packNibbles(m_singles[_part], offset, _ev.c);
			return true;
		}
	}

	bool State::changeSingleParameter(const uint8_t _part, const SingleParam _parameter, const uint8_t _value)
	{
		if(_part >= m_singles.size())
			return false;
		return changeSingleParameter(m_singles[_part], _parameter, _value);
	}

	bool State::changeMultiParameter(const MultiParam _parameter, const uint8_t _value)
	{
		return changeDumpParameter(m_multi, getOffsetInMultiDump(_parameter), _value);
	}

	bool State::changeSingleParameter(SingleDump& _dump, const SingleParam _param, const uint8_t _value)
	{
		return changeDumpParameter(_dump, getOffsetInSingleDump(_param), _value);
	}

	void State::updateMultiFromSingles()
	{
		for(uint8_t i=0; i<static_cast<uint8_t>(m_singles.size()); ++i)
			copySingleToMulti(m_multi, m_singles[i], i);
	}

	void State::createDefaultSingle(SingleDump& _single, const uint8_t _program, const uint8_t _bank/* = n2x::SingleDumpBankEditBuffer*/)
	{
		createHeader(_single, _bank, _program);
		_single[g_singleDumpSize-1] = 0xf7;

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
		std::copy_n(_single.begin() + g_sysexHeaderSize, g_singleDataSize, _multi.begin() + i);
	}

	void State::extractSingleFromMulti(SingleDump& _single, const MultiDump& _multi, uint8_t _index)
	{
		uint32_t i = SysexIndex::IdxMsgSpec + 1;
		i += g_singleDataSize * _index;
		std::copy_n(_multi.begin() + i, g_singleDataSize, _single.begin() + g_sysexHeaderSize);
	}

	void State::createDefaultMulti(MultiDump& _multi, const uint8_t _bank/* = SysexByte::MultiDumpBankEditBuffer*/)
	{
		createHeader(_multi, _bank, 0);
		_multi[g_multiDumpSize-1] = 0xf7;

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

	uint32_t State::getOffsetInSingleDump(const SingleParam _param)
	{
		return g_sysexHeaderSize + (_param<<1);
	}

	uint32_t State::getOffsetInMultiDump(const MultiParam _param)
	{
		return g_sysexHeaderSize + (_param<<1);
	}

	std::vector<uint8_t> State::getPartsForMidiChannel(const synthLib::SMidiEvent& _ev) const
	{
		const auto ch = _ev.a & 0xf;
		return getPartsForMidiChannel(ch);
	}

	std::vector<uint8_t> State::getPartsForMidiChannel(const uint8_t _channel) const
	{
		std::vector<uint8_t> res;

		for(uint8_t i=0; i<static_cast<uint8_t>(m_singles.size()); ++i)
		{
			if(getPartMidiChannel(i) == _channel)
				res.push_back(i);
		}
		return res;
	}

	std::vector<uint8_t> State::createKnobSysex(KnobType _type, uint8_t _value)
	{
		return {0xf0, IdClavia, DefaultDeviceId, IdN2X,
			EmuSetPotPosition,
			static_cast<uint8_t>(_type),
			static_cast<uint8_t>(_value >> 4),
			static_cast<uint8_t>(_value & 0x0f),
			0xf7
		};
	}

	bool State::parseKnobSysex(KnobType& _type, uint8_t& _value, const std::vector<uint8_t>& _sysex)
	{
		if(_sysex.size() <= SysexIndex::IdxKnobPosL)
			return false;
		if(_sysex[SysexIndex::IdxMsgType] != SysexByte::EmuSetPotPosition)
			return false;

		_type = static_cast<KnobType>(_sysex[SysexIndex::IdxMsgSpec]);
		_value = static_cast<uint8_t>((_sysex[SysexIndex::IdxKnobPosH] << 4) | _sysex[SysexIndex::IdxKnobPosL]);

		return true;
	}

	bool State::getKnobState(uint8_t& _result, const KnobType _type) const
	{
		const auto it = m_knobStates.find(_type);
		if(it == m_knobStates.end())
			return false;
		_result = it->second;
		return true;
	}

	bool State::isSingleDump(const std::vector<uint8_t>& _dump)
	{
		return _dump.size() == g_singleDumpSize || _dump.size() == g_singleDumpWithNameSize;
	}

	bool State::isMultiDump(const std::vector<uint8_t>& _dump)
	{
		return _dump.size() == g_multiDumpSize || _dump.size() == g_multiDumpWithNameSize;
	}

	std::string State::extractPatchName(const std::vector<uint8_t>& _dump)
	{
		if(!isDumpWithPatchName(_dump))
			return {};
		auto* begin = &_dump[_dump.size() - g_nameLength - 1];
		if(*begin == 0xf7)
			return {};
		std::string name(reinterpret_cast<const char*>(begin), g_nameLength);
		return name;
	}

	bool State::isDumpWithPatchName(const std::vector<uint8_t>& _dump)
	{
		return _dump.size() == g_singleDumpWithNameSize || _dump.size() == g_multiDumpWithNameSize;
	}

	std::vector<uint8_t> State::stripPatchName(const std::vector<uint8_t>& _dump)
	{
		if(!isDumpWithPatchName(_dump))
			return _dump;
		auto d = _dump;
		d.erase(d.end() - g_nameLength - 1, d.end() - 1);
		assert(d.size() == g_singleDumpSize || d.size() == g_multiDumpSize);
		d.back() = 0xf7;
		return d;
	}

	bool State::isValidPatchName(const std::vector<uint8_t>& _dump)
	{
		if(!isDumpWithPatchName(_dump))
			return false;

		if(_dump.back() != 0xf7)
			return false;

		const auto nameStart = _dump.size() - g_nameLength - 1;

		for(size_t i=nameStart; i<nameStart+g_nameLength; ++i)
		{
			if(_dump[i] < 32 || _dump[i] >= 128)
				return false;
		}
		return true;
	}

	std::vector<uint8_t> State::validateDump(const std::vector<uint8_t>& _dump)
	{
		if(!isValidPatchName(_dump))
			return stripPatchName(_dump);
		return _dump;
	}

	synthLib::SMidiEvent& State::createPartCC(uint8_t _part, synthLib::SMidiEvent& _ccEvent)
	{
		_ccEvent.sysex = { 0xf0, IdClavia, SysexByte::DefaultDeviceId, IdN2X,
			SysexByte::EmuSetPartCC,
			_part,
			_ccEvent.a,
			_ccEvent.b,
			_ccEvent.c,
			0xf7
		};
		return _ccEvent;
	}

	void State::send(const synthLib::SMidiEvent& _e) const
	{
		if(_e.source == synthLib::MidiEventSource::Plugin)
			return;

		if(m_hardware)
		{
			const auto& sysex = _e.sysex;

			if(isDumpWithPatchName(sysex))
			{
				auto e = _e;
				e.sysex = stripPatchName(sysex);
				m_hardware->sendMidi(e);
			}
			else
			{
				m_hardware->sendMidi(_e);
			}
		}
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

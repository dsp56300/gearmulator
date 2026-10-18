#include "88lib/gs/params.h"

#include "88lib/gs/efx.h"
#include "88lib/gs/gs.h"

#include <cstdio>

namespace emu88Lib::gs
{
	Generation generation(const DeviceModel _model)
	{
		switch(_model)
		{
		case DeviceModel::Sc88:
		case DeviceModel::Sc88VL:
		case DeviceModel::Xpgs:
			return Generation::Sc88;
		case DeviceModel::Sc88Pro:
		case DeviceModel::Sk88Pro:
		case DeviceModel::VeGsPro:
			return Generation::Sc88Pro;
		case DeviceModel::Sc8850:
		case DeviceModel::Sc8820:
			return Generation::Sc8850;
		default:
			return Generation::Sc55;
		}
	}

	bool isFirstSc55Program(const DeviceModel _model)
	{
		return isSc55Model(_model) && getSc55DeviceProfile(_model).generation == Sc55Generation::First;
	}

	namespace
	{
		Param param(const char* _id, const char* _section, const char* _label, const uint32_t _address,
			const uint16_t _min, const uint16_t _max, const uint16_t _def, const ParamText _text = ParamText::Number,
			const Generation _since = Generation::Sc55)
		{
			Param p{};
			p.id = _id;
			p.section = _section;
			p.label = _label;
			p.address = _address;
			p.min = _min;
			p.max = _max;
			p.def = _def;
			p.text = _text;
			p.since = _since;
			return p;
		}

		Param withCc(Param _p, const uint8_t _cc) { _p.cc = _cc; return _p; }
		Param withRpn(Param _p, const uint8_t _rpn) { _p.rpn = _rpn; return _p; }
		Param withNrpn(Param _p, const uint8_t _lsb) { _p.nrpnMsb = 1; _p.nrpnLsb = _lsb; return _p; }
		Param withField(Param _p, const PartField _field) { _p.field = _field; return _p; }
		Param withSize(Param _p, const uint8_t _size) { _p.size = _size; return _p; }
		Param withSysexSince(Param _p, const Generation _since) { _p.sysexSince = _since; return _p; }
		Param ignoring(Param _p, const OutOfRange _outOfRange = OutOfRange::Ignore)
		{
			_p.outOfRange = _outOfRange;
			return _p;
		}
		Param ignoringOn8850(Param _p) { return ignoring(_p, OutOfRange::IgnoreOn8850); }

		std::vector<Param> createGlobalParams()
		{
			constexpr auto sc88 = Generation::Sc88;
			constexpr auto pro = Generation::Sc88Pro;
			std::vector<Param> params = {
				param("MasterVolume", "System", "Volume", 0x400004, 0, 127, 127),
				param("MasterPan", "System", "Pan", 0x400006, 1, 127, 64, ParamText::Pan),
				param("MasterKeyShift", "System", "Key Shift", 0x400005, 40, 88, 64, ParamText::Signed),
				withSize(param("MasterTune", "System", "Tune", g_masterTune, 24, 2024, 1024, ParamText::MasterTune), 4),

				ignoringOn8850(param("ReverbMacro", "Reverb", "Type", 0x400130, 0, 7, 4, ParamText::ReverbMacro)),
				ignoringOn8850(param("ReverbCharacter", "Reverb", "Character", 0x400131, 0, 7, 4,
					ParamText::ReverbMacro)),
				param("ReverbPreLpf", "Reverb", "Pre-LPF", 0x400132, 0, 7, 0),
				param("ReverbLevel", "Reverb", "Level", 0x400133, 0, 127, 64),
				param("ReverbTime", "Reverb", "Time", 0x400134, 0, 127, 64),
				param("ReverbDelayFeedback", "Reverb", "Delay Feedback", 0x400135, 0, 127, 0),
				param("ReverbPreDelay", "Reverb", "Pre Delay", 0x400137, 0, 127, 0, ParamText::Milliseconds, sc88),

				ignoringOn8850(param("ChorusMacro", "Chorus", "Type", 0x400138, 0, 7, 2, ParamText::ChorusMacro)),
				param("ChorusPreLpf", "Chorus", "Pre-LPF", 0x400139, 0, 7, 0),
				param("ChorusLevel", "Chorus", "Level", 0x40013a, 0, 127, 64),
				param("ChorusFeedback", "Chorus", "Feedback", 0x40013b, 0, 127, 8),
				param("ChorusDelay", "Chorus", "Delay", 0x40013c, 0, 127, 80),
				param("ChorusRate", "Chorus", "Rate", 0x40013d, 0, 127, 3),
				param("ChorusDepth", "Chorus", "Depth", 0x40013e, 0, 127, 19),
				param("ChorusToReverb", "Chorus", "Send to Reverb", 0x40013f, 0, 127, 0),
				param("ChorusToDelay", "Chorus", "Send to Delay", 0x400140, 0, 127, 0, ParamText::Number, sc88),

				ignoringOn8850(param("DelayMacro", "Delay", "Type", 0x400150, 0, 9, 0, ParamText::DelayMacro, sc88)),
				param("DelayPreLpf", "Delay", "Pre-LPF", 0x400151, 0, 7, 0, ParamText::Number, sc88),
				ignoringOn8850(param("DelayTimeCenter", "Delay", "Time Center", 0x400152, 1, 115, 97, ParamText::Number,
					sc88)),
				ignoringOn8850(param("DelayRatioLeft", "Delay", "Time Ratio Left", 0x400153, 1, 120, 1,
					ParamText::DelayRatio, sc88)),
				ignoringOn8850(param("DelayRatioRight", "Delay", "Time Ratio Right", 0x400154, 1, 120, 1,
					ParamText::DelayRatio, sc88)),
				param("DelayLevelCenter", "Delay", "Level Center", 0x400155, 0, 127, 127, ParamText::Number, sc88),
				param("DelayLevelLeft", "Delay", "Level Left", 0x400156, 0, 127, 0, ParamText::Number, sc88),
				param("DelayLevelRight", "Delay", "Level Right", 0x400157, 0, 127, 0, ParamText::Number, sc88),
				param("DelayLevel", "Delay", "Level", 0x400158, 0, 127, 64, ParamText::Number, sc88),
				param("DelayFeedback", "Delay", "Feedback", 0x400159, 0, 127, 80, ParamText::Signed, sc88),
				param("DelayToReverb", "Delay", "Send to Reverb", 0x40015a, 0, 127, 0, ParamText::Number, sc88),

				ignoringOn8850(param("EqLowFreq", "Equalizer", "Low Frequency", 0x400200, 0, 1, 0, ParamText::EqLowFreq,
					sc88)),
				ignoringOn8850(param("EqLowGain", "Equalizer", "Low Gain", 0x400201, 52, 76, 64, ParamText::Decibel,
					sc88)),
				ignoringOn8850(param("EqHighFreq", "Equalizer", "High Frequency", 0x400202, 0, 1, 0,
					ParamText::EqHighFreq, sc88)),
				ignoringOn8850(param("EqHighGain", "Equalizer", "High Gain", 0x400203, 52, 76, 64, ParamText::Decibel,
					sc88)),

				withSize(param("EfxType", "Insertion Effect", "Type", g_efxTypeAddress, 0, 0x7f7f, 0,
					ParamText::EfxType, pro), 2),
			};
			// The twenty parameters as Thru has them, after a GS Reset; their names and ranges are the type's
			const auto& thru = efxTypes()[0];
			for(uint32_t i = 0; i < g_efxParamCount; ++i)
			{
				static const char* ids[g_efxParamCount] = {
					"EfxParam1", "EfxParam2", "EfxParam3", "EfxParam4", "EfxParam5", "EfxParam6", "EfxParam7",
					"EfxParam8", "EfxParam9", "EfxParam10", "EfxParam11", "EfxParam12", "EfxParam13", "EfxParam14",
					"EfxParam15", "EfxParam16", "EfxParam17", "EfxParam18", "EfxParam19", "EfxParam20"};
				params.push_back(param(ids[i], "Insertion Effect", ids[i], g_efxParamAddress + i, 0, 127, thru.defaults[i],
					ParamText::EfxParam, pro));
			}
			params.push_back(param("EfxToReverb", "Insertion Effect", "Send to Reverb", 0x400317, 0, 127,
				g_efxSendDefaults[0], ParamText::Number, pro));
			params.push_back(param("EfxToChorus", "Insertion Effect", "Send to Chorus", 0x400318, 0, 127,
				g_efxSendDefaults[1], ParamText::Number, pro));
			params.push_back(param("EfxToDelay", "Insertion Effect", "Send to Delay", 0x400319, 0, 127,
				g_efxSendDefaults[2], ParamText::Number, pro));
			// The SC-88Pro takes controllers 1-95 for a source, the SC-8850 any value
			params.push_back(ignoring(param("EfxSource1", "Insertion Effect", "Control Source 1", 0x40031b, 0, 95, 0,
				ParamText::EfxSource, pro), OutOfRange::RawOn8850));
			params.push_back(param("EfxDepth1", "Insertion Effect", "Control Depth 1", 0x40031c, 0, 127, 64,
				ParamText::Signed, pro));
			params.push_back(ignoring(param("EfxSource2", "Insertion Effect", "Control Source 2", 0x40031d, 0, 95, 0,
				ParamText::EfxSource, pro), OutOfRange::RawOn8850));
			params.push_back(param("EfxDepth2", "Insertion Effect", "Control Depth 2", 0x40031e, 0, 127, 64,
				ParamText::Signed, pro));
			params.push_back(ignoring(param("EfxToEq", "Insertion Effect", "Equalizer", 0x40031f, 0, 1, 1,
				ParamText::OffOn, pro), OutOfRange::RawOn8850));
			return params;
		}

		std::vector<Param> createPartParams()
		{
			constexpr auto sc88 = Generation::Sc88;
			std::vector<Param> params = {
				withField(withCc(param("Volume", "Mixer", "Volume", 0x401019, 0, 127, 100), 7), PartField::Level),
				withCc(param("Expression", "Mixer", "Expression", 0, 0, 127, 127), 11),
				withField(withCc(param("Pan", "Mixer", "Pan", 0x40101c, 0, 127, 64, ParamText::PartPan), 10),
					PartField::Pan),
				withCc(param("ReverbSend", "Mixer", "Reverb Send", 0x401022, 0, 127, 40), 91),
				withCc(param("ChorusSend", "Mixer", "Chorus Send", 0x401021, 0, 127, 0), 93),
				withCc(param("DelaySend", "Mixer", "Delay Send", 0x40102c, 0, 127, 0, ParamText::Number, sc88), 94),
				ignoring(param("EfxAssign", "Mixer", "Insertion Effect", 0x404022, 0, 1, 0, ParamText::OffOn,
					Generation::Sc88Pro)),
				param("PartEq", "Mixer", "Equalizer", 0x404020, 0, 1, 1, ParamText::OffOn, sc88),

				param("KeyShift", "Pitch", "Key Shift", 0x401016, 40, 88, 64, ParamText::Signed),
				withSysexSince(withSize(withRpn(param("FineTune", "Pitch", "Fine Tune", 0x40102a, 0, 127, 64,
					ParamText::FineTune), 1), 2), sc88),
				withRpn(param("BendRange", "Pitch", "Bend Range", 0, 0, 24, 2, ParamText::Semitones), 0),
				param("MonoPoly", "Pitch", "Mono / Poly", 0x401013, 0, 1, 1, ParamText::MonoPoly),

				withNrpn(param("VibratoRate", "Tone Modify", "Vibrato Rate", 0x401030, 0, 127, 64, ParamText::Signed),
					0x08),
				withNrpn(param("VibratoDepth", "Tone Modify", "Vibrato Depth", 0x401031, 0, 127, 64, ParamText::Signed),
					0x09),
				withNrpn(param("VibratoDelay", "Tone Modify", "Vibrato Delay", 0x401037, 0, 127, 64, ParamText::Signed),
					0x0a),
				withNrpn(param("Cutoff", "Tone Modify", "Cutoff", 0x401032, 0, 127, 64, ParamText::Signed), 0x20),
				withNrpn(param("Resonance", "Tone Modify", "Resonance", 0x401033, 0, 127, 64, ParamText::Signed), 0x21),
				withNrpn(param("Attack", "Tone Modify", "Attack", 0x401034, 0, 127, 64, ParamText::Signed), 0x63),
				withNrpn(param("Decay", "Tone Modify", "Decay", 0x401035, 0, 127, 64, ParamText::Signed), 0x64),
				withNrpn(param("Release", "Tone Modify", "Release", 0x401036, 0, 127, 64, ParamText::Signed), 0x66),

				ignoringOn8850(withField(param("PartMode", "Part Setup", "Part Mode", 0x401015, 0, 2, 0,
					ParamText::PartMode),
					PartField::UseForRhythm)),
				withField(param("RxChannel", "Part Setup", "Rx Channel", 0x401002, 0, 16, 0, ParamText::RxChannel),
					PartField::RxChannel),
				param("VelocityDepth", "Part Setup", "Velocity Depth", 0x40101a, 0, 127, 64),
				param("VelocityOffset", "Part Setup", "Velocity Offset", 0x40101b, 0, 127, 64),
				param("KeyRangeLow", "Part Setup", "Key Range Low", 0x40101d, 0, 127, 0, ParamText::Note),
				param("KeyRangeHigh", "Part Setup", "Key Range High", 0x40101e, 0, 127, 127, ParamText::Note),
				param("RxRpn", "Part Setup", "Rx RPN", 0x401009, 0, 1, 1, ParamText::OffOn),
				param("RxNrpn", "Part Setup", "Rx NRPN", 0x40100a, 0, 1, 1, ParamText::OffOn),
			};

			constexpr const char* notes[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
			constexpr const char* ids[] = {"ScaleC", "ScaleCs", "ScaleD", "ScaleDs", "ScaleE", "ScaleF", "ScaleFs",
				"ScaleG", "ScaleGs", "ScaleA", "ScaleAs", "ScaleB"};
			for(uint32_t n = 0; n < 12; ++n)
				params.push_back(param(ids[n], "Scale Tuning", notes[n], 0x401040 + n, 0, 127, 64, ParamText::Cents,
					sc88));
			return params;
		}
	}

	const std::vector<Param>& globalParams()
	{
		static const std::vector<Param> params = createGlobalParams();
		return params;
	}

	const std::vector<Param>& partParams()
	{
		static const std::vector<Param> params = createPartParams();
		return params;
	}

	bool isAvailable(const Param& _param, const DeviceModel _model)
	{
		return generation(_model) >= _param.since;
	}

	bool hasAddress(const Param& _param, const DeviceModel _model)
	{
		return _param.address != 0 && isAvailable(_param, _model) && generation(_model) >= _param.sysexSince;
	}

	std::pair<uint16_t, uint16_t> valueRange(const Param& _param, const DeviceModel _model)
	{
		// The SC-55s keep the tone modify within the GS range, and the SC-55 and SC-155 the cutoff below its centre
		if(_param.nrpnMsb == 1 && generation(_model) == Generation::Sc55)
		{
			if(_param.address == 0x401032 && (_model == DeviceModel::Sc55Mk1 || _model == DeviceModel::Sc155))
				return {0x0e, 0x50};
			return {0x0e, 0x72};
		}
		return {_param.min, _param.max};
	}

	bool acceptValue(const Param& _param, const DeviceModel _model, uint16_t& _value)
	{
		const auto [min, max] = valueRange(_param, _model);
		if(_value >= min && _value <= max)
			return true;
		switch(_param.outOfRange)
		{
		case OutOfRange::Ignore:
			return false;
		case OutOfRange::IgnoreOn8850:
			if(generation(_model) == Generation::Sc8850)
				return false;
			break;
		case OutOfRange::RawOn8850:
			return generation(_model) == Generation::Sc8850 && _value <= 127;
		default:
			break;
		}
		_value = _value < min ? min : max;
		return true;
	}

	namespace
	{
		template<typename T> int findIf(const std::vector<Param>& _params, const T& _match)
		{
			for(size_t i = 0; i < _params.size(); ++i)
			{
				if(_match(_params[i]))
					return static_cast<int>(i);
			}
			return -1;
		}
	}

	uint32_t partParamAddress(const uint8_t _part, const Param& _param)
	{
		return (_param.address & 0xfff0ffu) | static_cast<uint32_t>(partBlock(_part)) << 8;
	}

	int findGlobalParam(const uint32_t _address)
	{
		return findIf(globalParams(), [&](const Param& _p) { return _p.address == _address; });
	}

	int findPartParam(const uint32_t _address)
	{
		if(!_address)
			return -1;
		return findIf(partParams(), [&](const Param& _p) { return _p.address == _address; });
	}

	int findGlobalParamById(const std::string& _id)
	{
		return findIf(globalParams(), [&](const Param& _p) { return _id == _p.id; });
	}

	int findPartParamById(const std::string& _id)
	{
		return findIf(partParams(), [&](const Param& _p) { return _id == _p.id; });
	}

	int findPartParamByController(const uint8_t _cc)
	{
		return findIf(partParams(), [&](const Param& _p) { return _p.cc == _cc; });
	}

	int findPartParamByRpn(const uint8_t _lsb)
	{
		return findIf(partParams(), [&](const Param& _p) { return _p.rpn == _lsb; });
	}

	int findPartParamByNrpn(const uint8_t _msb, const uint8_t _lsb)
	{
		return findIf(partParams(), [&](const Param& _p) { return _p.nrpnMsb == _msb && _p.nrpnLsb == _lsb; });
	}

	std::vector<std::string> valueList(const Param& _param)
	{
		switch(_param.text)
		{
		case ParamText::OffOn: return {"Off", "On"};
		case ParamText::MonoPoly: return {"Mono", "Poly"};
		case ParamText::PartMode: return {"Normal", "Drum Map 1", "Drum Map 2"};
		case ParamText::RxChannel:
			{
				std::vector<std::string> list;
				for(int c = 1; c <= 16; ++c)
					list.push_back(std::to_string(c));
				list.emplace_back("Off");
				return list;
			}
		case ParamText::ReverbMacro:
			return {"Room 1", "Room 2", "Room 3", "Hall 1", "Hall 2", "Plate", "Delay", "Panning Delay"};
		case ParamText::ChorusMacro:
			return {"Chorus 1", "Chorus 2", "Chorus 3", "Chorus 4", "Feedback Chorus", "Flanger", "Short Delay",
				"Short Delay (FB)"};
		case ParamText::DelayMacro:
			return {"Delay 1", "Delay 2", "Delay 3", "Delay 4", "Pan Delay 1", "Pan Delay 2", "Pan Delay 3",
				"Pan Delay 4",
				"Delay to Reverb", "Pan Repeat"};
		case ParamText::EqLowFreq: return {"200 Hz", "400 Hz"};
		case ParamText::EqHighFreq: return {"3 kHz", "6 kHz"};
		default: return {};
		}
	}

	std::vector<std::pair<uint16_t, std::string>> valueChoices(const Param& _param)
	{
		std::vector<std::pair<uint16_t, std::string>> choices;
		if(_param.text == ParamText::EfxType)
		{
			for(size_t i = 0; i < efxTypeCount(); ++i)
				choices.emplace_back(efxTypes()[i].id, efxTypes()[i].name);
			return choices;
		}
		const auto list = valueList(_param);
		for(size_t i = 0; i < list.size(); ++i)
			choices.emplace_back(static_cast<uint16_t>(_param.min + i), list[i]);
		return choices;
	}

	int efxParamIndex(const uint32_t _address)
	{
		return _address >= g_efxParamAddress && _address < g_efxParamAddress + g_efxParamCount
			? static_cast<int>(_address - g_efxParamAddress) : -1;
	}

	std::string formatValue(const Param& _param, const uint16_t _value)
	{
		if(_param.text == ParamText::EfxType)
			return findEfxType(_value).name;
		if(_param.text == ParamText::EfxSource)
			return formatEfxSource(static_cast<uint8_t>(_value));

		const auto list = valueList(_param);
		if(!list.empty())
		{
			const auto index = static_cast<size_t>(_value) - _param.min;
			return _value >= _param.min && index < list.size() ? list[index] : std::to_string(_value);
		}

		const int v = _value;
		char buf[32];
		switch(_param.text)
		{
		case ParamText::Signed:
			std::snprintf(buf, sizeof(buf), "%+d", v - 64);
			return buf;
		case ParamText::Pan:
		case ParamText::PartPan:
			if(v == 0 && _param.text == ParamText::PartPan)
				return "Rnd";
			if(v == 64)
				return "C";
			return v < 64 ? "L" + std::to_string(64 - v) : "R" + std::to_string(v - 64);
		case ParamText::Note:
			{
				constexpr const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
				return names[v % 12] + std::to_string(v / 12 - 1);
			}
		case ParamText::Decibel:
			std::snprintf(buf, sizeof(buf), "%+d dB", v - 64);
			return buf;
		case ParamText::Cents:
			std::snprintf(buf, sizeof(buf), "%+d ct", v - 64);
			return buf;
		case ParamText::MasterTune:
			std::snprintf(buf, sizeof(buf), "%+.1f ct", v * 0.1 - 102.4);
			return buf;
		case ParamText::DelayRatio:
			return std::to_string(v * 4) + " %";
		case ParamText::Milliseconds:
			return std::to_string(v) + " ms";
		case ParamText::Semitones:
			return std::to_string(v) + " st";
		case ParamText::FineTune:
			std::snprintf(buf, sizeof(buf), "%+.1f ct", (v - 64) * 100.0 / 64.0);
			return buf;
		default:
			return std::to_string(v);
		}
	}

	std::vector<std::pair<uint32_t, uint8_t>> macroPreset(const uint32_t _macroAddress, const uint8_t _value)
	{
		// The firmware's macro tables, in the order of the effect's parameters. A macro also clears the effect's
		// sends to the others: the reverb its 40 01 36 and pre delay, the chorus its sends to reverb and delay, the
		// delay its send to reverb.
		static constexpr uint8_t reverb[8][5] = {
			{0, 3, 64, 80, 0}, {1, 4, 64, 56, 0}, {2, 0, 64, 64, 0}, {3, 4, 64, 72, 0},
			{4, 0, 64, 64, 0}, {5, 0, 64, 88, 0}, {6, 0, 64, 32, 40}, {7, 0, 64, 64, 32},
		};
		static constexpr uint8_t chorus[8][6] = {
			{0, 64, 0, 112, 3, 5}, {0, 64, 5, 80, 9, 19}, {0, 64, 8, 80, 3, 19}, {0, 64, 16, 64, 9, 16},
			{0, 64, 64, 127, 2, 24}, {0, 64, 112, 127, 1, 5}, {0, 64, 0, 127, 0, 127}, {0, 64, 80, 127, 0, 127},
		};
		static constexpr uint8_t delay[10][10] = {
			{0, 97, 1, 1, 127, 0, 0, 64, 80, 0}, {0, 106, 1, 1, 127, 0, 0, 64, 80, 0},
			{0, 115, 1, 1, 127, 0, 0, 64, 72, 0}, {0, 83, 1, 1, 127, 0, 0, 64, 72, 0},
			{0, 105, 12, 24, 0, 125, 60, 64, 74, 0}, {0, 109, 12, 24, 0, 125, 60, 64, 71, 0},
			{0, 115, 12, 24, 0, 120, 64, 64, 73, 0}, {0, 93, 12, 24, 0, 120, 64, 64, 72, 0},
			{0, 109, 12, 24, 0, 114, 60, 64, 61, 36}, {0, 110, 21, 32, 97, 127, 67, 64, 40, 0},
		};

		std::vector<std::pair<uint32_t, uint8_t>> result;
		const auto add = [&](const uint32_t _first, const uint8_t* _values, const size_t _count)
		{
			for(size_t i = 0; i < _count; ++i)
				result.emplace_back(_first + static_cast<uint32_t>(i), _values[i]);
		};

		switch(_macroAddress)
		{
		case 0x400130:
			if(_value < 8)
			{
				add(0x400131, reverb[_value], 5);
				result.emplace_back(0x400136, 0);
				result.emplace_back(0x400137, 0);
			}
			break;
		case 0x400138:
			if(_value < 8)
			{
				add(0x400139, chorus[_value], 6);
				result.emplace_back(0x40013f, 0);
				result.emplace_back(0x400140, 0);
			}
			break;
		case 0x400150:
			if(_value < 10)
				add(0x400151, delay[_value], 10);
			break;
		default:
			break;
		}
		return result;
	}
}

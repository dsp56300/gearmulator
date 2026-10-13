#include "88lib/gs/efx.h"

#include <iterator>

namespace emu88Lib::gs
{
	namespace
	{
#include "88lib/gs/efxData.inc"

		std::string signedText(const int _value, const char* _unit)
		{
			return (_value > 0 ? "+" : "") + std::to_string(_value) + _unit;
		}
	}

	const EfxType* efxTypes()
	{
		return g_efxTypes;
	}

	size_t efxTypeCount()
	{
		return std::size(g_efxTypes);
	}

	const EfxType& findEfxType(const uint16_t _id)
	{
		for(const auto& type : g_efxTypes)
		{
			if(type.id == _id)
				return type;
		}
		return g_efxTypes[0];
	}

	bool isEfxType(const uint16_t _id)
	{
		return findEfxType(_id).id == _id;
	}

	const EfxParam* findEfxParam(const EfxType& _type, const size_t _index)
	{
		for(size_t i = 0; i < _type.paramCount; ++i)
		{
			const auto& p = g_efxParams[_type.firstParam + i];
			if(p.index == _index)
				return &p;
		}
		return nullptr;
	}

	std::vector<const EfxParam*> efxParams(const EfxType& _type)
	{
		std::vector<const EfxParam*> params;
		for(size_t i = 0; i < _type.paramCount; ++i)
			params.push_back(&g_efxParams[_type.firstParam + i]);
		return params;
	}

	std::vector<std::string> efxValueList(const EfxParam& _param)
	{
		switch(_param.format)
		{
		case EfxFormat::LowFreq: return {"200 Hz", "400 Hz"};
		case EfxFormat::HighFreq: return {"4 kHz", "8 kHz"};
		case EfxFormat::Q: return {"0.5", "1.0", "2.0", "4.0", "9.0"};
		case EfxFormat::AmpType: return {"Small", "Built-In", "2-Stack", "3-Stack"};
		case EfxFormat::FilterType: return {"LPF", "BPF"};
		case EfxFormat::Polarity: return {"Down", "Up"};
		case EfxFormat::ModWave: return {"Tri", "Sqr", "Sin", "Saw1", "Saw2"};
		case EfxFormat::Output: return {"Speaker", "Phones"};
		case EfxFormat::Vowel: return {"a", "e", "i", "o", "u"};
		case EfxFormat::ReverbType: return {"Room 1", "Room 2", "Stage 1", "Stage 2", "Hall 1", "Hall 2"};
		case EfxFormat::GateType: return {"Normal", "Reverse", "Sweep 1", "Sweep 2"};
		case EfxFormat::FeedbackMode: return {"Normal", "Cross"};
		case EfxFormat::Phase: return {"Normal", "Invert"};
		case EfxFormat::PreFilter: return {"Off", "LPF", "HPF"};
		case EfxFormat::OdSelect: return {"Overdrive", "Distortion"};
		case EfxFormat::ChorusFlanger: return {"Chorus", "Flanger"};
		case EfxFormat::TremoloPan: return {"Tremolo", "Auto Pan"};
		case EfxFormat::PostGain: return {"0 dB", "+6 dB", "+12 dB", "+18 dB"};
		case EfxFormat::Ratio: return {"1.5:1", "2:1", "4:1", "100:1"};
		case EfxFormat::MonoStereo: return {"Mono", "Stereo"};
		case EfxFormat::OffOn: return {"Off", "On"};
		default: return {};
		}
	}

	std::string formatEfxValue(const EfxParam& _param, const uint8_t _value)
	{
		const auto list = efxValueList(_param);
		if(!list.empty())
		{
			const auto index = static_cast<size_t>(_value) - _param.min;
			return _value >= _param.min && index < list.size() ? list[index] : std::to_string(_value);
		}

		const int v = _value;
		switch(_param.format)
		{
		case EfxFormat::Gain: return signedText(v - 64, " dB");
		case EfxFormat::Pan:
			if(v == 64)
				return "C";
			return v < 64 ? "L" + std::to_string(64 - v) : "R" + std::to_string(v - 64);
		case EfxFormat::Feedback: return signedText((v - 64) * 2, " %");
		case EfxFormat::Semitones: return signedText(v - 64, "");
		case EfxFormat::Cents: return signedText((v - 64) * 2, " ct");
		case EfxFormat::RotarySpeed: return v < 64 ? "Slow" : "Fast";
		default: return std::to_string(v);
		}
	}

	std::string formatEfxSource(const uint8_t _value)
	{
		if(_value == 0)
			return "Off";
		if(_value <= 95)
			return "CC" + std::to_string(_value);
		if(_value == 96)
			return "CAf";
		if(_value == 97)
			return "Bend";
		return std::to_string(_value);
	}
}

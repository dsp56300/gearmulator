#include "88lib/mt32/params.h"

#include <array>
#include <cmath>
#include <cstdio>

namespace emu88Lib::mt32
{
	namespace
	{
		Param p(const char* _id, const char* _section, const char* _label, const uint8_t _offset, const uint8_t _max,
			const ParamText _text = ParamText::Number, const uint8_t _center = 0)
		{
			return {_id, _section, _label, _offset, _max, _max, _text, _center};
		}

		Param old(Param _p, const uint8_t _maxOld)
		{
			_p.maxOld = _maxOld;
			return _p;
		}

		std::vector<Param> createLaSystem()
		{
			std::vector<Param> v = {
				p("MasterTune", "System", "Master Tune", 0x00, 127, ParamText::MasterTune),
				p("ReverbMode", "Reverb", "Mode", 0x01, 3, ParamText::ReverbMode),
				p("ReverbTime", "Reverb", "Time", 0x02, 7),
				p("ReverbLevel", "Reverb", "Level", 0x03, 7),
			};
			static const char* reserveIds[] = {"Reserve1", "Reserve2", "Reserve3", "Reserve4", "Reserve5",
				"Reserve6", "Reserve7", "Reserve8", "ReserveR"};
			static const char* reserveLabels[] = {"Part 1", "Part 2", "Part 3", "Part 4", "Part 5", "Part 6",
				"Part 7", "Part 8", "Rhythm"};
			for(uint8_t i = 0; i < g_laParts; ++i)
				v.push_back(p(reserveIds[i], "Partial Reserve", reserveLabels[i], static_cast<uint8_t>(0x04 + i), 32));
			static const char* channelIds[] = {"Channel1", "Channel2", "Channel3", "Channel4", "Channel5",
				"Channel6", "Channel7", "Channel8", "ChannelR"};
			for(uint8_t i = 0; i < g_laParts; ++i)
				v.push_back(p(channelIds[i], "MIDI Channel", reserveLabels[i], static_cast<uint8_t>(0x0d + i), 16,
					ParamText::MidiChannel));
			v.push_back(p("MasterVolume", "System", "Master Volume", 0x16, 100));
			return v;
		}

		std::vector<Param> createPatch(const bool _temp)
		{
			std::vector<Param> v = {
				p("TimbreGroup", "Patch", "Timbre Group", 0x00, 3, ParamText::TimbreGroup),
				p("TimbreNumber", "Patch", "Timbre Number", 0x01, 63, ParamText::TimbreNumber),
				p("KeyShift", "Patch", "Key Shift", 0x02, 48, ParamText::Signed, 24),
				p("FineTune", "Patch", "Fine Tune", 0x03, 100, ParamText::Signed, 50),
				p("BenderRange", "Patch", "Bender Range", 0x04, 24),
				old(p("AssignMode", "Patch", "Assign Mode", 0x05, 3, ParamText::AssignMode), 4),
				p("ReverbSwitch", "Patch", "Reverb", 0x06, 1, ParamText::OffOn),
			};
			if(_temp)
			{
				v.push_back(p("OutputLevel", "Part", "Output Level", 0x08, 100));
				v.push_back(p("Pan", "Part", "Pan", 0x09, 14, ParamText::PanLa));
			}
			return v;
		}

		std::vector<Param> createRhythmKey()
		{
			return {
				p("Timbre", "Rhythm", "Timbre", 0x00, 127, ParamText::RhythmTimbre),
				p("OutputLevel", "Rhythm", "Output Level", 0x01, 100),
				p("Pan", "Rhythm", "Pan", 0x02, 14, ParamText::PanLa),
				// The new firmware stores up to 7 here, the old one 1
				old(p("ReverbSwitch", "Rhythm", "Reverb", 0x03, 7, ParamText::OffOn), 1),
			};
		}

		std::vector<Param> createTimbreCommon()
		{
			auto name = p("Name", "Common", "Name", 0x00, 127, ParamText::Name);
			name.size = 10;
			return {
				name,
				p("Structure12", "Common", "Structure 1 & 2", 0x0a, 12, ParamText::Structure),
				p("Structure34", "Common", "Structure 3 & 4", 0x0b, 12, ParamText::Structure),
				p("PartialMute", "Common", "Partial Mute", 0x0c, 15, ParamText::PartialMute),
				p("EnvMode", "Common", "Env Mode", 0x0d, 1, ParamText::EnvMode),
			};
		}

		std::vector<Param> createPartial()
		{
			return {
				p("PitchCoarse", "WG", "Pitch Coarse", 0x00, 96, ParamText::PitchCoarse),
				p("PitchFine", "WG", "Pitch Fine", 0x01, 100, ParamText::Signed, 50),
				p("PitchKeyfollow", "WG", "Pitch Keyfollow", 0x02, 16, ParamText::PitchKeyfollow),
				p("BenderSwitch", "WG", "Bender", 0x03, 1, ParamText::OffOn),
				old(p("Waveform", "WG", "Waveform / PCM Bank", 0x04, 3, ParamText::Waveform), 1),
				p("PcmWave", "WG", "PCM Wave", 0x05, 127, ParamText::PcmWave),
				p("PulseWidth", "WG", "Pulse Width", 0x06, 100),
				p("PwVeloSens", "WG", "PW Velo Sens", 0x07, 14, ParamText::Signed, 7),

				p("PenvDepth", "P-ENV", "Depth", 0x08, 10),
				old(p("PenvVeloSens", "P-ENV", "Velo Sens", 0x09, 3), 100),
				p("PenvTimeKeyf", "P-ENV", "Time Keyfollow", 0x0a, 4),
				p("PenvTime1", "P-ENV", "Time 1", 0x0b, 100),
				p("PenvTime2", "P-ENV", "Time 2", 0x0c, 100),
				p("PenvTime3", "P-ENV", "Time 3", 0x0d, 100),
				p("PenvTime4", "P-ENV", "Time 4", 0x0e, 100),
				p("PenvLevel0", "P-ENV", "Level 0", 0x0f, 100, ParamText::Signed, 50),
				p("PenvLevel1", "P-ENV", "Level 1", 0x10, 100, ParamText::Signed, 50),
				p("PenvLevel2", "P-ENV", "Level 2", 0x11, 100, ParamText::Signed, 50),
				p("PenvSustain", "P-ENV", "Sustain Level", 0x12, 100, ParamText::Signed, 50),
				p("PenvEnd", "P-ENV", "End Level", 0x13, 100, ParamText::Signed, 50),

				p("LfoRate", "P-LFO", "Rate", 0x14, 100),
				p("LfoDepth", "P-LFO", "Depth", 0x15, 100),
				p("LfoModSens", "P-LFO", "Mod Sens", 0x16, 100),

				p("TvfCutoff", "TVF", "Cutoff", 0x17, 100),
				p("TvfResonance", "TVF", "Resonance", 0x18, 30),
				p("TvfKeyfollow", "TVF", "Keyfollow", 0x19, 14, ParamText::TvfKeyfollow),
				p("TvfBiasPoint", "TVF", "Bias Point", 0x1a, 127, ParamText::BiasPoint),
				p("TvfBiasLevel", "TVF", "Bias Level", 0x1b, 14, ParamText::Signed, 7),
				p("TvfEnvDepth", "TVF", "Env Depth", 0x1c, 100),
				p("TvfEnvVeloSens", "TVF", "Env Velo Sens", 0x1d, 100),
				p("TvfEnvDepthKeyf", "TVF", "Env Depth Keyfollow", 0x1e, 4),
				p("TvfEnvTimeKeyf", "TVF", "Env Time Keyfollow", 0x1f, 4),
				p("TvfTime1", "TVF", "Time 1", 0x20, 100),
				p("TvfTime2", "TVF", "Time 2", 0x21, 100),
				p("TvfTime3", "TVF", "Time 3", 0x22, 100),
				p("TvfTime4", "TVF", "Time 4", 0x23, 100),
				p("TvfTime5", "TVF", "Time 5", 0x24, 100),
				p("TvfLevel1", "TVF", "Level 1", 0x25, 100),
				p("TvfLevel2", "TVF", "Level 2", 0x26, 100),
				p("TvfLevel3", "TVF", "Level 3", 0x27, 100),
				p("TvfSustain", "TVF", "Sustain Level", 0x28, 100),

				p("TvaLevel", "TVA", "Level", 0x29, 100),
				p("TvaVeloSens", "TVA", "Velo Sens", 0x2a, 100, ParamText::Signed, 50),
				p("TvaBiasPoint1", "TVA", "Bias Point 1", 0x2b, 127, ParamText::BiasPoint),
				p("TvaBiasLevel1", "TVA", "Bias Level 1", 0x2c, 12, ParamText::BiasLevelTva),
				p("TvaBiasPoint2", "TVA", "Bias Point 2", 0x2d, 127, ParamText::BiasPoint),
				p("TvaBiasLevel2", "TVA", "Bias Level 2", 0x2e, 12, ParamText::BiasLevelTva),
				p("TvaEnvTimeKeyf", "TVA", "Env Time Keyfollow", 0x2f, 4),
				p("TvaEnvTimeVelo", "TVA", "Env Time Velo Follow", 0x30, 4),
				p("TvaTime1", "TVA", "Time 1", 0x31, 100),
				p("TvaTime2", "TVA", "Time 2", 0x32, 100),
				p("TvaTime3", "TVA", "Time 3", 0x33, 100),
				p("TvaTime4", "TVA", "Time 4", 0x34, 100),
				p("TvaTime5", "TVA", "Time 5", 0x35, 100),
				p("TvaLevel1", "TVA", "Level 1", 0x36, 100),
				p("TvaLevel2", "TVA", "Level 2", 0x37, 100),
				p("TvaLevel3", "TVA", "Level 3", 0x38, 100),
				p("TvaSustain", "TVA", "Sustain Level", 0x39, 100),
			};
		}

		std::vector<Param> createPcmSystem()
		{
			std::vector<Param> v = {
				p("MasterTune", "System", "Master Tune", 0x00, 127, ParamText::MasterTune),
				p("ReverbMode", "Reverb", "Mode", 0x01, 3, ParamText::ReverbMode),
				p("ReverbTime", "Reverb", "Time", 0x02, 7),
				p("ReverbLevel", "Reverb", "Level", 0x03, 7),
			};
			static const char* reserveIds[] = {"Reserve1", "Reserve2", "Reserve3", "Reserve4", "Reserve5",
				"Reserve6"};
			static const char* channelIds[] = {"Channel1", "Channel2", "Channel3", "Channel4", "Channel5",
				"Channel6"};
			static const char* labels[] = {"Part 1", "Part 2", "Part 3", "Part 4", "Part 5", "Part 6"};
			for(uint8_t i = 0; i < g_pcmParts; ++i)
				v.push_back(p(reserveIds[i], "Partial Reserve", labels[i], static_cast<uint8_t>(0x04 + i), 31));
			for(uint8_t i = 0; i < g_pcmParts; ++i)
				v.push_back(p(channelIds[i], "MIDI Channel", labels[i], static_cast<uint8_t>(0x0a + i), 16,
					ParamText::MidiChannel));
			v.push_back(p("MasterVolume", "System", "Master Volume", 0x10, 100));
			return v;
		}

		std::vector<Param> createPcmPatch(const bool _temp)
		{
			std::vector<Param> v = {
				p("ToneMedia", "Patch", "Tone Media", 0x00, 1, ParamText::ToneMedia),
				p("ToneNumber", "Patch", "Tone Number", 0x01, 127, ParamText::ToneNumber),
				p("KeyShift", "Patch", "Key Shift", 0x02, 24, ParamText::Signed, 12),
				p("FineTune", "Patch", "Fine Tune", 0x03, 100, ParamText::Signed, 50),
				p("BenderRange", "Patch", "Bender Range", 0x04, 12),
				p("KeyRangeLower", "Patch", "Key Range Lower", 0x05, 127, ParamText::Note),
				p("KeyRangeUpper", "Patch", "Key Range Upper", 0x06, 127, ParamText::Note),
				p("AssignMode", "Patch", "Assign Mode", 0x07, 3, ParamText::AssignMode),
				p("ReverbSwitch", "Patch", "Reverb", 0x08, 1, ParamText::OffOn),
				p("VelocitySens", "Patch", "Velocity Sens", 0x09, 15),
				p("EnvAttack", "Envelope", "Attack Rate", 0x0a, 127),
				p("EnvRelease", "Envelope", "Release Rate", 0x0b, 127),
				p("LfoRate", "LFO", "Rate", 0x0c, 127),
				p("LfoAutoDelay", "LFO", "Auto Delay Time", 0x0d, 15),
				p("LfoAutoRise", "LFO", "Auto Rise Time", 0x0e, 15),
				p("LfoAutoDepth", "LFO", "Auto Depth", 0x0f, 15),
				p("LfoManRise", "LFO", "Manual Rise Time", 0x10, 15),
				p("LfoManDepth", "LFO", "Manual Depth", 0x11, 15),
				p("DetuneDepth", "Patch", "Detune Depth", 0x12, 50),
			};
			if(_temp)
			{
				v.push_back(p("Pan", "Part", "Pan", 0x13, 127, ParamText::PanPcm));
				v.push_back(p("OutputLevel", "Part", "Output Level", 0x14, 100));
			}
			return v;
		}

		const std::array<std::vector<Param>, static_cast<size_t>(Block::Count)>& tables()
		{
			static const std::array<std::vector<Param>, static_cast<size_t>(Block::Count)> t = {
				createLaSystem(), createPatch(true), createRhythmKey(), createPatch(false), createTimbreCommon(),
				createPartial(), createPcmSystem(), createPcmPatch(true), createPcmPatch(false),
			};
			return t;
		}

		constexpr const char* g_noteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
	}

	const std::vector<Param>& params(const Block _block)
	{
		return tables()[static_cast<size_t>(_block)];
	}

	Block blockOf(const Area _area)
	{
		switch(_area)
		{
		case Area::PatchTemp: return Block::PatchTemp;
		case Area::RhythmSetup: return Block::RhythmKey;
		case Area::PatchMemory: return Block::PatchMemory;
		case Area::LaSystem: return Block::LaSystem;
		case Area::PcmPatchTemp: return Block::PcmPatchTemp;
		case Area::PcmPatchMemory: return Block::PcmPatchMemory;
		case Area::PcmSystem: return Block::PcmSystem;
		// A timbre is a common block and four partials; storeByte() takes them by their own offsets
		default: return Block::TimbreCommon;
		}
	}

	size_t blockSize(const Block _block)
	{
		switch(_block)
		{
		case Block::LaSystem: return g_laSystemSize;
		case Block::PatchTemp: return g_patchTempSize;
		case Block::RhythmKey: return g_rhythmKeySize;
		case Block::PatchMemory: return g_patchMemorySize;
		case Block::TimbreCommon: return g_timbreCommonSize;
		case Block::Partial: return g_partialSize;
		case Block::PcmSystem: return g_pcmSystemSize;
		case Block::PcmPatchTemp: return g_pcmPatchTempSize;
		case Block::PcmPatchMemory: return g_pcmPatchMemorySize;
		default: return 0;
		}
	}

	int findParam(const Block _block, const uint8_t _offset)
	{
		const auto& list = params(_block);
		for(size_t i = 0; i < list.size(); ++i)
		{
			if(_offset >= list[i].offset && _offset < list[i].offset + list[i].size)
				return static_cast<int>(i);
		}
		return -1;
	}

	int findParamById(const Block _block, const std::string& _id)
	{
		const auto& list = params(_block);
		for(size_t i = 0; i < list.size(); ++i)
		{
			if(_id == list[i].id)
				return static_cast<int>(i);
		}
		return -1;
	}

	uint8_t maxValue(const Param& _param, const LaFirmware _firmware)
	{
		return _firmware == LaFirmware::Old ? _param.maxOld : _param.max;
	}

	bool storeByte(const Block _block, const uint8_t _offset, const LaFirmware _firmware, uint8_t& _value)
	{
		_value &= 0x7f;
		const bool oldFirmware = _firmware == LaFirmware::Old;
		// The dummy bytes: the new firmware stores some of them as they come, the old one none
		if(_block == Block::PatchTemp && (_offset == 0x07 || _offset == 0x0a || _offset == 0x0b))
			return !oldFirmware;
		if(_block == Block::PatchMemory && _offset == 0x07)
			return !oldFirmware;
		const auto index = findParam(_block, _offset);
		if(index < 0)
			return false;
		const auto& param = params(_block)[index];
		if(param.text == ParamText::Name)
			return true;
		const auto max = maxValue(param, _firmware);
		if(_value > max)
			_value = max;
		return true;
	}

	std::vector<std::string> valueList(const Param& _param, const LaFirmware _firmware)
	{
		std::vector<std::string> list;
		switch(_param.text)
		{
		case ParamText::OffOn: return {"Off", "On"};
		case ParamText::TimbreGroup: return {"a", "b", "i", "r"};
		// Single: a note played again ends the one sounding, multi: they sound together. Last or first: which notes
		// keep their partials when they run out.
		case ParamText::AssignMode:
			return {"POLY 1: single, last", "POLY 2: single, first", "POLY 3: multi, last", "POLY 4: multi, first"};
		case ParamText::ReverbMode: return {"Room", "Hall", "Plate", "Tap Delay"};
		case ParamText::EnvMode: return {"Normal", "No Sustain"};
		case ParamText::ToneMedia: return {"Internal", "Card"};
		case ParamText::PitchKeyfollow:
			return {"-1", "-1/2", "-1/4", "0", "1/8", "1/4", "3/8", "1/2", "5/8", "3/4", "7/8", "1", "5/4", "3/2", "2",
				"s1", "s2"};
		case ParamText::TvfKeyfollow:
			return {"-1", "-1/2", "-1/4", "0", "1/8", "1/4", "3/8", "1/2", "5/8", "3/4", "7/8", "1", "5/4", "3/2", "2"};
		case ParamText::Waveform:
			if(_firmware == LaFirmware::Old)
				return {"SQU/1", "SAW/1"};
			return {"SQU/1", "SAW/1", "SQU/2", "SAW/2"};
		default:
			break;
		}
		if(_param.text == ParamText::Number || _param.text == ParamText::Name)
			return list;
		for(uint32_t v = 0; v <= maxValue(_param, _firmware); ++v)
			list.push_back(formatValue(_param, static_cast<uint8_t>(v)));
		return list;
	}

	std::string noteName(const uint8_t _note)
	{
		return std::string(g_noteNames[_note % 12]) + std::to_string(static_cast<int>(_note / 12) - 1);
	}

	std::string formatValue(const Param& _param, const uint8_t _value)
	{
		char buf[32];
		switch(_param.text)
		{
		case ParamText::Number:
		case ParamText::Name:
			return std::to_string(_value);
		case ParamText::Signed:
		{
			const int v = static_cast<int>(_value) - _param.center;
			return v > 0 ? "+" + std::to_string(v) : std::to_string(v);
		}
		case ParamText::TimbreNumber:
		case ParamText::Structure:
		case ParamText::PcmWave:
		case ParamText::ToneNumber:
			return std::to_string(_value + 1);
		case ParamText::MasterTune:
			// One semitone over 128 steps, 440 Hz at 64. The CM-32L's manual gives 427.5-452.6 Hz, which this is;
			// the MT-32's and the CM-32P's quote 432.1-457.6, unmeasured here.
			snprintf(buf, sizeof(buf), "%.1f Hz", 440.0 * std::pow(2.0, (static_cast<int>(_value) - 64) / 1536.0));
			return buf;
		case ParamText::PanLa:
		{
			const int v = static_cast<int>(_value) - 7;
			return v == 0 ? "C" : v < 0 ? "R" + std::to_string(-v) : "L" + std::to_string(v);
		}
		case ParamText::PanPcm:
		{
			const int v = static_cast<int>(_value) - 64;
			return v == 0 ? "C" : v < 0 ? "R" + std::to_string(-v) : "L" + std::to_string(v);
		}
		case ParamText::MidiChannel:
			return _value >= 16 ? "OFF" : std::to_string(_value + 1);
		case ParamText::RhythmTimbre:
			snprintf(buf, sizeof(buf), "%c%02d", _value < 64 ? 'i' : 'r', (_value & 63) + 1);
			return buf;
		case ParamText::PartialMute:
			for(int i = 0; i < 4; ++i)
				buf[i] = (_value >> i) & 1 ? static_cast<char>('1' + i) : '-';
			buf[4] = 0;
			return buf;
		case ParamText::PitchCoarse:
			return noteName(static_cast<uint8_t>(_value + 24));
		case ParamText::BiasPoint:
			// 64 steps from A1 up, below (<) or above (>) the point
			return std::string(_value < 64 ? "<" : ">") + noteName(static_cast<uint8_t>((_value & 63) + 33));
		case ParamText::BiasLevelTva:
			return std::to_string(static_cast<int>(_value) - 12);
		case ParamText::Note:
			return noteName(_value);
		default:
		{
			const auto list = valueList(_param);
			return _value < list.size() ? list[_value] : std::to_string(_value);
		}
		}
	}

	namespace
	{
		// By value: whether the first and the second partial play PCM, and their mix. The MT-32 manual draws them; Munt
		// and MT32edit4v have the same table.
		constexpr Structure g_structures[13] = {
			{{false, false}, StructureMix::Mix},		// 1 S1 + S2
			{{false, false}, StructureMix::RingAndFirst},
			{{true, false}, StructureMix::Mix},
			{{true, false}, StructureMix::RingAndFirst},
			{{false, true}, StructureMix::RingAndFirst},
			{{true, true}, StructureMix::Mix},
			{{true, true}, StructureMix::RingAndFirst},
			{{false, false}, StructureMix::Stereo},
			{{true, true}, StructureMix::Stereo},
			{{false, false}, StructureMix::Ring},
			{{true, false}, StructureMix::Ring},
			{{false, true}, StructureMix::Ring},
			{{true, true}, StructureMix::Ring},		// 13 (P1 + P2) -> R
		};
	}

	Structure structure(const uint8_t _value)
	{
		return g_structures[_value < 13 ? _value : 0];
	}

	bool structureValue(const StructureMix _mix, const bool _firstPcm, const bool _secondPcm, uint8_t& _value)
	{
		for(uint8_t v = 0; v < 13; ++v)
		{
			const auto& s = g_structures[v];
			if(s.mix == _mix && s.pcm[0] == _firstPcm && s.pcm[1] == _secondPcm)
			{
				_value = v;
				return true;
			}
		}
		return false;
	}

	std::string structureName(const uint8_t _value, const int _firstPartial)
	{
		const auto s = structure(_value);
		const auto a = std::string(s.pcm[0] ? "P" : "S") + std::to_string(_firstPartial);
		const auto b = std::string(s.pcm[1] ? "P" : "S") + std::to_string(_firstPartial + 1);
		switch(s.mix)
		{
		case StructureMix::RingAndFirst: return "(" + a + " + " + b + " \u2192 R) + " + a;
		case StructureMix::Ring: return "(" + a + " + " + b + ") \u2192 R";
		case StructureMix::Stereo: return a + " left, " + b + " right";
		default: return a + " + " + b;
		}
	}

	namespace
	{
		constexpr const char* g_pcmWaves[2][128] = {{
			"Ac. Bass Drum", "Ac. Snare Drum", "El. Snare Drum", "Electric Tom", "Closed Hihat", "Open Hihat",
			"Crash Cymbal", "Crash Cymbal (loop)", "Ride cymbal", "Rim Shot", "Hand Clap", "Muted Conga", "Conga",
			"Bongo", "Cowbell", "Tambourine", "Agogo Bell", "Claves", "Timbale", "Cabasa", "Keypress", "Perc Organ",
			"Trombone", "Trumpet", "Breath Noise (loop)", "Clarinet", "Flute", "Pan Pipes", "Shakuhachi", "Alto Sax",
			"Baritone Sax", "Marimba", "Glockenspiel", "Xylophone", "Tubular Bells", "Fingered Bass", "Slap Bass",
			"Picked Bass (loop)", "Acoustic Bass", "Nylon Guitar", "Steel Guitar", "Pizzicato", "Harp",
			"Harpsichord (loop)", "Bow string", "Violin", "Timpani", "Orchestra Hit", "Flute", "Organ (loop)",
			"Bowed Glass (loop)", "Telephone", "Bowed Glass", "Reverse Cymbal", "Ac. Bass Drum #",
			"Ac. Snare Drum #", "El. Snare Drum #", "Ac. Tom #", "Closed Hihat #", "Open Hihat #", "Crash Cymbal #",
			"Crash Cymbal (loop) #", "Ride Cymbal #", "Rim shot #", "Hand clap #", "Mute Conga #", "Conga #",
			"Bongo #", "Cowbell #", "Tambourine #", "Agogo #", "Claves #", "Timbale #", "Cabasa #",
			"Bass Drum (loop)", "Snare (loop)", "Acoustic Tom (loop)", "Electric Tom (loop)", "Hihat (loop)",
			"Crash Cymbal (loop)", "Ride cymbal (loop)", "Ride cymbal 2 (loop)", "Rim (loop)", "Hand clap (loop)",
			"Bongo (loop)", "Conga (loop)", "Muted conga (loop)", "Cowbell (loop)", "Tambourine (loop)",
			"Agogo (loop)", "Woodblock (loop)", "Timbales (loop)", "Maracas (loop)", "Sticks (loop)",
			"Perc Organ (loop)", "Trombone (loop)", "Trumpet (loop)", "Clarinet (loop)", "Piccolo (loop)",
			"Pan Pipe (loop)", "Breath Noise (loop)", "Alto Sax (loop)", "Baritone Sax (loop)", "Xylophone (loop)",
			"Glockenspiel (loop)", "Marimba (loop)", "Tubular Bells (loop)", "Fingered Bass (loop)",
			"Slap Bass (loop)", "Acoustic Bass (loop)", "Nylon Guitar (loop)", "Steel Guitar (loop)",
			"Pizzicato (loop)", "Harp (loop)", "Bowed string (loop)", "String Ensemble (loop)", "Timpani (loop)",
			"Orchestra Hit (loop)", "Flute (loop)", "Perc. loop 1", "Perc. loop 2", "Orch&Perc loop",
			"Wind&Perc loop", "Guitar & Bass loop", "Orchestra loop", "Perc. loop 3", "Bass & Perc. loop",
			"Bass & Snare loop",
		}, {
			"Laugh #", "Applause #", "Windchime #", "Crash #", "Train #", "Wind #", "Bird #", "Stream #",
			"Door Creak #", "Scream #", "Punch #", "Footsteps #", "Door Slam #", "Car Start #", "Aircraft #",
			"Gun Shot #", "Horse #", "Thunder #", "Bubble #", "Heartbeat #", "Engine #", "Tire Screech #", "Siren #",
			"Helicopter #", "Dog Bark #", "Car Pass #", "Male Voice #", "Machine Gun #", "Starship #",
			"Laugh (Loop) #", "Applause (Loop) #", "Windchime (Loop) #", "Crash (Loop) #", "Train (Loop) #",
			"Wind (Loop) #", "Bird (Loop) #", "Stream (Loop) #", "Door Creak (Loop) #", "Scream (Loop) #",
			"Punch (Loop) #", "Footsteps (Loop) #", "Door Slam (Loop) #", "Car Start (Loop) #", "Aircraft (Loop) #",
			"Gun Shot (Loop) #", "Horse (Loop) #", "Thunder (Loop) #", "Bubble (Loop) #", "Heartbeat (Loop) #",
			"Engine (Loop) #", "Tire Screech (Loop) #", "Siren (Loop) #", "Helicopter (Loop) #", "Dog Bark (Loop) #",
			"Car Pass (Loop) #", "Male Voice (Loop) #", "Machine Gun (Loop) #", "Starship (Loop) #", "Jam-1 (Loop)",
			"Jam-2 (Loop)", "Jam-3 (Loop)", "Jam-4 (Loop)", "Jam-5 (Loop)", "Jam-6 (Loop)", "Jam-7 (Loop)",
			"Jam-8 (Loop)", "Jam-9 (Loop)", "Jam-10 (Loop)", "Jam-11 (Loop)", "Jam-12 (Loop)", "Jam-13 (Loop)",
			"Jam-14 (Loop)", "Jam-15 (Loop)", "Jam-16 (Loop)", "Jam-17 (Loop)", "Jam-18 (Loop)", "Jam-19 (Loop)",
			"Jam-20 (Loop)", "Jam-21 (Loop)", "Jam-22 (Loop)", "Jam-23 (Loop)", "Jam-24 (Loop)", "Jam-25 (Loop)",
			"Jam-26 (Loop)", "Jam-27 (Loop)", "Jam-28 (Loop)", "Jam-29 (Loop)", "Jam-30 (Loop)", "Jam-31 (Loop)",
			"Jam-32 (Loop)", "Jam-33 (Loop)", "Jam-34 (Loop)", "Jam-35 (Loop)", "Jam-36 (Loop)", "Jam-37 (Loop)",
			"Jam-38 (Loop)", "Jam-39 (Loop)", "Jam-40 (Loop)", "Shot-1", "Shot-2", "Shot-3", "Shot-4", "Shot-5",
			"Shot-6", "Shot-7", "Shot-8", "Shot-9", "Shot-10", "Shot-11", "Shot-12", "Shot-13", "Shot-14", "Shot-15",
			"Shot-16", "Shot-17", "Shot-18", "Shot-19", "Shot-20", "Shot-21", "Shot-22", "Shot-23", "Shot-24",
			"Shot-25", "Shot-26", "Alto Sax", "Shakuhachi", "Marimba", "Dog Bark",
		}};
	}

	const char* pcmWaveName(const uint8_t _bank, const uint8_t _wave)
	{
		return g_pcmWaves[_bank & 1][_wave & 0x7f];
	}
}

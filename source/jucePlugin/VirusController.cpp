#include "VirusController.h"
#include "PluginProcessor.h"

// TODO: all sysex structs can be refactored to common instead of including this!
#include "../virusLib/microcontroller.h"

using MessageType = virusLib::Microcontroller::SysexMessageType;

namespace Virus
{
    static constexpr uint8_t kSysExStart[] = {0xf0, 0x00, 0x20, 0x33, 0x01};
    static constexpr auto kHeaderWithMsgCodeLen = 7;

    Controller::Controller(AudioPluginAudioProcessor &p, unsigned char deviceId) : m_processor(p), m_deviceId(deviceId)
    {
        registerParams();
		// add lambda to enforce updating patches when virus switch from/to multi/single.
		(findSynthParam(0, 0x72, 0x7a))->onValueChanged = [this] {
			const uint8_t prg = isMultiMode() ? 0x0 : 0x40;
			sendSysEx(constructMessage({MessageType::REQUEST_SINGLE, 0x0, prg}));
		};
		sendSysEx(constructMessage({MessageType::REQUEST_TOTAL}));
		sendSysEx(constructMessage({MessageType::REQUEST_ARRANGEMENT}));
		startTimer(5);
	}

    void Controller::registerParams()
    {
        // 16 parts * 3 pages * 128 params
        // TODO: not register internal/unused params?
		auto globalParams = std::make_unique<juce::AudioProcessorParameterGroup>("global", "Global", "|");
		for (uint8_t pt = 0; pt < 16; pt++)
		{
			const auto partNumber = juce::String(pt + 1);
			auto group =
				std::make_unique<juce::AudioProcessorParameterGroup>("ch" + partNumber, "Ch " + partNumber, "|");
			for (auto desc : m_paramsDescription)
			{
				ParamIndex idx = {static_cast<uint8_t>(desc.page), pt, desc.index};
				auto p = std::make_unique<Parameter>(*this, desc, pt);
				const bool isNonPartExclusive = (desc.classFlags & Parameter::Class::GLOBAL) ||
					(desc.classFlags & Parameter::Class::NON_PART_SENSITIVE);
				if (isNonPartExclusive)
				{
					if (pt != 0)
						continue; // only register on first part!
				}
				if (p->getDescription().isPublic)
				{
					// lifecycle managed by host
					m_synthParams.insert_or_assign(idx, p.get());
					if (isNonPartExclusive)
					{
						jassert(pt == 0);
						globalParams->addChild(std::move(p));
					}
					else
						group->addChild(std::move(p));
				}
				else
					m_synthInternalParams.insert_or_assign(idx, std::move(p));
			}
			m_processor.addParameterGroup(std::move(group));
		}
		m_processor.addParameterGroup(std::move(globalParams));
	}

	void Controller::parseMessage(const SysEx &msg)
	{
        if (msg.size() < 8)
            return; // shorter than expected!

        if (msg[msg.size() - 1] != 0xf7)
            return; // invalid end?!?

        for (auto i = 0; i < msg.size(); ++i)
        {
            if (i < 5)
            {
                if (msg[i] != kSysExStart[i])
                    return; // invalid header
            }
            else if (i == 5)
            {
                if (msg[i] != m_deviceId)
                    return; // not intended to this device!
            }
            else if (i == 6)
            {
                switch (msg[i])
                {
                case MessageType::DUMP_SINGLE:
                    parseSingle(msg);
                    break;
                case MessageType::DUMP_MULTI:
                    parseMulti(msg);
                    break;
                case MessageType::PARAM_CHANGE_A:
                case MessageType::PARAM_CHANGE_B:
                case MessageType::PARAM_CHANGE_C:
                    parseParamChange(msg);
                    break;
                default:
                    std::cout << "Controller: Begin Unhandled SysEx! --" << std::endl;
                    printMessage(msg);
                    std::cout << "Controller: End Unhandled SysEx! --" << std::endl;
                }
            }
        }
    }

	Parameter *Controller::findSynthParam(const uint8_t ch, const uint8_t bank, const uint8_t paramIndex)
	{
		auto it = m_synthParams.find({static_cast<uint8_t>(bank), ch, paramIndex});
		if (it == m_synthParams.end())
		{
			auto iti = m_synthInternalParams.find({static_cast<uint8_t>(bank), ch, paramIndex});
			if (iti == m_synthInternalParams.end())
				return nullptr;
			else
				return iti->second.get();
		}
		return it->second;
	}

	juce::Value *Controller::getParam(uint8_t ch, uint8_t bank, uint8_t paramIndex)
	{
		auto *param = findSynthParam(ch, static_cast<uint8_t>(0x70 + bank), paramIndex);
		if (param == nullptr)
		{
            // unregistered param?
            jassertfalse;
            return nullptr;
        }
		return &param->getValueObject();
	}

	void Controller::parseParamChange(const SysEx &msg)
    {
        const auto pos = kHeaderWithMsgCodeLen - 1;
        const auto value = msg[pos + 3];
		const auto bank = msg[pos];
		const auto ch = msg[pos + 1];
		const auto index = msg[pos + 2];
		auto param = findSynthParam(ch, bank, index);
		if (param == nullptr && ch != 0)
		{
            // ensure it's not global
			param = findSynthParam(0, bank, index);
			if (param == nullptr)
			{
                jassertfalse;
                return;
            }
			auto flags = param->getDescription().classFlags;
			if (!(flags & Parameter::Class::GLOBAL) && !(flags & Parameter::Class::NON_PART_SENSITIVE))
            {
                jassertfalse;
                return;
            }
        }
		param->setValueFromSynth(value, true);
		// TODO:
        /**
         If a
        global  parameter  or  a  Multi  parameter  is  ac-
        cessed,  which  is  not  part-sensitive  (e.g.  Input
        Boost  or  Multi  Delay  Time),  the  part  number  is
        ignored
         */
    }

    juce::StringArray Controller::getSinglePresetNames(int bank) const
    {
        if (bank > 1 || bank < 0)
        {
            jassertfalse;
            return {};
        }

        juce::StringArray bankNames;
        for (auto i = 0; i < 128; i++)
            bankNames.add(parseAsciiText(m_singles[0][i].data, 128 + 112));
        return bankNames;
    }
    juce::StringArray Controller::getMultiPresetsName() const
    {
        juce::StringArray bankNames;
        for (auto i = 0; i < 128; i++)
            bankNames.add(parseAsciiText(m_multis[i].data, 0));
        return bankNames;
    }

	juce::String Controller::getCurrentPartPresetName(uint8_t part)
	{
		// expensive but fresh!
		constexpr uint8_t asciiStart = 112;
		char text[kNameLength + 1];
		text[kNameLength] = 0; // termination
		for (auto pos = 0; pos < kNameLength; ++pos)
			text[pos] = static_cast<int>(getParam(part, 1, asciiStart + pos)->getValue());
		return juce::String(text);
	}

	void Controller::setCurrentPartPreset(uint8_t part, uint8_t bank, uint8_t prg)
	{
		if (bank < 0 || bank > 1 || prg < 0 || prg > 127)
		{
			jassertfalse;
			return;
		}

		uint8_t pt = isMultiMode() ? part : 0x40;
		auto &preset = m_singles[bank][prg];
		SysEx patch = {MessageType::DUMP_SINGLE, 0x0, pt};
		for (auto i = 0; i < kDataSizeInBytes; ++i)
			patch.push_back(preset.data[i]);
		sendSysEx(constructMessage(patch));
		sendSysEx(constructMessage({MessageType::REQUEST_ARRANGEMENT}));
	}

	void Controller::parseSingle(const SysEx &msg)
	{
        constexpr auto pageSize = 128;
        constexpr auto expectedDataSize = pageSize * 2 + 1 + 1; // we have 2 pages
        constexpr auto checkSumSize = 1;
        const auto dataSize = msg.size() - (kHeaderWithMsgCodeLen + 1); // 1 end byte, 1 bank, 1 prg
        const auto hasChecksum = dataSize == expectedDataSize + checkSumSize;
        assert(hasChecksum || dataSize == expectedDataSize);

        SinglePatch patch;
        patch.bankNumber = msg[kHeaderWithMsgCodeLen];
        patch.progNumber = msg[kHeaderWithMsgCodeLen + 1];
        [[maybe_unused]] const auto dataSum = copyData(msg, kHeaderWithMsgCodeLen + 2, patch.data);

        if (hasChecksum)
        {
            const auto checksum = msg[msg.size() - 2];
            const auto deviceId = msg[5];
            [[maybe_unused]] const auto expectedSum =
                (deviceId + 0x10 + patch.bankNumber + patch.progNumber + dataSum) & 0x7f;
            assert(expectedSum == checksum);
        }
		if (patch.bankNumber == 0)
		{
			// virus sends also the single buffer not matter what's the mode.
			// instead of keeping both, we 'encapsulate' this into first channel.
			// the logic to maintain this is done by listening the global single/multi param.
			if (isMultiMode() && patch.progNumber == 0x40)
				return;
			else if (!isMultiMode() && patch.progNumber == 0x0)
				return;

			constexpr auto bankSize = kDataSizeInBytes / 2;
			const auto ch = patch.progNumber == 0x40 ? 0 : patch.progNumber;
			for (auto i = 0; i < kDataSizeInBytes; i++)
			{
				if (auto *p = findSynthParam(ch, i > bankSize ? 0x71 : 0x70, i % bankSize))
					p->setValueFromSynth(patch.data[i], true);
			}
		}
		else
			m_singles[patch.bankNumber - 1][patch.progNumber] = patch;

        const auto namePos = kHeaderWithMsgCodeLen + 2 + 128 + 112;
        assert(namePos < msg.size());
        auto progName = parseAsciiText(msg, namePos);
        DBG(progName);
    }

    void Controller::parseMulti(const SysEx &msg)
    {
        constexpr auto expectedDataSize = 2 + 256;
        constexpr auto checkSumSize = 1;
        const auto dataSize = msg.size() - kHeaderWithMsgCodeLen - 1;
        const auto hasChecksum = dataSize == expectedDataSize + checkSumSize;
        assert(hasChecksum || dataSize == expectedDataSize);

        constexpr auto startPos = kHeaderWithMsgCodeLen;

        MultiPatch patch;
        patch.bankNumber = msg[startPos];
        patch.progNumber = msg[startPos + 1];
        auto progName = parseAsciiText(msg, startPos + 2 + 3);
        [[maybe_unused]] auto dataSum = copyData(msg, startPos + 2, patch.data);
        if (hasChecksum)
        {
            const int expectedChecksum = msg[msg.size() - 2];
            const auto msgDeviceId = msg[5];
            const int checksum = (msgDeviceId + 0x11 + patch.bankNumber + patch.progNumber + dataSum) & 0x7f;
            assert(checksum == expectedChecksum);
        }
        m_multis[patch.progNumber] = patch;
    }

	void Controller::parseControllerDump(synthLib::SMidiEvent &m)
	{
		uint8_t bank = 0xFF;
		uint8_t part = 0x40;
		if (m.a & synthLib::M_CONTROLCHANGE)
		{
			part = m.a ^ synthLib::M_CONTROLCHANGE;
			bank = 0x0;
		}
		else if (m.a & synthLib::M_POLYPRESSURE)
		{
			part = m.a ^ synthLib::M_POLYPRESSURE;
			bank = 0x1;
		}
		else
		{
			jassertfalse;
			return;
		}
		jassert(bank != 0xFF);
		DBG(juce::String::formatted("Set part: %d bank: %s param: %d  value: %d", part, bank == 0 ? "A" : "B", m.b,
									m.c));
		findSynthParam(part, bank, m.b)->setValueFromSynth(m.c, true);
	}


	juce::String numToBank(float bankIdx, Parameter::Description)
	{
		jassert(bankIdx >= 0);
		juce::String prefix = "RAM";
		if (bankIdx > 3)
		{
			prefix = bankIdx < 9 ? "ROM " : "ROM";
		}
		return prefix + juce::String(juce::roundToInt(bankIdx + 1));
	}

	juce::String numToPan(float panIdx, Parameter::Description)
	{
		// handles rounding due to continuous...
		const auto idx = juce::roundToInt(panIdx);
		if (idx == 64)
			return "Center";
		if (idx == 0)
			return "Left";
		if (idx == 127)
			return "Right";

		const auto sign = idx > 64 ? "+" : "";
		return sign + juce::String(juce::roundToInt(idx - 64));
	}

	const std::initializer_list<Parameter::Description> Controller::m_paramsDescription =
{
    {Parameter::Page::A, Parameter::Class::PERFORMANCE_CONTROLLER, 0, "Bank Select", {0, 3 + 26}, numToBank, {}, false, true, false}, // The Virus TI contains 4 banks of RAM, followed by 26 banks of ROM
    {Parameter::Page::A, Parameter::Class::PERFORMANCE_CONTROLLER, 1, "Modulation Wheel", {0,127}, {},{}, false, false, false},
    {Parameter::Page::A, Parameter::Class::PERFORMANCE_CONTROLLER, 2, "Breath Controller", {0,127}, {},{}, false, false, false},
    {Parameter::Page::A, Parameter::Class::PERFORMANCE_CONTROLLER, 3, "Contr 3", {0,127}, {},{}, false, false, false},
    {Parameter::Page::A, Parameter::Class::PERFORMANCE_CONTROLLER, 4, "Foot Controller", {0,127}, {},{}, false, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 5, "Portamento Time", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::PERFORMANCE_CONTROLLER, 6, "Data Slider", {0,127}, {},{}, false, false, false},
    {Parameter::Page::A, Parameter::Class::PERFORMANCE_CONTROLLER, 7, "Channel Volume", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::PERFORMANCE_CONTROLLER, 8, "Balance", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::PERFORMANCE_CONTROLLER, 9, "Contr 9", {0,127}, {},{}, false, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 10, "Panorama", {0,127}, numToPan,{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::PERFORMANCE_CONTROLLER, 11, "Expression", {0,127}, {},{}, false, false, false},
    {Parameter::Page::A, Parameter::Class::PERFORMANCE_CONTROLLER, 12, "Contr 12", {0,127}, {},{}, false, false, false},
    {Parameter::Page::A, Parameter::Class::PERFORMANCE_CONTROLLER, 13, "Contr 13", {0,127}, {},{}, false, false, false},
    {Parameter::Page::A, Parameter::Class::PERFORMANCE_CONTROLLER, 14, "Contr 14", {0,127}, {},{}, false, false, false},
    {Parameter::Page::A, Parameter::Class::PERFORMANCE_CONTROLLER, 15, "Contr 15", {0,127}, {},{}, false, false, false},
    {Parameter::Page::A, Parameter::Class::PERFORMANCE_CONTROLLER, 16, "Contr 16", {0,127}, {},{}, false, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 17, "Osc1 Shape", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 18, "Osc1 Pulsewidth", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 19, "Osc1 Wave Select", {0,64}, {},{}, true, true, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 20, "Osc1 Semitone", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 21, "Osc1 Keyfollow", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 22, "Osc2 Shape", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 23, "Osc2 Pulsewidth", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 24, "Osc2 Wave Select", {0,64}, {},{}, true, true, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 25, "Osc2 Semitone", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 26, "Osc2 Detune", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 27, "Osc2 FM Amount", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 28, "Osc2 Sync", {0,1}, {},{}, true, false, true},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 29, "Osc2 Filt Env Amt", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 30, "FM Filt Env Amt", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 31, "Osc2 Keyfollow", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::PERFORMANCE_CONTROLLER, 32, "Bank Select", {0,3}, {},{}, true, true, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 33, "Osc Balance", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 34, "Suboscillator Volume", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 35, "Suboscillator Shape", {0,1}, {},{}, true, false, true},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 36, "Osc Mainvolume", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 37, "Noise Volume", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 38, "Ringmodulator Volume", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A|Parameter::Class::VIRUS_C, 39, "Noise Color", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 40, "Cutoff", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 41, "Cutoff2", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 42, "Filter1 Resonance", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 43, "Filter2 Resonance", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 44, "Filter1 Env Amt", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 45, "Filter2 Env Amt", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 46, "Filter1 Keyfollow", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 47, "Filter2 Keyfollow", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 48, "Filter Balance", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 49, "Saturation Curve", {0,6}, {},{}, true, true, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 51, "Filter1 Mode", {0,3}, {},{}, true, true, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 52, "Filter2 Mode", {0,3}, {},{}, true, true, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 53, "Filter Routing", {0,3}, {},{}, true, true, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 54, "Filter Env Attack", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 55, "Filter Env Decay", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 56, "Filter Env Sustain", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 57, "Filter Env Sustain Time", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 58, "Filter Env Release", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 59, "Amp Env Attack", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 60, "Amp Env Decay", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 61, "Amp Env Sustain", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 62, "Amp Env Sustain Time", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 63, "Amp Env Release", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::PERFORMANCE_CONTROLLER, 64, "Hold Pedal", {0,127}, {},{}, false, false, false},
    {Parameter::Page::A, Parameter::Class::PERFORMANCE_CONTROLLER, 65, "Portamento Pedal", {0,127}, {},{}, false, false, false},
    {Parameter::Page::A, Parameter::Class::PERFORMANCE_CONTROLLER, 66, "Sostenuto Pedal", {0,127}, {},{}, false, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 67, "Lfo1 Rate", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 68, "Lfo1 Shape", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 69, "Lefo1 Env Mode", {0,1}, {},{}, true, false, true},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 70, "Lfo1 Mode", {0,1}, {},{}, true, false, true},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 71, "Lfo1 Symmetry", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 72, "Lfo1 Keyfollow", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 73, "Lfo1 Keytrigger", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 74, "Osc1 Lfo1 Amount", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 75, "Osc2 Lfo1 Amount", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 76, "PW Lfo1 Amount", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 77, "Reso Lfo1 Amount", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 78, "FiltGain Lfo1 Amount", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 79, "Lfo2 Rate", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 80, "Lfo2 Shape", {0,5}, {},{}, true, true, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 81, "Lfo2 Env Mode", {0,1}, {},{}, true, false, true},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 82, "Lfo2 Mode", {0,1}, {},{}, true, false, true},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 83, "Lfo2 Symmetry", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 84, "Lfo2 Keyfollow", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 85, "Lfo2 Keytrigger", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 86, "OscShape Lfo2 Amount", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 87, "FmAmount Lfo2 Amount", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 88, "Cutoff1 Lfo2 Amount", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 89, "Cutoff2 Lfo2 Amount", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 90, "Panorama Lfo2 Amount", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 91, "Patch Volume", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 93, "Transpose", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 94, "Key Mode", {0,4}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 97, "Unison Mode", {0,15}, {},{}, true, true, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 98, "Unison Detune", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 99, "Unison Panorama Spread", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 100, "Unison Lfo Phase", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 101, "Input Mode", {0,2}, {},{}, true, true, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 102, "Input Select", {0,8}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 105, "Chorus Mix", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 106, "Chorus Rate", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 107, "Chorus Depth", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 108, "Chorus Delay", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 109, "Chorus Feedback", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 110, "Chorus Lfo Shape", {0,5}, {},{}, true, true, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 112, "Delay/Reverb Mode", {0,1}, {},{}, true, false, true},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A|Parameter::Class::MULTI_OR_SINGLE, 113, "Effect Send", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A|Parameter::Class::MULTI_OR_SINGLE|Parameter::Class::NON_PART_SENSITIVE, 114, "Delay Time", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A|Parameter::Class::MULTI_OR_SINGLE|Parameter::Class::NON_PART_SENSITIVE, 115, "Delay Feedback", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A|Parameter::Class::MULTI_OR_SINGLE|Parameter::Class::NON_PART_SENSITIVE, 116, "Delay Rate / Reverb Decay Time", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A|Parameter::Class::MULTI_OR_SINGLE|Parameter::Class::NON_PART_SENSITIVE, 117, "Delay Depth / Reverb Room Size", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A|Parameter::Class::MULTI_OR_SINGLE|Parameter::Class::NON_PART_SENSITIVE, 118, "Delay Lfo Shape", {0,5}, {},{}, true, true, false},
//    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A|Parameter::Class::MULTI_OR_SINGLE|Parameter::Class::NON_PART_SENSITIVE, 118, "Reverb Damping", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A|Parameter::Class::MULTI_OR_SINGLE|Parameter::Class::NON_PART_SENSITIVE, 119, "Delay Color", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A|Parameter::Class::MULTI_OR_SINGLE|Parameter::Class::NON_PART_SENSITIVE, 122, "Keyb Local", {0,1}, {},{}, false, false, true},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A|Parameter::Class::MULTI_OR_SINGLE|Parameter::Class::NON_PART_SENSITIVE, 123, "All Notes Off", {0,127}, {},{}, false, false, false},

    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 1, "Arp Mode", {0,6}, {},{}, true, true, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 2, "Arp Pattern Selct", {0,31}, {},{}, true, true, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 3, "Arp Octave Range", {0,3}, {},{}, true, true, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 4, "Arp Hold Enable", {0,1}, {},{}, true, false, true},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 5, "Arp Note Length", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 6, "Arp Swing", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 7, "Lfo3 Rate", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 8, "Lfo3 Shape", {0,5}, {},{}, true, true, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 9, "Lfo3 Mode", {0,1}, {},{}, true, false, true},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 10, "Lfo3 Keyfollow", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 11, "Lfo3 Destination", {0,5}, {},{}, true, true, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 12, "Osc Lfo3 Amount", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 13, "Lfo3 Fade-In Time", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 16, "Clock Tempo", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 17, "Arp Clock", {0,17}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 18, "Lfo1 Clock", {0,19}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 19, "Lfo2 Clock", {0,19}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::MULTI_OR_SINGLE|Parameter::Class::NON_PART_SENSITIVE, 20, "Delay Clock", {0,16}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 21, "Lfo3 Clock", {0,19}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 25, "Control Smooth Mode", {0,3}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 26, "Bender Range Up", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 27, "Bender Range Down", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 28, "Bender Scale", {0,1}, {},{}, true, false, true},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 30, "Filter1 Env Polarity", {0,1}, {},{}, true, false, true},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 31, "Filter1 Env Polarity", {0,1}, {},{}, true, false, true},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 32, "Filter2 Cutoff Link", {0,1}, {},{}, true, false, true},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 33, "Filter Keytrack Base", {0,127}, {},{}, true, false, true},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 34, "Osc FM Mode", {0,12}, {},{}, true, true, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 35, "Osc Init Phase", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 36, "Punch Intensity", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 38, "Input Follower Mode",
        {0,9}, {},{}, true, true, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 39, "Vocoder Mode", {0,12}, {},{}, true, true, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 41, "Osc3 Mode", {0,67}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 42, "Osc3 Volume", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 43, "Osc3 Semitone", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 44, "Osc3 Detune", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 45, "LowEQ Frequency", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 46, "HighEQ Frequency", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 47, "Osc1 Shape Velocity", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 48, "Osc1 Shape Velocity", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 49, "PulseWidth Velocity", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 50, "Fm Amount Velocity", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 51, "Soft Knob1 ShortName", {0,127}, {},{}, false, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 52, "Soft Knob2 ShortName", {0,127}, {},{}, false, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 54, "Filter1 EnvAmt Velocity", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 55, "Filter1 EnvAmt Velocity", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 56, "Resonance1 Velocity", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 57, "Resonance2 Velocity", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 58, "Second Output Balance", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 60, "Amp Velocity", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 61, "Panorama Velocity", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 62, "Soft Knob-1 Single", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 63, "Soft Knob-2 Single", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 64, "Assign1 Source", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 65, "Assign1 Destination", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 66, "Assign1 Amount", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 67, "Assign2 Source", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 68, "Assign2 Destination1", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 69, "Assign2 Amount1", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 70, "Assign2 Destination2", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 71, "Assign2 Amount2", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 72, "Assign3 Source", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 73, "Assign3 Destination1", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 74, "Assign3 Amount1", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 75, "Assign3 Destination2", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 76, "Assign3 Amount2", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 77, "Assign2 Destination3", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 78, "Assign2 Amount3", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 79, "LFO1 Assign Dest", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 80, "LFO1 Assign Amount", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 81, "LFO2 Assign Dest", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 82, "LFO2 Assign Amount", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 84, "Phaser Mode", {0,6}, {},{}, true, true, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 85, "Phaser Mix", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 86, "Phaser Rate", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 87, "Phaser Depth", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 88, "Phaser Frequency", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 89, "Phaser Feedback", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 90, "Phaser Spread", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 92, "MidEQ Gain", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 93, "MidEQ Frequency", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 94, "MidEQ Q-Factor", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 95, "LowEQ Gain", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 96, "HighEQ Gain", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 97, "Bass Intensity", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 98, "Bass Tune", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 99, "Input Ringmodulator", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 100, "Distortion Curve", {0,6}, {},{}, true, true, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 101, "Distortion Intensity", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 102, "Assign 4 Source", {0,27}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 103, "Assign 4 Destination", {0,122}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C,104, "Assign 4 Amount", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 105, "Assign 5 Source", {0,27}, {},{}, true, true, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 106, "Assign 5 Destination", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 107, "Assign 5 Amount", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 108, "Assign 6 Source", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 109, "Assign 6 Destination", {0,122}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 110, "Assign 6 Amount", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 122, "Filter Select", {0,2}, {},{}, true, true, false},

    {Parameter::Page::C, Parameter::Class::MULTI_PARAM|Parameter::Class::NON_PART_SENSITIVE, 22, "Delay Output Select", {0,14}, {},{}, true, true, false},
    {Parameter::Page::C, Parameter::Class::MULTI_PARAM|Parameter::Class::BANK_PROGRAM_CHANGE_PARAM_BANK_SELECT, 31, "Part Bank Select", {0,3}, {},{}, true, true, false},
    {Parameter::Page::C, Parameter::Class::MULTI_PARAM|Parameter::Class::BANK_PROGRAM_CHANGE_PARAM_BANK_SELECT, 32, "Part Bank Change", {0,3}, {},{}, true, true, false},
    {Parameter::Page::C, Parameter::Class::MULTI_PARAM|Parameter::Class::BANK_PROGRAM_CHANGE_PARAM_BANK_SELECT, 33, "Part Program Change", {0,127}, {},{}, true, false, false},
    {Parameter::Page::C, Parameter::Class::MULTI_PARAM, 34, "Part Midi Channel", {0,15}, {},{}, false, true, false},
    {Parameter::Page::C, Parameter::Class::MULTI_PARAM, 35, "Part Low Key", {0,127}, {},{}, false, true, false},
    {Parameter::Page::C, Parameter::Class::MULTI_PARAM, 36, "Part High Key", {0,127}, {},{}, false, true, false},
    {Parameter::Page::C, Parameter::Class::MULTI_PARAM, 37, "Part Transpose", {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::MULTI_PARAM, 38, "Part Detune", {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::MULTI_PARAM, 39, "Part Volume", {0,127}, {},{}, true, false, false},
    {Parameter::Page::C, Parameter::Class::MULTI_PARAM, 40, "Part Midi Volume Init", {0,127}, {},{}, false, true, false},
    {Parameter::Page::C, Parameter::Class::MULTI_PARAM, 41, "Part Output Select", {0,14}, {},{}, false, true, false},
    {Parameter::Page::C, Parameter::Class::GLOBAL, 45, "Second Output Select", {0,15}, {},{}, false, true, false},
    {Parameter::Page::C, Parameter::Class::GLOBAL, 63, "Keyb Transpose Buttons", {0,1}, {},{}, false, false, true},
    {Parameter::Page::C, Parameter::Class::GLOBAL, 64, "Keyb Local", {0,1}, {},{}, false, false, true},
    {Parameter::Page::C, Parameter::Class::GLOBAL, 65, "Keyb Mode", {0,1}, {},{}, false, false, true},
    {Parameter::Page::C, Parameter::Class::GLOBAL, 66, "Keyb Transpose", {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::GLOBAL, 67, "Keyb ModWheel Contr", {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::GLOBAL, 68, "Keyb Pedal 1 Contr", {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::GLOBAL, 69, "Keyb Pedal 2 Contr", {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::GLOBAL, 70, "Keyb Pressure Sens", {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::MULTI_PARAM, 72, "Part Enable", {0,1}, {},{}, false, false, true},
    {Parameter::Page::C, Parameter::Class::MULTI_PARAM, 73, "Part Midi Volume Enable", {0,1}, {},{}, false, false, true},
    {Parameter::Page::C, Parameter::Class::MULTI_PARAM, 74, "Part Hold Pedal Enable", {0,1}, {},{}, false, false, true},
    {Parameter::Page::C, Parameter::Class::MULTI_PARAM, 75, "Keyb To Midi", {0,1}, {},{}, false, false, true},
    {Parameter::Page::C, Parameter::Class::MULTI_PARAM, 77, "Note Steal Priority", {0,1}, {},{}, false, false, true},
    {Parameter::Page::C, Parameter::Class::MULTI_PARAM, 78, "Part Prog Change Enable", {0,1}, {},{}, false, false, true},
    {Parameter::Page::C, Parameter::Class::GLOBAL, 85, "Glob Prog Change Enable", {0,1}, {},{}, false, false, true},
    {Parameter::Page::C, Parameter::Class::GLOBAL, 86, "MultiProg Change Enable", {0,1}, {},{}, false, false, true},
    {Parameter::Page::C, Parameter::Class::GLOBAL, 87, "Glob Midi Volume Enable", {0,1}, {},{}, false, false, true},
    {Parameter::Page::C, Parameter::Class::GLOBAL, 90, "Input Thru Level", {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::GLOBAL, 91, "Input Boost", {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::GLOBAL, 92, "Master Tune", {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::GLOBAL, 93, "Device ID", {0,16}, {},{}, false, true, false},
    {Parameter::Page::C, Parameter::Class::GLOBAL, 94, "Midi Control Low Page", {0,1}, {},{}, false, false, true},
    {Parameter::Page::C, Parameter::Class::GLOBAL, 95, "Midi Control High Page", {0,1}, {},{}, false, false, true},
    {Parameter::Page::C, Parameter::Class::GLOBAL, 96, "Midi Arpeggiator Send", {0,1}, {},{}, false, false, true},
    {Parameter::Page::C, Parameter::Class::GLOBAL, 97, "Knob Display", {0,3}, {},{}, false, true, false},
    {Parameter::Page::C, Parameter::Class::GLOBAL, 98, "Midi Dump Tx", {0,4}, {},{}, false, true, false},
    {Parameter::Page::C, Parameter::Class::GLOBAL, 99, "Midi Dump Rx", {0,4}, {},{}, false, true, false},
    {Parameter::Page::C, Parameter::Class::GLOBAL, 105, "Multi Program Change", {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::GLOBAL, 106, "Midi Clock Rx", {0,3}, {},{}, false, true, false},
    {Parameter::Page::C, Parameter::Class::GLOBAL, 110, "Soft Knob-1 Mode", {0,2}, {},{}, false, true, false},
    {Parameter::Page::C, Parameter::Class::GLOBAL, 111, "Soft Knob-2 Mode", {0,2}, {},{}, false, true, false},
    {Parameter::Page::C, Parameter::Class::GLOBAL, 112, "Soft Knob-1 Global", {0,2}, {},{}, false, true, false},
    {Parameter::Page::C, Parameter::Class::GLOBAL, 113, "Soft Knob-2 Global", {0,2}, {},{}, false, true, false},
    {Parameter::Page::C, Parameter::Class::GLOBAL, 114, "Soft Knob-1 Midi", {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::GLOBAL, 115, "Soft Knob-2 Midi", {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::GLOBAL, 116, "Expert Mode", {0,2}, {},{}, false, true, false},
    {Parameter::Page::C, Parameter::Class::GLOBAL, 117, "Knob Mode", {0,2}, {},{}, false, true, false},
    {Parameter::Page::C, Parameter::Class::GLOBAL, 118, "Memory Protect", {0,1}, {},{}, false, false, true},
    {Parameter::Page::C, Parameter::Class::GLOBAL, 120, "Soft Thru", {0,1}, {},{}, false, true, false},
    {Parameter::Page::C, Parameter::Class::GLOBAL, 121, "Panel Destination", {0,2}, {},{}, false, true, false},
    {Parameter::Page::C, Parameter::Class::GLOBAL, 122, "Play Mode", {0,2}, {},{}, false, true, false},
    {Parameter::Page::C, Parameter::Class::GLOBAL, 123, "Part Number", {0,127}, {},{}, false, true, false}, // 0..15;40
    {Parameter::Page::C, Parameter::Class::GLOBAL, 124, "Global Channel", {0,15}, {},{}, false, true, false},
    {Parameter::Page::C, Parameter::Class::GLOBAL, 125, "Led Mode", {0,2}, {},{}, false, true, false},
    {Parameter::Page::C, Parameter::Class::GLOBAL, 126, "LCD Contrast", {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::GLOBAL, 127, "Master Volume", {0,127}, {},{}, false, false, false},

    // UNDEFINED / UNUSED / STUBS
    {Parameter::Page::A, Parameter::Class::UNDEFINED, 92, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::A, Parameter::Class::UNDEFINED, 95, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::A, Parameter::Class::UNDEFINED, 96, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::A, Parameter::Class::UNDEFINED, 111, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::A, Parameter::Class::UNDEFINED, 120, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::A, Parameter::Class::UNDEFINED, 121, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::A, Parameter::Class::UNDEFINED, 124, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::A, Parameter::Class::UNDEFINED, 125, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::A, Parameter::Class::UNDEFINED, 126, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::A, Parameter::Class::UNDEFINED, 127, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::B, Parameter::Class::UNDEFINED, 0, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::B, Parameter::Class::UNDEFINED, 14, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::B, Parameter::Class::UNDEFINED, 15, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::B, Parameter::Class::UNDEFINED, 22, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::B, Parameter::Class::UNDEFINED, 23, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::B, Parameter::Class::UNDEFINED, 24, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::B, Parameter::Class::UNDEFINED, 29, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::B, Parameter::Class::UNDEFINED, 37, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::B, Parameter::Class::UNDEFINED, 40, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::B, Parameter::Class::UNDEFINED, 53, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::B, Parameter::Class::UNDEFINED, 69, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::B, Parameter::Class::UNDEFINED, 83, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::B, Parameter::Class::UNDEFINED, 91, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::B, Parameter::Class::UNDEFINED, 111, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::B, Parameter::Class::UNDEFINED, 123, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::B, Parameter::Class::UNDEFINED, 124, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::UNDEFINED, 0, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::UNDEFINED, 1, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::UNDEFINED, 2, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::UNDEFINED, 3, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::UNDEFINED, 4, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::UNDEFINED, 15, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::UNDEFINED, 16, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::UNDEFINED, 17, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::UNDEFINED, 18, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::UNDEFINED, 19, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::UNDEFINED, 20, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::UNDEFINED, 21, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::UNDEFINED, 23, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::UNDEFINED, 24, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::UNDEFINED, 25, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::UNDEFINED, 26, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::UNDEFINED, 27, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::UNDEFINED, 28, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::UNDEFINED, 29, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::UNDEFINED, 30, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::UNDEFINED, 76, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::UNDEFINED, 79, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::UNDEFINED, 80, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::UNDEFINED, 81, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::UNDEFINED, 82, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::UNDEFINED, 83, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::UNDEFINED, 84, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::UNDEFINED, 88, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::UNDEFINED, 89, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::UNDEFINED, 100, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::UNDEFINED, 101, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::UNDEFINED, 102, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::UNDEFINED, 103, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::UNDEFINED, 104, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::UNDEFINED, 119, {}, {0,127}, {},{}, false, false, false},
    // Text Chars / Unused
    {Parameter::Page::B, Parameter::Class::UNDEFINED, 110, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::B, Parameter::Class::UNDEFINED, 111, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::B, Parameter::Class::UNDEFINED, 112, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::B, Parameter::Class::UNDEFINED, 113, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::B, Parameter::Class::UNDEFINED, 114, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::B, Parameter::Class::UNDEFINED, 115, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::B, Parameter::Class::UNDEFINED, 116, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::B, Parameter::Class::UNDEFINED, 117, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::B, Parameter::Class::UNDEFINED, 118, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::B, Parameter::Class::UNDEFINED, 119, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::B, Parameter::Class::UNDEFINED, 120, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::B, Parameter::Class::UNDEFINED, 121, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::UNDEFINED, 5, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::UNDEFINED, 6, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::UNDEFINED, 7, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::UNDEFINED, 8, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::UNDEFINED, 9, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::UNDEFINED, 10, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::UNDEFINED, 11, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::UNDEFINED, 12, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::UNDEFINED, 13, {}, {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::UNDEFINED, 14, {}, {0,127}, {},{}, false, false, false},
    // TODO: B51-52 shortname?!? B54-B55 (same name?!)
};

    uint8_t Controller::copyData(const SysEx &src, int startPos, uint8_t *dst)
    {
        uint8_t sum = 0;
        for (auto i = 0; i < kDataSizeInBytes; i++)
        {
            dst[i] = src[startPos + i];
            sum += dst[i];
        }
        return sum;
    }

    template <typename T> juce::String Controller::parseAsciiText(const T &msg, const int start) const
    {
        char text[kNameLength + 1];
        text[kNameLength] = 0; // termination
        for (auto pos = 0; pos < kNameLength; ++pos)
            text[pos] = msg[start + pos];
        return juce::String(text);
    }

    void Controller::printMessage(const SysEx &msg) const
    {
        for (auto &m : msg)
        {
            std::cout << std::hex << (int)m << ",";
        }
        std::cout << std::endl;
    }

    void Controller::sendSysEx(const SysEx &msg)
    {
        synthLib::SMidiEvent ev;
        ev.sysex = msg;
        m_processor.addMidiEvent(ev);
    }

    std::vector<uint8_t> Controller::constructMessage(SysEx msg)
    {
        const uint8_t start[] = {0xf0, 0x00, 0x20, 0x33, 0x01, static_cast<uint8_t>(m_deviceId)};
        msg.insert(msg.begin(), std::begin(start), std::end(start));
        msg.push_back(0xf7);
        return msg;
    }

    void Controller::timerCallback()
    {
        const juce::ScopedLock sl(m_eventQueueLock);
        for (auto msg : m_virusOut)
        {
            if (msg.sysex.size() == 0)
            {
                // no sysex
				parseControllerDump(msg);
			}
            else
                parseMessage(msg.sysex);
        }
        m_virusOut.clear();
    }

    void Controller::dispatchVirusOut(const std::vector<synthLib::SMidiEvent> &newData)
    {
        const juce::ScopedTryLock sl(m_eventQueueLock);
        if (!sl.isLocked())
            return;

        m_virusOut.insert(m_virusOut.end(), newData.begin(), newData.end());
    }
}; // namespace Virus

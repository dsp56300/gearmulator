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

    	for(uint8_t i=3; i<=8; ++i)
			sendSysEx(constructMessage({MessageType::REQUEST_BANK_SINGLE, i}));

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
        if (bank >= m_singles.size() || bank < 0)
        {
            jassertfalse;
            return {};
        }

        juce::StringArray bankNames;
        for (auto i = 0; i < 128; i++)
            bankNames.add(parseAsciiText(m_singles[bank][i].data, 128 + 112));
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
		if (bank < 0 || bank >= m_singles.size() || prg < 0 || prg > 127)
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

	juce::String numTo7bitSigned(const int idx)
	{
		const auto sign = idx > 64 ? "+" : "";
		return sign + juce::String(juce::roundToInt(idx - 64));
	}

	juce::String paramTo7bitSigned(const float idx, Parameter::Description)
	{
		return numTo7bitSigned(juce::roundToInt(idx));
	}

	float textTo7bitSigned(juce::String text, Parameter::Description)
	{
		const auto val = std::clamp(text.getIntValue(), -64, 63);
		return val + 64;
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
		return numTo7bitSigned(idx);
	}

	juce::String numToOscShape(float panIdx, Parameter::Description)
	{
		const auto idx = juce::roundToInt(panIdx);
		if (idx == 64)
			return "Saw";
		if (idx == 0)
			return "Wave";
		if (idx == 127)
			return "Pulse";
		return numTo7bitSigned(idx);
	}

	juce::String numToOscWaveSelect(float panIdx, Parameter::Description)
	{
		const auto idx = juce::roundToInt(panIdx);
		if (idx == 0)
			return "Sine";
		if (idx == 1)
			return "Triangle";
		return "Wave " + juce::String(idx);
	}

	juce::String numToKeyfollow(float panIdx, Parameter::Description)
	{
		const auto idx = juce::roundToInt(panIdx);
		if (idx == 32)
			return "Default";
		return juce::String(idx);
	}

	juce::String numToSatCurv(float idx, Parameter::Description)
	{
		const auto ridx = juce::roundToInt(idx);
		switch (ridx)
		{
		case 0:  return "Off";
		case 1:  return "Light";
		case 2:  return "Soft";
		case 3:  return "Middle";
		case 4:  return "Hard";
		case 5:  return "Digital";
		case 6:  return "Shaper";
		case 7:  return "Rectifier";
		case 8:  return "BitReducer";
		case 9:  return "RateReducer";
		case 10:  return "Rate+Flw";
		case 11:  return "LowPass";
		case 12:  return "Low+Flw";
		case 13:  return "HighPass";
		case 14:  return "High+Flw";

		default: return juce::String(idx);
		}
	}

	juce::String numToFilterMode(float idx, Parameter::Description)
	{
		const auto ridx = juce::roundToInt(idx);
		switch (ridx)
		{
		case 0:  return "LowPass";
		case 1:  return "HighPass";
		case 2:  return "BandPass";
		case 3:  return "BandStop";
		case 4:  return "Analog 1P";
		case 5:  return "Analog 2P";
		case 6:  return "Analog 3P";
		case 7:  return "Analog 4P";

		default: return juce::String(idx);
		}
	}

	juce::String numToFilterRouting(float idx, Parameter::Description)
	{
		const auto ridx = juce::roundToInt(idx);
		switch (ridx)
		{
		case 0:  return "Serial 4";
		case 1:  return "Serial 6";
		case 2:  return "Parallel 4";
		case 3:  return "Split";
		default: return juce::String(idx);
		}
	}

	juce::String numToEnvSusTime(float idx, Parameter::Description)
	{
		// handles rounding due to continuous...
		const auto ridx = juce::roundToInt(idx);
		if (ridx == 64)
			return "Fall";
		if (ridx == 0)
			return "Infinite";
		if (ridx == 127)
			return "Rise";
		return numTo7bitSigned(idx);
	}

	juce::String numToLfoShape(float idx, Parameter::Description)
	{
		const auto ridx = juce::roundToInt(idx);
		switch (ridx)
		{
		case 0:  return "Sine";
		case 1:  return "Triangle";
		case 2:  return "Saw";
		case 3:  return "Square";
		case 4:  return "S&H";
		case 5:  return "S&G";
		case 6:  return "Wave3";
		case 7:  return "Wave4";
		case 8:  return "Wave5";
		case 9:  return "Wave6";
		case 10:  return "Wave7";
		case 11:  return "Wave8";
		case 12:  return "Wave9";
		case 13:  return "Wave10";
		case 14:  return "Wave11";
		case 15:  return "Wave12";
		case 16:  return "Wave13";
		case 17:  return "Wave14";
		case 18:  return "Wave15";
		case 19:  return "Wave16";
		case 20:  return "Wave17";
		case 21:  return "Wave18";
		case 22:  return "Wave19";
		case 23:  return "Wave20";
		case 24:  return "Wave21";
		case 25:  return "Wave22";
		case 26:  return "Wave23";
		case 27:  return "Wave24";
		case 28:  return "Wave25";
		case 29:  return "Wave26";
		case 30:  return "Wave27";
		case 31:  return "Wave28";
		case 32:  return "Wave29";
		case 33:  return "Wave30";
		case 34:  return "Wave31";
		case 35:  return "Wave32";
		case 36:  return "Wave33";
		case 37:  return "Wave34";
		case 38:  return "Wave35";
		case 39:  return "Wave36";
		case 40:  return "Wave37";
		case 41:  return "Wave38";
		case 42:  return "Wave39";
		case 43:  return "Wave40";
		case 44:  return "Wave41";
		case 45:  return "Wave42";
		case 46:  return "Wave43";
		case 47:  return "Wave44";
		case 48:  return "Wave45";
		case 49:  return "Wave46";
		case 50:  return "Wave47";
		case 51:  return "Wave48";
		case 52:  return "Wave49";
		case 53:  return "Wave50";
		case 54:  return "Wave51";
		case 55:  return "Wave52";
		case 56:  return "Wave53";
		case 57:  return "Wave54";
		case 58:  return "Wave55";
		case 59:  return "Wave56";
		case 60:  return "Wave57";
		case 61:  return "Wave58";
		case 62:  return "Wave59";
		case 63:  return "Wave60";
		case 64:  return "Wave61";
		case 65:  return "Wave62";
		case 66:  return "Wave63";
		case 67:  return "Wave64";
		default: return juce::String(idx);
		}
	}

	juce::String numToLfoMode(float v, Parameter::Description) { return juce::roundToInt(v) == 0 ? "Poly" : "Mono"; }

	juce::String numToKeyMode(float idx, Parameter::Description)
	{
		const auto ridx = juce::roundToInt(idx);
		switch (ridx)
		{
		case 0:  return "Poly";
		case 1:  return "Mono 1";
		case 2:  return "Mono 2";
		case 3:  return "Mono 3";
		case 4:  return "Mono 4";
		case 5:  return "Hold";
		default: return juce::String(idx);
		}
	}

	juce::String numToInputMode(float idx, Parameter::Description)
	{
		const auto ridx = juce::roundToInt(idx);
		switch (ridx)
		{
		case 0:  return "Off";
		case 1:  return "Dynamic";
		case 2:  return "Static";
		case 3:  return "To Effects";
		default: return juce::String(idx);
		}
	}

	juce::String numToDelayReverbMode(float idx, Parameter::Description)
	{
		const auto ridx = juce::roundToInt(idx);
		switch (ridx)
		{
		case 0:  return "Off";
		case 1:  return "Delay";
		case 2:  return "Reverb";
		case 3:  return "Reverb + Feedback";
		default: return juce::String(idx);
		}
	}

	juce::String numToArpMode(float idx, Parameter::Description)
	{
		const auto ridx = juce::roundToInt(idx);
		switch (ridx)
		{
		case 0:  return "Off";
		case 1:  return "Up";
		case 2:  return "Down";
		case 3:  return "Up&Down";
		case 4:  return "As Played";
		case 5:  return "Random";
		case 6:  return "Chord";
		default: return juce::String(idx);
		}
	}

	juce::String numToLfoDest(float idx, Parameter::Description)
	{
		const auto ridx = juce::roundToInt(idx);
		switch (ridx)
		{
		case 0:  return "Osc1";
		case 1:  return "Osc1+2";
		case 2:  return "Osc2";
		case 3:  return "PW1";
		case 4:  return "PW1+2";
		case 5:  return "PW2";
		default: return juce::String(idx);
		}
	}

	juce::String numToArpSwing(float idx, Parameter::Description)
	{
		return juce::String(juce::roundToInt(50 + (75 - 50) * (idx / 127))) + "%";
	}

	juce::String numToClockTempo(float idx, Parameter::Description)
	{
		return juce::String(juce::roundToInt(63 + (190 - 63) * (idx / 127))) + "BPM";
	}

	juce::String numToControlSmoothMode(float idx, Parameter::Description)
	{
		const auto ridx = juce::roundToInt(idx);
		switch (ridx)
		{
		case 0:  return "Off";
		case 1:  return "On";
		case 2:  return "Auto";
		case 3:  return "Note";
		default: return juce::String(idx);
		}
	}

	juce::String numToNegPos(float v, Parameter::Description)
	{
		return juce::roundToInt(v) == 0 ? "Negative" : "Positive";
	}

	juce::String numToLinExp(float v, Parameter::Description)
	{
		return juce::roundToInt(v) == 0 ? "Linear" : "Exponential";
	}

	juce::String numToModMatrixSource(float v, Parameter::Description)
	{
		const auto ridx = juce::roundToInt(v);
		switch (ridx)
		{
		case 0:  return "Off";
		case 1:  return "PitchBnd";
		case 2:  return "ChanPres";
		case 3:  return "ModWheel";
		case 4:  return "Breath";
		case 5:  return "Contr3";
		case 6:  return "Foot";
		case 7:  return "Data";
		case 8:  return "Balance";
		case 9:  return "Contr 9";
		case 10: return "Express";
		case 11: return "Contr 12";
		case 12: return "Contr 13";
		case 13: return "Contr 14";
		case 14: return "Contr 15";
		case 15: return "Contr 16";
		case 16: return "HoldPed";
		case 17: return "PortaSw";
		case 18: return "SostPed";
		case 19: return "AmpEnv";
		case 20: return "FiltEnv";
		case 21: return "Lfo 1";
		case 22: return "Lfo 2";
		case 23: return "Lfo 3";
		case 24: return "VeloOn";
		case 25: return "VeloOff";
		case 26: return "KeyFlw";
		case 27: return "Random";
		default: return juce::String(v);
		}
	}

	juce::String numToModMatrixDest(float v, Parameter::Description)
	{
		const auto ridx = juce::roundToInt(v);
		switch (ridx)
		{
		case 0:   return "Off";
		case 1:   return "PatchVol";
		case 2:   return "ChannelVol";
		case 3:   return "Panorama";
		case 4:   return "Transpose";
		case 5:   return "Portamento";
		case 6:   return "Osc1Shape";
		case 7:   return "Osc1PlsWdh";
		case 8:   return "Osc1WavSel";
		case 9:   return "Osc1Pitch";
		case 10:  return "Osc1Keyflw";
		case 11:  return "Osc2Shape";
		case 12:  return "Osc2PlsWdh";
		case 13:  return "Osc2WavSel";
		case 14:  return "Osc2Pitch";
		case 15:  return "Osc2Detune";
		case 16:  return "Osc2FmAmt";
		case 17:  return "Osc2EnvAmt";
		case 18:  return "FmEnvAmt";
		case 19:  return "Osc2Keyflw";
		case 20:  return "OscBalance";
		case 21:  return "SubOscVol";
		case 22:  return "OscMainVol";
		case 23:  return "NoiseVol";
		case 24:  return "Cutoff";
		case 25:  return "Cutoff2";
		case 26:  return "Filt1Reso";
		case 27:  return "Filt2Reso";
		case 28:  return "Flt1EnvAmt";
		case 29:  return "Flt2EnvAmt";
		case 30:  return "Flt1Keyflw";
		case 31:  return "Flt2Keyflw";
		case 32:  return "FltBalance";
		case 33:  return "FltAttack";
		case 34:  return "FltDecay";
		case 35:  return "FltSustain";
		case 36:  return "FltSusTime";
		case 37:  return "FltRelease";
		case 38:  return "AmpAttack";
		case 39:  return "AmpDecay";
		case 40:  return "AmpSustain";
		case 41:  return "AmpSusTime";
		case 42:  return "AmpRelease";
		case 43:  return "Lfo1Rate";
		case 44:  return "Lfo1Cont";
		case 45:  return "Lfo1>Osc1";
		case 46:  return "Lfo1>Osc2";
		case 47:  return "Lfo1>PlsWd";
		case 48:  return "Lfo1>Reso";
		case 49:  return "Lfo1>FltGn";
		case 50:  return "Lfo2Rate";
		case 51:  return "Lfo2Cont";
		case 52:  return "Lfo2>Shape";
		case 53:  return "Lfo2>Fm";
		case 54:  return "Lfo2>Cut1";
		case 55:  return "Lfo2>Cut2";
		case 56:  return "Lfo2>Pan";
		case 57:  return "Lfo3Rate";
		case 58:  return "Lfo3OscAmt";
		case 59:  return "UniDetune";
		case 60:  return "UniSpread";
		case 61:  return "UniLfoPhs";
		case 62:  return "ChorusMix";
		case 63:  return "ChorusRate";
		case 64:  return "ChorusDpth";
		case 65:  return "ChorusDly";
		case 66:  return "ChorusFeed";
		case 67:  return "EffectSend";
		case 68:  return "DelayTime";
		case 69:  return "DelayFeed";
		case 70:  return "DelayRate";
		case 71:  return "DelayDepth";
		case 72:  return "Osc1ShpVel";
		case 73:  return "Osc2ShpVel";
		case 74:  return "PlsWhdVel";
		case 75:  return "FmAmtVel";
		case 76:  return "Flt1EnvVel";
		case 77:  return "Flt2EnvVel";
		case 78:  return "Reso1Vel";
		case 79:  return "Reso2Vel";
		case 80:  return "AmpVel";
		case 81:  return "PanVel";
		case 82:  return "Ass1Amt1";
		case 83:  return "Ass2Amt1";
		case 84:  return "Ass2Amt2";
		case 85:  return "Ass3Amt1";
		case 86:  return "Ass3Amt2";
		case 87:  return "Ass3Amt3";
		case 88:  return "OscInitPhs";
		case 89:  return "PunchInt";
		case 90:  return "RingMod";
		case 91:  return "NoiseColor";
		case 92:  return "DelayColor";
		case 93:  return "ABoostInt";
		case 94:  return "ABoostTune";
		case 95:  return "DistInt";
		case 96:  return "RingmodMix";
		case 97:  return "Osc3Volume";
		case 98:  return "Osc3Semi";
		case 99:  return "Osc3Detune";
		case 100: return "Lfo1AssAmt";
		case 101: return "Lfo2AssAmt";
		case 102: return "PhaserMix";
		case 103: return "PhaserRate";
		case 104: return "PhaserDept";
		case 105: return "PhaserFreq";
		case 106: return "PhaserFdbk";
		case 107: return "PhaserSprd";
		case 108: return "RevbDecay";
		case 109: return "RevDamping";
		case 110: return "RevbColor";
		case 111: return "RevPredely";
		case 112: return "RevFeedbck";
		case 113: return "SecBalance";
		case 114: return "ArpNoteLen";
		case 115: return "ArpSwing";
		case 116: return "ArpPattern";
		case 117: return "EqMidGain";
		case 118: return "EqMidFreq";
		case 119: return "EqMidQFactor";
		case 120: return "Assign4Amt";
		case 121: return "Assign5Amt";
		case 122: return "Assign6Amt";
		default:  return juce::String(v);
		}
	}

	juce::String numToSoftKnobDest(float v, Parameter::Description)
	{
		const auto ridx = juce::roundToInt(v);
		switch (ridx)
		{
		case 0:   return "Off";
		case 1:   return "ModWheel";
		case 2:   return "Breath";
		case 3:   return "Contr3";
		case 4:   return "Foot";
		case 5:   return "Data";
		case 6:   return "Balance";
		case 7:   return "Contr9";
		case 8:   return "Expression";
		case 9:   return "Contr12";
		case 10:  return "Contr13";
		case 11:  return "Contr14";
		case 12:  return "Contr15";
		case 13:  return "Contr16";
		case 14:  return "PatchVolume";
		case 15:  return "ChannelVolume";
		case 16:  return "Panorama";
		case 17:  return "Transpose";
		case 18:  return "Portamento";
		case 19:  return "UnisonDetune";
		case 20:  return "UnisonPanSprd";
		case 21:  return "UnisoLfoPhase";
		case 22:  return "ChorusMix";
		case 23:  return "ChorusRate";
		case 24:  return "ChorusDepth";
		case 25:  return "ChorusDelay";
		case 26:  return "ChorusFeedback";
		case 27:  return "EffectSend";
		case 28:  return "DelayTime(ms)";
		case 29:  return "DelayFeedback";
		case 30:  return "DelayRate";
		case 31:  return "DelayDepth";
		case 32:  return "Osc1WavSelect";
		case 33:  return "Osc1PulseWidth";
		case 34:  return "Osc1Semitone";
		case 35:  return "Osc1Keyfollow";
		case 36:  return "Osc2WavSelect";
		case 37:  return "Os2PulseWidth";
		case 38:  return "Osc2EnvAmount";
		case 39:  return "FmEnvAmount";
		case 40:  return "Osc2Keyfollow";
		case 41:  return "NoiseVolume";
		case 42:  return "Filt1Resonance";
		case 43:  return "Filt2Resonance";
		case 44:  return "Filt1EnvAmount";
		case 45:  return "Filt2EnvAmount";
		case 46:  return "Filt1Keyfollow";
		case 47:  return "Filt2Keyfollow";
		case 48:  return "Lfo1Symmetry";
		case 49:  return "Lfo1>Osc1";
		case 50:  return "Lfo1>Osc2";
		case 51:  return "Lfo1>PulsWidth";
		case 52:  return "Lfo1>Resonance";
		case 53:  return "Lfo1>FiltGain";
		case 54:  return "Lfo2Symmetry";
		case 55:  return "Lfo2>Shape";
		case 56:  return "Lfo2>FmAmount";
		case 57:  return "Lfo2>Cutoff1";
		case 58:  return "Lfo2>Cutoff2";
		case 59:  return "Lfo2>Panorama";
		case 60:  return "Lfo3Rate";
		case 61:  return "Lfo3OscAmount";
		case 62:  return "Osc1ShapeVel";
		case 63:  return "Osc2ShapeVel";
		case 64:  return "PulsWidthVel";
		case 65:  return "FmAmountVel";
		case 66:  return "Filt1EnvVel";
		case 67:  return "Filt2EnvVel";
		case 68:  return "Resonance1Vel";
		case 69:  return "Resonance2Vel";
		case 70:  return "AmplifierVel";
		case 71:  return "PanoramaVel";
		case 72:  return "Assign1Amt1";
		case 73:  return "Assign2Amt1";
		case 74:  return "Assign2Amt2";
		case 75:  return "Assign3Amt1";
		case 76:  return "Assign3Amt2";
		case 77:  return "Assign3Amt3";
		case 78:  return "ClockTempo";
		case 79:  return "InputThru";
		case 80:  return "OscInitPhase";
		case 81:  return "PunchIntensity";
		case 82:  return "Ringmodulator";
		case 83:  return "NoiseColor";
		case 84:  return "DelayColor";
		case 85:  return "AnalogBoostInt";
		case 86:  return "AnalogBstTune";
		case 87:  return "DistortionInt";
		case 88:  return "RingModMix";
		case 89:  return "Osc3Volume";
		case 90:  return "Osc3Semitone";
		case 91:  return "Osc3Detune";
		case 92:  return "Lfo1AssignAmt";
		case 93:  return "Lfo2AssignAmt";
		case 94:  return "PhaserMix";
		case 95:  return "PhaserRate";
		case 96:  return "PhaserDepth";
		case 97:  return "PhaserFrequency";
		case 98:  return "PhaserFeedback";
		case 99:  return "PhaserSpread";
		case 100: return "RevDecayTime";
		case 101: return "ReverbDamping";
		case 102: return "ReverbColor";
		case 103: return "ReverbFeedback";
		case 104: return "SecondBalance";
		case 105: return "ArpMode";
		case 106: return "ArpPattern";
		case 107: return "ArpClock";
		case 108: return "ArpNoteLength";
		case 109: return "ArpSwing";
		case 110: return "ArpOctaves";
		case 111: return "ArpHold";
		case 112: return "EqMidGain";
		case 113: return "EqMidFreq";
		case 114: return "EqMidQFactor";
		case 115: return "Assign4Amt";
		case 116: return "Assign5Amt";
		case 117: return "Assign6Amt";
		default:  return juce::String(v);
		}
	}

	juce::String numToKeytrigger(float v, Parameter::Description)
	{
		return juce::roundToInt(v) == 0 ? "Off" : "Keytrigger Phase " + juce::String(juce::roundToInt(v));
	}

	juce::String numToUnisonMode(float v, Parameter::Description)
	{
		return juce::roundToInt(v) == 0 ? "Off" : "Twin " + juce::String(juce::roundToInt(v));
	}

	juce::String numToOscFmMode(float idx, Parameter::Description)
	{
		const auto ridx = juce::roundToInt(idx);
		switch (ridx)
		{
		case 0:  return "Pos-Tri";
		case 1:  return "Tri";
		case 2:  return "Wave";
		case 3:  return "Noise";
		case 4:  return "In L";
		case 5:  return "In L+R";
		default: return juce::String(idx);
		}
	}

	juce::String numToMusicDivision(float idx, Parameter::Description)
	{
		const auto ridx = juce::roundToInt(idx);
		switch (ridx)
		{
		case 0:  return "Off";
		case 1:  return "1/64";
		case 2:  return "1/32";
		case 3:  return "1/16";
		case 4:  return "1/8";
		case 5:  return "1/4";
		case 6:  return "1/2";
		case 7:  return "3/64";
		case 8:  return "3/32";
		case 9:  return "3/16";
		case 10: return "3/8";
		case 11: return "1/24";
		case 12: return "1/12";
		case 13: return "1/6";
		case 14: return "1/3";
		case 15: return "2/3";
		case 16: return "3/4";
		case 17: return "1/1";
		case 18: return "2/1";
		case 19: return "4/1";
		case 20: return "8/1";
		case 21: return "16/1";
		default: return juce::String(idx);
		}
	}

	juce::String numToPhaserMode(float v, Parameter::Description)
	{
		return juce::roundToInt(v) == 0 ? "Off" : "Stage " + juce::String(juce::roundToInt(v));
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
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 17, "Osc1 Shape", {0,127}, numToOscShape,{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 18, "Osc1 Pulsewidth", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 19, "Osc1 Wave Select", {0,64}, numToOscWaveSelect, {}, true, true, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 20, "Osc1 Semitone", {0,127}, paramTo7bitSigned, textTo7bitSigned, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 21, "Osc1 Keyfollow", {0,127}, numToKeyfollow, {}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 22, "Osc2 Shape", {0,127}, numToOscShape, {}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 23, "Osc2 Pulsewidth", {0,127}, {}, {}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 24, "Osc2 Wave Select", {0,64}, numToOscWaveSelect,{}, true, true, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 25, "Osc2 Semitone", {0,127}, paramTo7bitSigned, textTo7bitSigned, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 26, "Osc2 Detune", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 27, "Osc2 FM Amount", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 28, "Osc2 Sync", {0,1}, {},{}, true, false, true},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 29, "Osc2 Filt Env Amt", {0,127}, paramTo7bitSigned, textTo7bitSigned, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 30, "FM Filt Env Amt", {0,127}, paramTo7bitSigned, textTo7bitSigned, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 31, "Osc2 Keyfollow", {0,127}, numToKeyfollow,{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::PERFORMANCE_CONTROLLER, 32, "Bank Select", {0, 3 + 26}, numToBank,{}, false, true, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 33, "Osc Balance", {0,127}, paramTo7bitSigned, textTo7bitSigned, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 34, "Suboscillator Volume", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 35, "Suboscillator Shape", {0,1}, [](float v, Parameter::Description){ return juce::roundToInt(v) == 0 ? "Square" : "Triangle"; },{}, true, true, true},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 36, "Osc Mainvolume", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 37, "Noise Volume", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 38, "Ringmodulator Volume", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A|Parameter::Class::VIRUS_C, 39, "Noise Color", {0,127}, paramTo7bitSigned, textTo7bitSigned, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 40, "Cutoff", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 41, "Cutoff2", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 42, "Filter1 Resonance", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 43, "Filter2 Resonance", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 44, "Filter1 Env Amt", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 45, "Filter2 Env Amt", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 46, "Filter1 Keyfollow", {0,127}, paramTo7bitSigned, textTo7bitSigned, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 47, "Filter2 Keyfollow", {0,127}, paramTo7bitSigned, textTo7bitSigned, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 48, "Filter Balance", {0,127}, paramTo7bitSigned, textTo7bitSigned, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 49, "Saturation Curve", {0,14}, numToSatCurv, {}, true, true, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 51, "Filter1 Mode", {0,7}, numToFilterMode, {}, true, true, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 52, "Filter2 Mode", {0,7}, numToFilterMode, {}, true, true, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 53, "Filter Routing", {0,3}, numToFilterRouting, {}, true, true, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 54, "Filter Env Attack", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 55, "Filter Env Decay", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 56, "Filter Env Sustain", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 57, "Filter Env Sustain Time", {0,127}, numToEnvSusTime, {}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 58, "Filter Env Release", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 59, "Amp Env Attack", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 60, "Amp Env Decay", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 61, "Amp Env Sustain", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 62, "Amp Env Sustain Time", {0,127}, numToEnvSusTime, {}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 63, "Amp Env Release", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::PERFORMANCE_CONTROLLER, 64, "Hold Pedal", {0,127}, {},{}, false, false, false},
    {Parameter::Page::A, Parameter::Class::PERFORMANCE_CONTROLLER, 65, "Portamento Pedal", {0,127}, {},{}, false, false, false},
    {Parameter::Page::A, Parameter::Class::PERFORMANCE_CONTROLLER, 66, "Sostenuto Pedal", {0,127}, {},{}, false, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 67, "Lfo1 Rate", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 68, "Lfo1 Shape", {0,67}, numToLfoShape, {}, true, true, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 69, "Lfo1 Env Mode", {0,1}, {},{}, true, false, true},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 70, "Lfo1 Mode", {0,1}, numToLfoMode, {}, true, false, true},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 71, "Lfo1 Symmetry", {0,127}, paramTo7bitSigned, textTo7bitSigned, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 72, "Lfo1 Keyfollow", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 73, "Lfo1 Keytrigger", {0,127}, numToKeytrigger, {}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 74, "Osc1 Lfo1 Amount", {0,127}, paramTo7bitSigned, textTo7bitSigned, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 75, "Osc2 Lfo1 Amount", {0,127}, paramTo7bitSigned, textTo7bitSigned, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 76, "PW Lfo1 Amount", {0,127}, paramTo7bitSigned, textTo7bitSigned, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 77, "Reso Lfo1 Amount", {0,127}, paramTo7bitSigned, textTo7bitSigned, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 78, "FiltGain Lfo1 Amount", {0,127}, paramTo7bitSigned, textTo7bitSigned, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 79, "Lfo2 Rate", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 80, "Lfo2 Shape", {0,67}, numToLfoShape,{}, true, true, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 81, "Lfo2 Env Mode", {0,1}, {},{}, true, false, true},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 82, "Lfo2 Mode", {0,1}, numToLfoMode, {}, true, false, true},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 83, "Lfo2 Symmetry", {0,127}, paramTo7bitSigned, textTo7bitSigned, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 84, "Lfo2 Keyfollow", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 85, "Lfo2 Keytrigger", {0,127}, numToKeytrigger,{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 86, "OscShape Lfo2 Amount", {0,127}, paramTo7bitSigned, textTo7bitSigned, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 87, "FmAmount Lfo2 Amount", {0,127}, paramTo7bitSigned, textTo7bitSigned, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 88, "Cutoff1 Lfo2 Amount", {0,127}, paramTo7bitSigned, textTo7bitSigned, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 89, "Cutoff2 Lfo2 Amount", {0,127}, paramTo7bitSigned, textTo7bitSigned, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 90, "Panorama Lfo2 Amount", {0,127}, paramTo7bitSigned, textTo7bitSigned, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 91, "Patch Volume", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 93, "Transpose", {0,127}, paramTo7bitSigned, textTo7bitSigned, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 94, "Key Mode", {0,5}, numToKeyMode, {}, true, true, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 97, "Unison Mode", {0,15}, numToUnisonMode, {}, true, true, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 98, "Unison Detune", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 99, "Unison Panorama Spread", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 100, "Unison Lfo Phase", {0,127}, paramTo7bitSigned, textTo7bitSigned, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 101, "Input Mode", {0,3}, numToInputMode, {}, true, true, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 102, "Input Select", {0,8}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 105, "Chorus Mix", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 106, "Chorus Rate", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 107, "Chorus Depth", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 108, "Chorus Delay", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 109, "Chorus Feedback", {0,127}, paramTo7bitSigned, textTo7bitSigned, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 110, "Chorus Lfo Shape", {0,67}, numToLfoShape, {}, true, true, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A, 112, "Delay/Reverb Mode", {0,3}, numToDelayReverbMode, {}, true, false, true},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A|Parameter::Class::MULTI_OR_SINGLE, 113, "Effect Send", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A|Parameter::Class::MULTI_OR_SINGLE|Parameter::Class::NON_PART_SENSITIVE, 114, "Delay Time", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A|Parameter::Class::MULTI_OR_SINGLE|Parameter::Class::NON_PART_SENSITIVE, 115, "Delay Feedback", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A|Parameter::Class::MULTI_OR_SINGLE|Parameter::Class::NON_PART_SENSITIVE, 116, "Delay Rate / Reverb Decay Time", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A|Parameter::Class::MULTI_OR_SINGLE|Parameter::Class::NON_PART_SENSITIVE, 117, "Delay Depth / Reverb Room Size", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A|Parameter::Class::MULTI_OR_SINGLE|Parameter::Class::NON_PART_SENSITIVE, 118, "Delay Lfo Shape", {0,5}, numToLfoShape, {}, true, true, false},
//    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A|Parameter::Class::MULTI_OR_SINGLE|Parameter::Class::NON_PART_SENSITIVE, 118, "Reverb Damping", {0,127}, {},{}, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A|Parameter::Class::MULTI_OR_SINGLE|Parameter::Class::NON_PART_SENSITIVE, 119, "Delay Color", {0,127}, paramTo7bitSigned, textTo7bitSigned, true, false, false},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A|Parameter::Class::MULTI_OR_SINGLE|Parameter::Class::NON_PART_SENSITIVE, 122, "Keyb Local", {0,1}, {},{}, false, false, true},
    {Parameter::Page::A, Parameter::Class::SOUNDBANK_A|Parameter::Class::MULTI_OR_SINGLE|Parameter::Class::NON_PART_SENSITIVE, 123, "All Notes Off", {0,127}, {},{}, false, false, false},

    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 1, "Arp Mode", {0,6}, numToArpMode, {}, true, true, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 2, "Arp Pattern Selct", {0,63}, {},{}, true, true, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 3, "Arp Octave Range", {0,3}, {},{}, true, true, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 4, "Arp Hold Enable", {0,1}, {},{}, true, false, true},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 5, "Arp Note Length", {0,127}, paramTo7bitSigned, textTo7bitSigned, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 6, "Arp Swing", {0,127}, numToArpSwing, {}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 7, "Lfo3 Rate", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 8, "Lfo3 Shape", {0,67}, numToLfoShape, {}, true, true, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 9, "Lfo3 Mode", {0,1}, numToLfoMode, {}, true, false, true},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 10, "Lfo3 Keyfollow", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 11, "Lfo3 Destination", {0,5}, numToLfoDest, {}, true, true, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 12, "Osc Lfo3 Amount", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 13, "Lfo3 Fade-In Time", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 16, "Clock Tempo", {0,127}, numToClockTempo, {}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 17, "Arp Clock", {0,17}, numToMusicDivision, {}, true, true, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 18, "Lfo1 Clock", {0,19}, numToMusicDivision, {}, true, true, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 19, "Lfo2 Clock", {0,19}, numToMusicDivision, {}, true, true, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::MULTI_OR_SINGLE|Parameter::Class::NON_PART_SENSITIVE, 20, "Delay Clock", {0,16}, numToMusicDivision, {}, true, true, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 21, "Lfo3 Clock", {0,19}, numToMusicDivision, {}, true, true, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 25, "Control Smooth Mode", {0,3}, numToControlSmoothMode,{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 26, "Bender Range Up", {0,127}, paramTo7bitSigned, textTo7bitSigned, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 27, "Bender Range Down", {0,127}, paramTo7bitSigned, textTo7bitSigned, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 28, "Bender Scale", {0,1}, numToLinExp, {}, true, false, true},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 30, "Filter1 Env Polarity", {0,1}, numToNegPos, {}, true, false, true},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 31, "Filter1 Env Polarity", {0,1}, numToNegPos, {}, true, false, true},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 32, "Filter2 Cutoff Link", {0,1}, {},{}, true, false, true},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 33, "Filter Keytrack Base", {0,127}, {},{}, true, false, true},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 34, "Osc FM Mode", {0,12}, numToOscFmMode,{}, true, true, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 35, "Osc Init Phase", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 36, "Punch Intensity", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 38, "Input Follower Mode",
        {0,9}, {},{}, true, true, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 39, "Vocoder Mode", {0,12}, {},{}, true, true, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 41, "Osc3 Mode", {0,67}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 42, "Osc3 Volume", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 43, "Osc3 Semitone", {0,127}, paramTo7bitSigned, textTo7bitSigned, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 44, "Osc3 Detune", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 45, "LowEQ Frequency", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 46, "HighEQ Frequency", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 47, "Osc1 Shape Velocity", {0,127}, paramTo7bitSigned, textTo7bitSigned, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 48, "Osc2 Shape Velocity", {0,127}, paramTo7bitSigned, textTo7bitSigned, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 49, "PulseWidth Velocity", {0,127}, paramTo7bitSigned, textTo7bitSigned, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 50, "Fm Amount Velocity", {0,127}, paramTo7bitSigned, textTo7bitSigned, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 51, "Soft Knob1 ShortName", {0,127}, {},{}, false, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 52, "Soft Knob2 ShortName", {0,127}, {},{}, false, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 54, "Filter1 EnvAmt Velocity", {0,127}, paramTo7bitSigned, textTo7bitSigned, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 55, "Filter2 EnvAmt Velocity", {0,127}, paramTo7bitSigned, textTo7bitSigned, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 56, "Resonance1 Velocity", {0,127}, paramTo7bitSigned, textTo7bitSigned, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 57, "Resonance2 Velocity", {0,127}, paramTo7bitSigned, textTo7bitSigned, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 58, "Second Output Balance", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 60, "Amp Velocity", {0,127}, paramTo7bitSigned, textTo7bitSigned, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 61, "Panorama Velocity", {0,127}, paramTo7bitSigned, textTo7bitSigned, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 62, "Soft Knob-1 Single", {0,127}, numToSoftKnobDest, {}, true, true, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 63, "Soft Knob-2 Single", {0,127}, numToSoftKnobDest, {}, true, true, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 64, "Assign1 Source", {0,27}, numToModMatrixSource, {}, true, true, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 65, "Assign1 Destination", {0,122}, numToModMatrixDest, {}, true, true, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 66, "Assign1 Amount", {0,127}, paramTo7bitSigned, textTo7bitSigned, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 67, "Assign2 Source", {0,27}, numToModMatrixSource,{}, true, true, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 68, "Assign2 Destination1", {0,122}, numToModMatrixDest,{}, true, true, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 69, "Assign2 Amount1", {0,127}, paramTo7bitSigned, textTo7bitSigned, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 70, "Assign2 Destination2", {0,122}, numToModMatrixDest, {}, true, true, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 71, "Assign2 Amount2", {0,127}, paramTo7bitSigned, textTo7bitSigned, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 72, "Assign3 Source", {0,27}, numToModMatrixSource, {}, true, true, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 73, "Assign3 Destination1", {0,122}, numToModMatrixDest,{}, true, true, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 74, "Assign3 Amount1", {0,127}, paramTo7bitSigned, textTo7bitSigned, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 75, "Assign3 Destination2", {0,122}, numToModMatrixDest,{}, true, true, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 76, "Assign3 Amount2", {0,127}, paramTo7bitSigned, textTo7bitSigned, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 77, "Assign2 Destination3", {0,122}, numToModMatrixDest,{}, true, true, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 78, "Assign2 Amount3", {0,127}, paramTo7bitSigned, textTo7bitSigned, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 79, "LFO1 Assign Dest", {0,122}, numToModMatrixDest, {}, true, true, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 80, "LFO1 Assign Amount", {0,127}, paramTo7bitSigned, textTo7bitSigned, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 81, "LFO2 Assign Dest", {0,122}, numToModMatrixDest, {}, true, true, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B, 82, "LFO2 Assign Amount", {0,127}, paramTo7bitSigned, textTo7bitSigned, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 84, "Phaser Mode", {0,6}, numToPhaserMode, {}, true, true, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 85, "Phaser Mix", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 86, "Phaser Rate", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 87, "Phaser Depth", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 88, "Phaser Frequency", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 89, "Phaser Feedback", {0,127}, paramTo7bitSigned, textTo7bitSigned, true, false, false},
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
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 102, "Assign 4 Source", {0,27}, numToModMatrixSource,{}, true, true, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 103, "Assign 4 Destination", {0,122}, numToModMatrixSource, {}, true, true, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C,104, "Assign 4 Amount", {0,127}, {},{}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 105, "Assign 5 Source", {0,27}, numToModMatrixSource, {}, true, true, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 106, "Assign 5 Destination", {0,122}, numToModMatrixDest, {}, true, true, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 107, "Assign 5 Amount", {0,127}, paramTo7bitSigned, textTo7bitSigned, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 108, "Assign 6 Source", {0,27}, numToModMatrixSource, {}, true, true, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 109, "Assign 6 Destination", {0,122}, numToModMatrixDest, {}, true, true, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 110, "Assign 6 Amount", {0,127}, paramTo7bitSigned, {}, true, false, false},
    {Parameter::Page::B, Parameter::Class::SOUNDBANK_B|Parameter::Class::VIRUS_C, 122, "Filter Select", {0,2}, {},{}, true, true, false},

    {Parameter::Page::C, Parameter::Class::MULTI_PARAM|Parameter::Class::NON_PART_SENSITIVE, 22, "Delay Output Select", {0,14}, {},{}, true, true, false},
    {Parameter::Page::C, Parameter::Class::MULTI_PARAM|Parameter::Class::BANK_PROGRAM_CHANGE_PARAM_BANK_SELECT, 31, "Part Bank Select", {0, 3 + 26}, {},{}, false, true, false},
    {Parameter::Page::C, Parameter::Class::MULTI_PARAM|Parameter::Class::BANK_PROGRAM_CHANGE_PARAM_BANK_SELECT, 32, "Part Bank Change", {0, 3 + 26}, {},{}, false, true, false},
    {Parameter::Page::C, Parameter::Class::MULTI_PARAM|Parameter::Class::BANK_PROGRAM_CHANGE_PARAM_BANK_SELECT, 33, "Part Program Change", {0,127}, {},{}, false, false, false},
    {Parameter::Page::C, Parameter::Class::MULTI_PARAM, 34, "Part Midi Channel", {0,15}, {},{}, false, true, false},
    {Parameter::Page::C, Parameter::Class::MULTI_PARAM, 35, "Part Low Key", {0,127}, {},{}, false, true, false},
    {Parameter::Page::C, Parameter::Class::MULTI_PARAM, 36, "Part High Key", {0,127}, {},{}, false, true, false},
    {Parameter::Page::C, Parameter::Class::MULTI_PARAM, 37, "Part Transpose", {0,127}, paramTo7bitSigned, textTo7bitSigned, false, false, false},
    {Parameter::Page::C, Parameter::Class::MULTI_PARAM, 38, "Part Detune", {0,127}, paramTo7bitSigned, textTo7bitSigned, false, false, false},
    {Parameter::Page::C, Parameter::Class::MULTI_PARAM, 39, "Part Volume", {0,127}, {},{}, true, false, false},
    {Parameter::Page::C, Parameter::Class::MULTI_PARAM, 40, "Part Midi Volume Init", {0,127}, {},{}, false, true, false},
    {Parameter::Page::C, Parameter::Class::MULTI_PARAM, 41, "Part Output Select", {0,14}, {},{}, false, true, false},
    {Parameter::Page::C, Parameter::Class::GLOBAL, 45, "Second Output Select", {0,15}, {},{}, false, true, false},
    {Parameter::Page::C, Parameter::Class::GLOBAL, 63, "Keyb Transpose Buttons", {0,1}, {},{}, false, false, true},
    {Parameter::Page::C, Parameter::Class::GLOBAL, 64, "Keyb Local", {0,1}, {},{}, false, false, true},
    {Parameter::Page::C, Parameter::Class::GLOBAL, 65, "Keyb Mode", {0,1}, {},{}, false, false, true},
    {Parameter::Page::C, Parameter::Class::GLOBAL, 66, "Keyb Transpose", {0,127}, paramTo7bitSigned, textTo7bitSigned, false, false, false},
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
    {Parameter::Page::C, Parameter::Class::GLOBAL, 92, "Master Tune", {0,127}, paramTo7bitSigned, textTo7bitSigned, false, false, false},
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
		ev.source = synthLib::MidiEventSourceEditor;
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
        const juce::ScopedLock sl(m_eventQueueLock);

    	m_virusOut.insert(m_virusOut.end(), newData.begin(), newData.end());
    }
}; // namespace Virus

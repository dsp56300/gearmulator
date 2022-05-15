#include "VirusController.h"

#include "BinaryData.h"
#include "PluginProcessor.h"

#include "../virusLib/microcontrollerTypes.h"

using MessageType = virusLib::SysexMessageType;

namespace Virus
{
    static constexpr uint8_t kSysExStart[] = {0xf0, 0x00, 0x20, 0x33, 0x01};
    static constexpr auto kHeaderWithMsgCodeLen = 7;

    Controller::Controller(AudioPluginAudioProcessor &p, unsigned char deviceId) : pluginLib::Controller(BinaryData::parameterDescriptions_C_json), m_processor(p), m_deviceId(deviceId)
    {
//		assert(m_descriptions.getDescriptions().size() == Param_Count && "size of enum must match size of parameter descriptions");

        registerParams(p);

		juce::PropertiesFile::Options opts;
		opts.applicationName = "DSP56300 Emulator";
		opts.filenameSuffix = ".settings";
		opts.folderName = "DSP56300 Emulator";
		opts.osxLibrarySubFolder = "Application Support/DSP56300 Emulator";
		m_config = new juce::PropertiesFile(opts);

		// add lambda to enforce updating patches when virus switch from/to multi/single.
		const auto& params = findSynthParam(0, 0x72, 0x7a);
		for (const auto& parameter : params)
		{
			parameter->onValueChanged = [this] {
				const uint8_t prg = isMultiMode() ? 0x0 : virusLib::SINGLE;
				sendSysEx(constructMessage({ MessageType::REQUEST_SINGLE, 0x0, prg }));
				sendSysEx(constructMessage({ MessageType::REQUEST_MULTI, 0x0, prg }));

				if (onMsgDone)
				{
					onMsgDone();
				}
			};
		}
		sendSysEx(constructMessage({MessageType::REQUEST_TOTAL}));
		sendSysEx(constructMessage({MessageType::REQUEST_ARRANGEMENT}));

    	for(uint8_t i=3; i<=8; ++i)
			sendSysEx(constructMessage({MessageType::REQUEST_BANK_SINGLE, i}));		 
    	startTimer(5);
	}

    Controller::~Controller()
    {
	    stopTimer();
		delete m_config;
    }

	void Controller::parseMessage(const SysEx &msg)
	{
        if (msg.size() < 8)
            return; // shorter than expected!

        if (msg[msg.size() - 1] != 0xf7)
            return; // invalid end?!?

        for (size_t i = 0; i < msg.size(); ++i)
        {
            if (i < 5)
            {
                if (msg[i] != kSysExStart[i])
                    return; // invalid header
            }
            else if (i == 5)
            {
                if (msg[i] != m_deviceId && msg[i] != 0x10)
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
                case MessageType::REQUEST_SINGLE:
                case MessageType::REQUEST_MULTI:
                case MessageType::REQUEST_GLOBAL:
                case MessageType::REQUEST_TOTAL:
                    sendSysEx(msg);
                    break;
				default:
					LOG("Controller: Begin Unhandled SysEx! --");
                    printMessage(msg);
					LOG("Controller: End Unhandled SysEx! --");
                }
            }
        }
    }

    juce::Value* Controller::getParamValue(uint8_t ch, uint8_t bank, uint8_t paramIndex)
	{
		const auto& params = findSynthParam(ch, static_cast<uint8_t>(virusLib::PAGE_A + bank), paramIndex);
		if (params.empty())
		{
            // unregistered param?
            return nullptr;
        }
		return &params.front()->getValueObject();
	}

    void Controller::parseParamChange(const SysEx &msg)
    {
        constexpr auto pos = kHeaderWithMsgCodeLen - 1;
		const auto page = msg[pos];
		const auto ch = msg[pos + 1];
		const auto index = msg[pos + 2];
		const auto value = msg[pos + 3];

        const auto& partParams = findSynthParam(ch, page, index);

    	if (partParams.empty() && ch != 0)
		{
            // ensure it's not global
			const auto& globalParams = findSynthParam(0, page, index);
			if (globalParams.empty())
			{
                jassertfalse;
                return;
            }
            for (const auto& param : globalParams)
            {
				auto flags = param->getDescription().classFlags;
				if (!(flags & (int)pluginLib::ParameterClass::Global) && !(flags & (int)pluginLib::ParameterClass::NonPartSensitive))
				{
					jassertfalse;
					return;
				}
            }
			for (const auto& param : globalParams)
				param->setValueFromSynth(value, true);
		}
		for (const auto& param : partParams)
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
	virusLib::VirusModel Controller::getVirusModel() const
	{
		return parseAsciiText(m_singles[2][0].data, 128 + 112) == "Taurus  JS" ? virusLib::VirusModel::B : virusLib::VirusModel::C;
	}
    juce::StringArray Controller::getSinglePresetNames(virusLib::BankNumber _bank) const
    {
		if (_bank == virusLib::BankNumber::EditBuffer)
		{
			jassertfalse;
			return {};
		}

		const auto bank = virusLib::toArrayIndex(_bank);

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
        for (const auto& m_multi : m_multis)
	        bankNames.add(parseAsciiText(m_multi.data, 0));
        return bankNames;
    }
	void Controller::setSinglePresetName(uint8_t part, juce::String _name) {
		constexpr uint8_t asciiStart = 112;
		_name = _name.substring(0,kNameLength).paddedRight(' ', kNameLength);
		for (int i = 0; i < kNameLength; i++)
		{
			auto* value = getParamValue(part, 1, asciiStart+i);
			jassert(value);
			value->setValue(static_cast<uint8_t>(_name[i]));
		}
	}
	juce::String Controller::getCurrentPartPresetName(uint8_t part)
	{
		// expensive but fresh!
		constexpr uint8_t asciiStart = 112;
		char text[kNameLength + 1];
		text[kNameLength] = 0; // termination
		for (auto pos = 0; pos < kNameLength; ++pos)
		{
			auto* value = getParamValue(part, 1, asciiStart + pos);
			jassert(value);
			text[pos] = static_cast<int>(value->getValue());
		}
		return {text};
	}

	void Controller::setCurrentPartPreset(uint8_t _part, const virusLib::BankNumber _bank, uint8_t _prg)
	{
    	if(_bank == virusLib::BankNumber::EditBuffer || _prg > 127)
    	{
			jassertfalse;
			return;
    	}

    	const auto bank = virusLib::toArrayIndex(_bank);

		if (bank >= m_singles.size())
		{
			jassertfalse;
			return;
		}

		const uint8_t pt = isMultiMode() ? _part : virusLib::SINGLE;

		sendSysEx(constructMessage({ MessageType::PARAM_CHANGE_C, pt, virusLib::PART_BANK_SELECT, virusLib::toMidiByte(_bank) }));
		sendSysEx(constructMessage({ MessageType::PARAM_CHANGE_C, pt, virusLib::PART_PROGRAM_CHANGE, _prg }));

		sendSysEx(constructMessage({MessageType::REQUEST_SINGLE, 0x0, pt}));

		m_currentBank[_part] = _bank;
		m_currentProgram[_part] = _prg;
	}

	virusLib::BankNumber Controller::getCurrentPartBank(uint8_t part) const { return m_currentBank[part]; }
	uint8_t Controller::getCurrentPartProgram(uint8_t part) const { return m_currentProgram[part]; }
	void Controller::parseSingle(const SysEx &msg)
	{
        constexpr auto pageSize = 128;
        constexpr auto expectedDataSize = pageSize * 2 + 1 + 1; // we have 2 pages
        constexpr auto checkSumSize = 1;
        const auto dataSize = msg.size() - (kHeaderWithMsgCodeLen + 1); // 1 end byte, 1 bank, 1 prg
        const auto hasChecksum = dataSize == expectedDataSize + checkSumSize;
        assert(hasChecksum || dataSize == expectedDataSize);

        SinglePatch patch;
        patch.bankNumber = virusLib::fromMidiByte(msg[kHeaderWithMsgCodeLen]);
        patch.progNumber = msg[kHeaderWithMsgCodeLen + 1];
        [[maybe_unused]] const auto dataSum = copyData(msg, kHeaderWithMsgCodeLen + 2, patch.data);

        if (hasChecksum)
        {
            const auto checksum = msg[msg.size() - 2];
            const auto deviceId = msg[5];
            [[maybe_unused]] const auto expectedSum = (deviceId + 0x10 + virusLib::toMidiByte(patch.bankNumber) + patch.progNumber + dataSum) & 0x7f;
            assert(expectedSum == checksum);
        }
		if (patch.bankNumber == virusLib::BankNumber::EditBuffer)
		{
			// virus sends also the single buffer not matter what's the mode.
			// instead of keeping both, we 'encapsulate' this into first channel.
			// the logic to maintain this is done by listening the global single/multi param.
			if (isMultiMode() && patch.progNumber == virusLib::SINGLE)
				return;
			if (!isMultiMode() && patch.progNumber == 0x0)
				return;

			const uint8_t ch = patch.progNumber == virusLib::SINGLE ? 0 : patch.progNumber;
			for (size_t i = 0; i < std::size(patch.data); i++)
			{
				const uint8_t page = virusLib::PAGE_A + static_cast<uint8_t>(i / pageSize);
				const auto& params = findSynthParam(ch, page, i % pageSize);
				if (!params.empty())
				{
					for (const auto& param : params)
					{
						if ((param->getDescription().classFlags & (int)pluginLib::ParameterClass::MultiOrSingle) && isMultiMode())
							continue;
						param->setValueFromSynth(patch.data[i], true);
					}
				}
			}
			if (onProgramChange)
				onProgramChange();
		}
		else
			m_singles[virusLib::toArrayIndex(patch.bankNumber)][patch.progNumber] = patch;

        constexpr auto namePos = kHeaderWithMsgCodeLen + 2 + 128 + 112;
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

		/* If it's a multi edit buffer, set the part page C single parameters to their multi equivalents */
		if (patch.bankNumber == 0) {
			for (uint8_t pt = 0; pt < 16; pt++) {
				for(int i = 0; i < 8; i++) {
					const auto& params = findSynthParam(pt, virusLib::PAGE_C, virusLib::PART_MIDI_CHANNEL + i);
					for (const auto& p : params)
						p->setValueFromSynth(patch.data[virusLib::MD_PART_MIDI_CHANNEL + (i * 16) + pt], true);
				}
				const auto& params = findSynthParam(pt, virusLib::PAGE_B, virusLib::CLOCK_TEMPO);
				for (const auto& p : params)
					p->setValueFromSynth(patch.data[virusLib::MD_CLOCK_TEMPO], true);
/*				if (auto* p = findSynthParam(pt, virusLib::PAGE_A, virusLib::EFFECT_SEND)) {
					p->setValueFromSynth(patch.data[virusLib::MD_PART_EFFECT_SEND], true);
				}*/
				m_currentBank[pt] = static_cast<virusLib::BankNumber>(patch.data[virusLib::MD_PART_BANK_NUMBER + pt] + 1);
				m_currentProgram[pt] = patch.data[virusLib::MD_PART_PROGRAM_NUMBER + pt];
			}
		}
        if (hasChecksum)
        {
            const int expectedChecksum = msg[msg.size() - 2];
            const auto msgDeviceId = msg[5];
            const int checksum = (msgDeviceId + 0x11 + patch.bankNumber + patch.progNumber + dataSum) & 0x7f;
            assert(checksum == expectedChecksum);
        }
		if (patch.bankNumber == 0) {
			m_currentMulti = patch;
		} else {
			m_multis[patch.progNumber] = patch;
		}
    }

	void Controller::parseControllerDump(synthLib::SMidiEvent &m)
	{
		const uint8_t status = m.a & 0xf0;
    	const uint8_t part = m.a & 0x0f;

		uint8_t page;

		if (status == synthLib::M_CONTROLCHANGE)
			page = virusLib::PAGE_A;
		else if (status == synthLib::M_POLYPRESSURE)
			page = virusLib::PAGE_B;
		else
			return;

		DBG(juce::String::formatted("Set part: %d bank: %s param: %d  value: %d", part, page == 0 ? "A" : "B", m.b, m.c));
		const auto& params = findSynthParam(part, page, m.b);
		for (const auto & p : params)
			p->setValueFromSynth(m.c, true);
	}

    uint8_t Controller::copyData(const SysEx &src, int startPos, std::array<uint8_t, kDataSizeInBytes>& dst)
    {
        uint8_t sum = 0;

    	size_t iSrc = startPos;

        for (size_t iDst = 0; iSrc < src.size() && iDst < dst.size(); ++iSrc, ++iDst)
        {
            dst[iDst] = src[iSrc];
            sum += dst[iDst];
        }
        return sum;
    }

    template <typename T> juce::String Controller::parseAsciiText(const T &msg, const int start)
    {
        char text[kNameLength + 1];
        text[kNameLength] = 0; // termination
        for (auto pos = 0; pos < kNameLength; ++pos)
            text[pos] = msg[start + pos];
        return {text};
    }

    void Controller::printMessage(const SysEx &msg)
    {
		std::stringstream ss;
        for (auto &m : msg)
        {
            ss << std::hex << static_cast<int>(m) << ",";
        }
		LOG((ss.str()));
    }

    void Controller::sendSysEx(const SysEx &msg) const
    {
        synthLib::SMidiEvent ev;
        ev.sysex = msg;
		ev.source = synthLib::MidiEventSourceEditor;
        m_processor.addMidiEvent(ev);
    }

    void Controller::onStateLoaded() const
    {
		sendSysEx(constructMessage({ MessageType::REQUEST_TOTAL }));
		sendSysEx(constructMessage({ MessageType::REQUEST_ARRANGEMENT }));
	}

    std::vector<uint8_t> Controller::constructMessage(SysEx msg) const
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
            if (msg.sysex.empty())
            {
                // no sysex
				parseControllerDump(msg);
			}
            else
			{
				parseMessage(msg.sysex);               
			}
        }
        m_virusOut.clear();
    }

    void Controller::dispatchVirusOut(const std::vector<synthLib::SMidiEvent> &newData)
    {
        const juce::ScopedLock sl(m_eventQueueLock);

        m_virusOut.insert(m_virusOut.end(), newData.begin(), newData.end());
    }

    void Controller::sendParameterChange(const pluginLib::Parameter& _parameter, uint8_t _value)
    {
        const auto& desc = _parameter.getDescription();
		sendSysEx(constructMessage({static_cast<uint8_t>(desc.page), _parameter.getPart(), desc.index, _value}));
    }

    pluginLib::Parameter* Controller::createParameter(pluginLib::Controller& _controller, const pluginLib::Description& _desc, uint8_t _part, int _uid)
    {
        return new Parameter(_controller, _desc, _part, _uid);
    }
}; // namespace Virus

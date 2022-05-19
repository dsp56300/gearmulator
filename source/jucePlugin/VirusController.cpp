#include "VirusController.h"

#include "VirusParameter.h"

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
				requestSingle(0, prg);
                requestMulti(0, prg);

				if (onMsgDone)
				{
					onMsgDone();
				}
			};
		}
		requestTotal();
		requestArrangement();

    	for(uint8_t i=3; i<=8; ++i)
			requestSingleBank(i);

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

    void Controller::parseParamChange(const SysEx& msg)
    {
	    pluginLib::MidiPacket::Data params;
        pluginLib::MidiPacket::ParamValues parameterValues;

        if(!parseMidiPacket("parameterchange", params, parameterValues, msg))
            return;

    	const auto page = params[pluginLib::MidiDataType::Page];
		const auto part = params[pluginLib::MidiDataType::Part];
		const auto index = params[pluginLib::MidiDataType::ParameterIndex];
		const auto value = params[pluginLib::MidiDataType::ParameterValue];

        const auto& partParams = findSynthParam(part, page, index);

    	if (partParams.empty() && part != 0)
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
		return m_singles[2][0].name == "Taurus  JS" ? virusLib::VirusModel::B : virusLib::VirusModel::C;
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
            bankNames.add(m_singles[bank][i].name);
        return bankNames;
    }
	void Controller::setSinglePresetName(uint8_t _part, const juce::String& _name)
	{
		for (int i=0; i<kNameLength; i++)
		{
	        const std::string paramName = "SingleName" + std::to_string(i);
            const auto idx = getParameterIndexByName(paramName);
            if(idx == InvalidParameterIndex)
                break;

            auto* param = getParameter(idx, _part);
            if(!param)
                break;
            auto& v = param->getValueObject();
            if(i >= _name.length())
				v.setValue(static_cast<uint8_t>(' '));
            else
				v.setValue(static_cast<uint8_t>(_name[i]));
		}
	}

	bool Controller::isMultiMode()
	{
		auto* value = getParamValue(0, 2, 0x7a);
		jassert(value);
		return value->getValue();
	}

	juce::String Controller::getCurrentPartPresetName(const uint8_t _part) const
	{
        std::string name;
		for (int i=0; i<kNameLength; i++)
		{
	        const std::string paramName = "SingleName" + std::to_string(i);
            const auto idx = getParameterIndexByName(paramName);
            if(idx == InvalidParameterIndex)
                break;

            auto* param = getParameter(idx, _part);
            if(!param)
                break;
            const int v = param->getValueObject().getValue();
			name += static_cast<char>(v);
		}
        return name;
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

		sendParameterChange(MessageType::PARAM_CHANGE_C, pt, virusLib::PART_BANK_SELECT, virusLib::toMidiByte(_bank));
		sendParameterChange(MessageType::PARAM_CHANGE_C, pt, virusLib::PART_PROGRAM_CHANGE, _prg);

		requestSingle(0x0, pt);

		m_currentBank[_part] = _bank;
		m_currentProgram[_part] = _prg;
	}

	virusLib::BankNumber Controller::getCurrentPartBank(const uint8_t _part) const
    {
	    return m_currentBank[_part];
    }
	uint8_t Controller::getCurrentPartProgram(const uint8_t _part) const
    {
	    return m_currentProgram[_part];
    }
	void Controller::parseSingle(const SysEx& msg)
	{
		pluginLib::MidiPacket::Data data;
        pluginLib::MidiPacket::ParamValues parameterValues;

    	if(!parseMidiPacket("singledump", data, parameterValues, msg))
            return;

        SinglePatch patch;

        patch.bankNumber = virusLib::fromMidiByte(data[pluginLib::MidiDataType::Bank]);
        patch.progNumber = data[pluginLib::MidiDataType::Program];
        
        for(uint32_t i=0; i<kNameLength; ++i)
        {
	        const std::string paramName = "SingleName" + std::to_string(i);
            const auto idx = getParameterIndexByName(paramName);
            if(idx == InvalidParameterIndex)
                break;

            const auto it = parameterValues.find(std::make_pair(pluginLib::MidiPacket::AnyPart, idx));
            if(it == parameterValues.end())
                break;

            patch.name += static_cast<char>(it->second);
        }

		copyData(msg, kHeaderWithMsgCodeLen + 2, patch.data);

		if (patch.bankNumber == virusLib::BankNumber::EditBuffer)
		{
			// virus sends also the single buffer not matter what's the mode. (?? no, both is requested, so both is sent)
			// instead of keeping both, we 'encapsulate' this into first channel.
			// the logic to maintain this is done by listening the global single/multi param.
			if (isMultiMode() && patch.progNumber == virusLib::SINGLE)
				return;
			if (!isMultiMode() && patch.progNumber == 0x0)
				return;

			const uint8_t ch = patch.progNumber == virusLib::SINGLE ? 0 : patch.progNumber;

            for(auto it = parameterValues.begin(); it != parameterValues.end(); ++it)
            {
	            auto* p = getParameter(it->first.second, ch);
				p->setValueFromSynth(it->second, true);

	            for (const auto& linkedParam : p->getLinkedParameters())
		            linkedParam->setValueFromSynth(it->second, true);
            }

            if (onProgramChange)
				onProgramChange();
		}
		else
			m_singles[virusLib::toArrayIndex(patch.bankNumber)][patch.progNumber] = patch;
    }

    void Controller::parseMulti(const SysEx& _msg)
    {
        pluginLib::MidiPacket::Data data;
	    pluginLib::MidiPacket::ParamValues paramValues;

    	if(!parseMidiPacket("multidump", data, paramValues, _msg))
            return;

        const auto bankNumber = data[pluginLib::MidiDataType::Bank];

		/* If it's a multi edit buffer, set the part page C parameters to their multi equivalents */
		if (bankNumber == 0)
        {
			for (const auto & paramValue : paramValues)
			{
                const auto part = paramValue.first.first;
                const auto index = paramValue.first.second;
                const auto value = paramValue.second;

                auto* param = getParameter(index, part);
                if(!param)
                    continue;

                const auto& desc = param->getDescription();

                if(desc.page != virusLib::PAGE_C)
                    continue;

                param->setValueFromSynth(value);
			}
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
		requestTotal();
		requestArrangement();
	}

    bool Controller::requestProgram(uint8_t _bank, uint8_t _program, bool _multi) const
    {
        std::map<pluginLib::MidiDataType, uint8_t> data;

        data.insert(std::make_pair(pluginLib::MidiDataType::Bank, _bank));
        data.insert(std::make_pair(pluginLib::MidiDataType::Program, _program));

		return sendSysEx(_multi ? "requestmulti" : "requestsingle", data);
    }

    bool Controller::requestSingle(uint8_t _bank, uint8_t _program) const
    {
        return requestProgram(_bank, _program, false);
    }

    bool Controller::requestMulti(uint8_t _bank, uint8_t _program) const
    {
        return requestProgram(_bank, _program, true);
    }

    bool Controller::requestSingleBank(uint8_t _bank) const
    {
        std::map<pluginLib::MidiDataType, uint8_t> data;
        data.insert(std::make_pair(pluginLib::MidiDataType::Bank, _bank));

        return sendSysEx("requestsinglebank", data);
    }

    bool Controller::requestTotal() const
    {
        return sendSysEx("requesttotal");
    }

    bool Controller::requestArrangement() const
    {
        return sendSysEx("requestarrangement");
    }

    bool Controller::sendSysEx(const std::string& _packetType) const
    {
	    std::map<pluginLib::MidiDataType, uint8_t> params;
        return sendSysEx(_packetType, params);
    }

    bool Controller::sendSysEx(const std::string& _packetType, std::map<pluginLib::MidiDataType, uint8_t>& _params) const
    {
	    std::vector<uint8_t> sysex;

        _params.insert(std::make_pair(pluginLib::MidiDataType::DeviceId, m_deviceId));

    	if(!createMidiDataFromPacket(sysex, _packetType, _params, 0))
            return false;

        sendSysEx(sysex);
        return true;
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

        sendParameterChange(desc.page, _parameter.getPart(), desc.index, _value);
    }

    bool Controller::sendParameterChange(uint8_t _page, uint8_t _part, uint8_t _index, uint8_t _value) const
    {
        std::map<pluginLib::MidiDataType, uint8_t> data;

        data.insert(std::make_pair(pluginLib::MidiDataType::Page, _page));
        data.insert(std::make_pair(pluginLib::MidiDataType::Part, _part));
        data.insert(std::make_pair(pluginLib::MidiDataType::ParameterIndex, _index));
        data.insert(std::make_pair(pluginLib::MidiDataType::ParameterValue, _value));

    	return sendSysEx("parameterchange", data);
    }

    pluginLib::Parameter* Controller::createParameter(pluginLib::Controller& _controller, const pluginLib::Description& _desc, uint8_t _part, int _uid)
    {
        return new Parameter(_controller, _desc, _part, _uid);
    }

    std::vector<uint8_t> Controller::createSingleDump(uint8_t _part, uint8_t _bank, uint8_t _program)
    {
	    std::map<pluginLib::MidiDataType, uint8_t> data;

        data.insert(std::make_pair(pluginLib::MidiDataType::DeviceId, m_deviceId));
        data.insert(std::make_pair(pluginLib::MidiDataType::Bank, _bank));
        data.insert(std::make_pair(pluginLib::MidiDataType::Program, _program));

        std::vector<uint8_t> dst;

    	if(!createMidiDataFromPacket(dst, "singledump", data, _part))
            return {};

        return dst;
    }
}; // namespace Virus

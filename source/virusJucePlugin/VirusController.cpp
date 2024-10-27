#include "VirusController.h"

#include <fstream>

#include "ParameterNames.h"
#include "VirusProcessor.h"

#include "virusLib/microcontrollerTypes.h"
#include "synthLib/os.h"

#include "VirusEditor.h"

using MessageType = virusLib::SysexMessageType;

namespace virus
{
    constexpr const char* g_midiPacketNames[] =
    {
	    "requestsingle",
	    "requestmulti",
	    "requestsinglebank",
	    "requestmultibank",
	    "requestarrangement",
	    "requestglobal",
	    "requesttotal",
	    "requestcontrollerdump",
	    "parameterchange",
	    "singledump",
	    "multidump",
	    "singledump_C",
    };

    static_assert(std::size(g_midiPacketNames) == static_cast<size_t>(Controller::MidiPacketType::Count));

    const char* midiPacketName(Controller::MidiPacketType _type)
    {
	    return g_midiPacketNames[static_cast<uint32_t>(_type)];
    }

    Controller::Controller(VirusProcessor& p, const virusLib::DeviceModel _defaultModel, unsigned char deviceId)
		: pluginLib::Controller(p, virusLib::isTIFamily(_defaultModel) ? "parameterDescriptions_TI.json" : "parameterDescriptions_C.json")
		, m_processor(p)
		, m_defaultModel(_defaultModel)
		, m_deviceId(deviceId)
		, m_onRomChanged(p.evRomChanged)
    {
     	registerParams(p);

		// add lambda to enforce updating patches when virus switches from/to multi/single.
        const auto paramIdx = getParameterIndexByName(g_paramPlayMode);
		auto* parameter = getParameter(paramIdx);
        if(parameter)
		{
			parameter->onValueChanged.addListener([this](pluginLib::Parameter*)
			{
				const uint8_t prg = isMultiMode() ? 0x0 : virusLib::SINGLE;
				requestSingle(0, prg);
                requestMulti(0, prg);

				if (onMsgDone)
				{
					onMsgDone();
				}
			});
		}

		requestTotal();
		requestArrangement();

		// ABC models have different factory presets depending on the used ROM, but for the TI we have all presets from all models loaded anyway so no need to replace them at runtime
		if(isTIFamily(m_processor.getModel()))
		{
			requestRomBanks();
		}
		else
		{
			m_onRomChanged = [this](const virusLib::ROMFile*)
			{
				requestRomBanks();
			};
		}
	}

    Controller::~Controller() = default;

    bool Controller::parseSysexMessage(const pluginLib::SysEx& _msg, synthLib::MidiEventSource _source)
	{
        std::string name;
    	pluginLib::MidiPacket::Data data;
        pluginLib::MidiPacket::ParamValues parameterValues;

		if(_msg.size() > 6 && _msg[6] == virusLib::DUMP_EMU_SYNTHSTATE)
		{
			if(!m_frontpanelState.fromMidiEvent(_msg))
				return false;
			onFrontPanelStateChanged(m_frontpanelState);
			return true;
		}

        if(parseMidiPacket(name,  data, parameterValues, _msg))
        {
            const auto deviceId = data[pluginLib::MidiDataType::DeviceId];

            if(deviceId != m_deviceId && deviceId != virusLib::OMNI_DEVICE_ID)
                return false; // not intended to this device!

            if(name == midiPacketName(MidiPacketType::SingleDump) || name == midiPacketName(MidiPacketType::SingleDump_C))
                parseSingle(_msg, data, parameterValues);
            else if(name == midiPacketName(MidiPacketType::MultiDump))
                parseMulti(_msg, data, parameterValues);
            else if(name == midiPacketName(MidiPacketType::ParameterChange))
            {
				// TI DSP sends parameter changes back as sysex, unsure why. Ignore them as it stops
				// host automation because the host thinks we "edit" the parameter
				if(_source != synthLib::MidiEventSource::Plugin)
	                parseParamChange(data);
            }
            else
            {
		        LOG("Controller: Begin unhandled SysEx! --");
		        printMessage(_msg);
		        LOG("Controller: End unhandled SysEx! --");
				return false;
            }
			return true;
        }

        LOG("Controller: Begin unknown SysEx! --");
        printMessage(_msg);
        LOG("Controller: End unknown SysEx! --");
		return false;
    }

    bool Controller::parseControllerMessage(const synthLib::SMidiEvent& e)
    {
		return parseControllerDump(e);
    }

    void Controller::parseParamChange(const pluginLib::MidiPacket::Data& _data)
    {
    	const auto page  = _data.find(pluginLib::MidiDataType::Page)->second;
		const auto part  = _data.find(pluginLib::MidiDataType::Part)->second;
		const auto index = _data.find(pluginLib::MidiDataType::ParameterIndex)->second;
		const auto value = _data.find(pluginLib::MidiDataType::ParameterValue)->second;

        const auto& partParams = findSynthParam(part == virusLib::SINGLE ? 0 : part, page, index);

    	if (partParams.empty() && part != 0 && part != virusLib::SINGLE)
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
				if (!param->getDescription().isNonPartSensitive())
				{
					jassertfalse;
					return;
				}
            }
			for (const auto& param : globalParams)
				param->setValueFromSynth(value, pluginLib::Parameter::Origin::Midi);
		}
		for (const auto& param : partParams)
			param->setValueFromSynth(value, pluginLib::Parameter::Origin::Midi);
		// TODO:
        /**
         If a
        global  parameter  or  a  Multi  parameter  is  ac-
        cessed,  which  is  not  part-sensitive  (e.g.  Input
        Boost  or  Multi  Delay  Time),  the  part  number  is
        ignored
         */
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

    std::string Controller::getSinglePresetName(const pluginLib::MidiPacket::ParamValues& _values) const
    {
        return getPresetName("SingleName", _values);
    }

    std::string Controller::getSinglePresetName(const pluginLib::MidiPacket::AnyPartParamValues& _values) const
    {
        return getPresetName("SingleName", _values);
    }

    std::string Controller::getMultiPresetName(const pluginLib::MidiPacket::ParamValues& _values) const
    {
        return getPresetName("MultiName", _values);
    }

    std::string Controller::getPresetName(const std::string& _paramNamePrefix, const pluginLib::MidiPacket::ParamValues& _values) const
    {
        std::string name;
        for(uint32_t i=0; i<kNameLength; ++i)
        {
	        const std::string paramName = _paramNamePrefix + std::to_string(i);
            const auto idx = getParameterIndexByName(paramName);
            if(idx == InvalidParameterIndex)
                break;

            const auto it = _values.find(std::make_pair(pluginLib::MidiPacket::AnyPart, idx));
            if(it == _values.end())
                break;

            name += static_cast<char>(it->second);
        }
        return name;
    }

    std::string Controller::getPresetName(const std::string& _paramNamePrefix, const pluginLib::MidiPacket::AnyPartParamValues& _values) const
    {
        std::string name;
        for(uint32_t i=0; i<kNameLength; ++i)
        {
	        const std::string paramName = _paramNamePrefix + std::to_string(i);
            const auto idx = getParameterIndexByName(paramName);
            if(idx == InvalidParameterIndex)
                break;

            const auto it = _values[idx];
            if(!it)
                break;

            name += static_cast<char>(*it);
        }
        return name;
    }

    void Controller::setSinglePresetName(const uint8_t _part, const juce::String& _name) const
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

    void Controller::setSinglePresetName(pluginLib::MidiPacket::AnyPartParamValues& _values, const std::string& _name) const
    {
        for(uint32_t i=0; i<kNameLength; ++i)
        {
	        const std::string paramName = "SingleName" + std::to_string(i);
            const auto idx = getParameterIndexByName(paramName);
            if(idx == InvalidParameterIndex)
                break;

            _values[idx] = (i < _name.size()) ? _name[i] : ' ';
        }
    }

    bool Controller::isMultiMode() const
	{
		return getParameter(g_paramPlayMode, 0)->getUnnormalizedValue();
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
            const auto v = param->getUnnormalizedValue();
			name += static_cast<char>(v);
		}
        return name;
	}

	void Controller::setCurrentPartPreset(uint8_t _part, const virusLib::BankNumber _bank, uint8_t _prg)
	{
    	if(_bank == virusLib::BankNumber::EditBuffer || _prg > m_singles[0].size())
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

		requestSingle(toMidiByte(virusLib::BankNumber::EditBuffer), pt);

		m_currentBank[_part] = _bank;
		m_currentProgram[_part] = _prg;
        m_currentPresetSource[_part] = PresetSource::Rom;
	}

	void Controller::setCurrentPartPresetSource(uint8_t _part, PresetSource _source)
	{
        m_currentPresetSource[_part] = _source;
	}

	virusLib::BankNumber Controller::getCurrentPartBank(const uint8_t _part) const
    {
	    return m_currentBank[_part];
    }

	uint8_t Controller::getCurrentPartProgram(const uint8_t _part) const
    {
	    return m_currentProgram[_part];
    }

	Controller::PresetSource Controller::getCurrentPartPresetSource(uint8_t _part) const
	{
        return m_currentPresetSource[_part];
	}

	bool Controller::parseSingle(pluginLib::MidiPacket::Data& _data, pluginLib::MidiPacket::AnyPartParamValues& _parameterValues, const pluginLib::SysEx& _msg) const
	{
        MidiPacketType unused;
        return parseSingle(_data, _parameterValues, _msg, unused);
	}

	bool Controller::parseSingle(pluginLib::MidiPacket::Data& _data, pluginLib::MidiPacket::AnyPartParamValues& _parameterValues, const pluginLib::SysEx& _msg, MidiPacketType& usedPacketType) const
	{
        const auto packetName = midiPacketName(MidiPacketType::SingleDump);

    	auto* m = getMidiPacket(packetName);

    	if(!m)
            return false;

    	usedPacketType = MidiPacketType::SingleDump;

        if(_msg.size() > m->size())
        {
	        pluginLib::SysEx temp;
            temp.insert(temp.begin(), _msg.begin(), _msg.begin() + (m->size()-1));
            temp.push_back(0xf7);
	    	return parseMidiPacket(*m, _data, _parameterValues, temp);
        }

		if(_msg.size() < m->size())
        {
			const auto* mc = getMidiPacket(midiPacketName(MidiPacketType::SingleDump_C));
	    	if(!mc)
	            return false;
            usedPacketType = MidiPacketType::SingleDump_C;
	    	return parseMidiPacket(*mc, _data, _parameterValues, _msg);
        }

    	return parseMidiPacket(*m, _data, _parameterValues, _msg);
    }

	void Controller::parseSingle(const pluginLib::SysEx& _msg, const pluginLib::MidiPacket::Data& _data, const pluginLib::MidiPacket::ParamValues& _parameterValues)
	{
        SinglePatch patch;

        patch.bankNumber = virusLib::fromMidiByte(_data.find(pluginLib::MidiDataType::Bank)->second);
        patch.progNumber = _data.find(pluginLib::MidiDataType::Program)->second;

        patch.name = getSinglePresetName(_parameterValues);

        patch.data = _msg;

		if (patch.bankNumber == virusLib::BankNumber::EditBuffer)
		{
            if(patch.progNumber == virusLib::SINGLE)
                m_singleEditBuffer = patch;
            else
                m_singleEditBuffers[patch.progNumber] = patch;

			// virus sends also the single buffer not matter what's the mode. (?? no, both is requested, so both is sent)
			// instead of keeping both, we 'encapsulate' this into first channel.
			// the logic to maintain this is done by listening the global single/multi param.
			if (isMultiMode() && patch.progNumber == virusLib::SINGLE)
				return;
			if (!isMultiMode() && patch.progNumber == 0x0)
				return;

			const uint8_t ch = patch.progNumber == virusLib::SINGLE ? 0 : patch.progNumber;

            const auto locked = m_locking.getLockedParameterNames(ch);

            for(auto it = _parameterValues.begin(); it != _parameterValues.end(); ++it)
            {
	            auto* p = getParameter(it->first.second, ch);

                if(locked.find(p->getDescription().name) == locked.end())
					p->setValueFromSynth(it->second, pluginLib::Parameter::Origin::PresetChange);
            }

            getProcessor().updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withProgramChanged(true));

            if(m_currentPresetSource[ch] != PresetSource::Browser)
            {
	            bool found = false;
	            for(size_t b=0; b<m_singles.size() && !found; ++b)
	            {
		            const auto& singlePatches = m_singles[b];

	                for(size_t s=0; s<singlePatches.size(); ++s)
	                {
	                    const auto& singlePatch = singlePatches[s];

	                    if(singlePatch.name == patch.name)
	                    {
	                        m_currentBank[ch] = virusLib::fromArrayIndex(static_cast<uint8_t>(b));
	                        m_currentProgram[ch] = static_cast<uint8_t>(s);
	                        m_currentPresetSource[ch] = PresetSource::Rom;
	                        found = true;
	                        break;
	                    }
	                }
	            }

	            if(!found)
	            {
		            m_currentProgram[ch] = 0;
	                m_currentBank[ch] = virusLib::BankNumber::EditBuffer;
	                m_currentPresetSource[ch] = PresetSource::Unknown;
	            }
            }
            else
            {
	            m_currentProgram[ch] = 0;
                m_currentBank[ch] = virusLib::BankNumber::EditBuffer;
            }

			if (onProgramChange)
				onProgramChange(patch.progNumber);
		}
		else
		{
            const auto bank = toArrayIndex(patch.bankNumber);
            const auto program = patch.progNumber;

			m_singles[bank][program] = patch;

            if(onRomPatchReceived)
				onRomPatchReceived(patch.bankNumber, program);
		}
	}

	void Controller::parseMulti(const pluginLib::SysEx& _msg, const pluginLib::MidiPacket::Data& _data, const pluginLib::MidiPacket::ParamValues& _parameterValues)
    {
        const auto bankNumber = _data.find(pluginLib::MidiDataType::Bank)->second;

		/* If it's a multi edit buffer, set the part page C parameters to their multi equivalents */
		if (bankNumber == 0)
        {
	        m_multiEditBuffer.progNumber = _data.find(pluginLib::MidiDataType::Program)->second;
	        m_multiEditBuffer.name = getMultiPresetName(_parameterValues);
	        m_multiEditBuffer.data = _msg;

			for (const auto & paramValue : _parameterValues)
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

                param->setValueFromSynth(value, pluginLib::Parameter::Origin::PresetChange);
			}

			getProcessor().updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withProgramChanged(true));
		}
    }

	bool Controller::parseControllerDump(const synthLib::SMidiEvent& m)
	{
		const uint8_t status = m.a & 0xf0;
    	const uint8_t part = m.a & 0x0f;

		uint8_t page;

		if (status == synthLib::M_CONTROLCHANGE)
			page = virusLib::PAGE_A;
		else if (status == synthLib::M_POLYPRESSURE)
		{
			// device decides if PP is enabled and will echo any parameter change to us. Reject any other source
			if(m.source != synthLib::MidiEventSource::Plugin)
				return false;
			page = virusLib::PAGE_B;
		}
		else
			return false;

		const auto& params = findSynthParam(part, page, m.b);
		for (const auto & p : params)
			p->setValueFromSynth(m.c, pluginLib::Parameter::Origin::Midi);

		return true;
	}

    void Controller::printMessage(const pluginLib::SysEx &msg)
    {
		std::stringstream ss;
        ss << "[size " << msg.size() << "] ";
        for(size_t i=0; i<msg.size(); ++i)
        {
            ss << HEXN(static_cast<int>(msg[i]), 2);
            if(i < msg.size()-1)
                ss << ',';
        }
        const auto s(ss.str());
		LOG(s);
    }

    void Controller::onStateLoaded()
    {
		requestTotal();
		requestArrangement();
	}

    uint8_t Controller::getPartCount() const
    {
	    return m_processor.getModel() == virusLib::DeviceModel::Snow ? 4 : 16;
    }

    bool Controller::requestProgram(uint8_t _bank, uint8_t _program, bool _multi) const
    {
        std::map<pluginLib::MidiDataType, uint8_t> data;

        data.insert(std::make_pair(pluginLib::MidiDataType::Bank, _bank));
        data.insert(std::make_pair(pluginLib::MidiDataType::Program, _program));

		return sendSysEx(_multi ? MidiPacketType::RequestMulti : MidiPacketType::RequestSingle, data);
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

        return sendSysEx(MidiPacketType::RequestSingleBank, data);
    }

    bool Controller::requestTotal() const
    {
        return sendSysEx(MidiPacketType::RequestTotal);
    }

    bool Controller::requestArrangement() const
    {
        return sendSysEx(MidiPacketType::RequestArrangement);
    }

    void Controller::requestRomBanks()
    {
		switch(m_processor.getModel())
		{
        default:
        case virusLib::DeviceModel::A:
        case virusLib::DeviceModel::B:
        case virusLib::DeviceModel::C:
			m_singles.resize(8);
			break;
        case virusLib::DeviceModel::Snow:
        case virusLib::DeviceModel::TI:
        case virusLib::DeviceModel::TI2:
        	m_singles.resize(
                virusLib::ROMFile::getRomBankCount(virusLib::DeviceModel::TI) +
                virusLib::ROMFile::getRomBankCount(virusLib::DeviceModel::TI2) +
                virusLib::ROMFile::getRomBankCount(virusLib::DeviceModel::Snow) +
                2
            );
			break;
        }

    	for(uint8_t i=3; i<=getBankCount(); ++i)
			requestSingleBank(i);
    }

    bool Controller::sendSysEx(MidiPacketType _type) const
    {
	    std::map<pluginLib::MidiDataType, uint8_t> params;
        return sendSysEx(_type, params);
    }

    bool Controller::sendSysEx(MidiPacketType _type, std::map<pluginLib::MidiDataType, uint8_t>& _params) const
    {
        _params.insert(std::make_pair(pluginLib::MidiDataType::DeviceId, m_deviceId));
        return pluginLib::Controller::sendSysEx(midiPacketName(_type), _params);
    }

    void Controller::sendParameterChange(const pluginLib::Parameter& _parameter, const pluginLib::ParamValue _value)
    {
        const auto& desc = _parameter.getDescription();

        sendParameterChange(desc.page, _parameter.getPart(), desc.index, static_cast<uint8_t>(_value));
    }

    bool Controller::sendParameterChange(uint8_t _page, uint8_t _part, uint8_t _index, uint8_t _value) const
    {
        std::map<pluginLib::MidiDataType, uint8_t> data;

        data.insert(std::make_pair(pluginLib::MidiDataType::Page, _page));
        data.insert(std::make_pair(pluginLib::MidiDataType::Part, _part));
        data.insert(std::make_pair(pluginLib::MidiDataType::ParameterIndex, _index));
        data.insert(std::make_pair(pluginLib::MidiDataType::ParameterValue, _value));

    	return sendSysEx(MidiPacketType::ParameterChange, data);
    }

    std::vector<uint8_t> Controller::createSingleDump(uint8_t _part, uint8_t _bank, uint8_t _program)
    {
	    pluginLib::MidiPacket::Data data;

        data.insert(std::make_pair(pluginLib::MidiDataType::DeviceId, m_deviceId));
        data.insert(std::make_pair(pluginLib::MidiDataType::Bank, _bank));
        data.insert(std::make_pair(pluginLib::MidiDataType::Program, _program));

        std::vector<uint8_t> dst;

    	if(!createMidiDataFromPacket(dst, midiPacketName(MidiPacketType::SingleDump), data, _part))
            return {};

        return dst;
    }

    std::vector<uint8_t> Controller::createSingleDump(MidiPacketType _packet, uint8_t _bank, uint8_t _program, const pluginLib::MidiPacket::AnyPartParamValues& _paramValues)
    {
        const auto* m = getMidiPacket(midiPacketName(_packet));
		assert(m && "midi packet not found");

    	if(!m)
			return {};

		pluginLib::MidiPacket::Data data;
		pluginLib::MidiPacket::NamedParamValues paramValues;

        data.insert(std::make_pair(pluginLib::MidiDataType::DeviceId, m_deviceId));
        data.insert(std::make_pair(pluginLib::MidiDataType::Bank, _bank));
        data.insert(std::make_pair(pluginLib::MidiDataType::Program, _program));

        if(!createNamedParamValues(paramValues, _paramValues))
            return {};

        pluginLib::MidiPacket::Sysex dst;
        if(!m->create(dst, data, paramValues))
            return {};
        return dst;
    }

    std::vector<uint8_t> Controller::modifySingleDump(const std::vector<uint8_t>& _sysex, const virusLib::BankNumber _newBank, const uint8_t _newProgram) const
    {
        auto* m = getMidiPacket(midiPacketName(MidiPacketType::SingleDump));
        assert(m);

        const auto idxBank = m->getByteIndexForType(pluginLib::MidiDataType::Bank);
        const auto idxProgram = m->getByteIndexForType(pluginLib::MidiDataType::Program);

        assert(idxBank != pluginLib::MidiPacket::InvalidIndex);
        assert(idxProgram != pluginLib::MidiPacket::InvalidIndex);

        auto data = _sysex;

        data[idxBank] = toMidiByte(_newBank);
        data[idxProgram] = _newProgram;

        return data;
    }

    void Controller::selectPrevPreset(const uint8_t _part)
    {
		if(getCurrentPartProgram(_part) > 0)
		{
            setCurrentPartPreset(_part, getCurrentPartBank(_part), getCurrentPartProgram(_part) - 1);
		}
    }

    void Controller::selectNextPreset(uint8_t _part)
    {
		if(getCurrentPartProgram(_part) < m_singles[0].size())
		{
            setCurrentPartPreset(_part, getCurrentPartBank(_part), getCurrentPartProgram(_part) + 1);
		}
    }

    std::string Controller::getBankName(uint32_t _index) const
    {
        char temp[32]{0};

        if(getBankCount() <= 26)
        {
	        snprintf(temp, sizeof(temp), "Bank %c", 'A' + _index);
        }
        else if(_index < 2)
        {
	        snprintf(temp, sizeof(temp), "RAM Bank %c", 'A' + _index);
        }
        else
        {
            _index -= 2;

            const auto countSnow    = virusLib::ROMFile::getRomBankCount(virusLib::DeviceModel::Snow);
            const auto countTI      = virusLib::ROMFile::getRomBankCount(virusLib::DeviceModel::TI);
            const auto countTI2     = virusLib::ROMFile::getRomBankCount(virusLib::DeviceModel::TI2);

            switch(m_processor.getModel())
            {
            case virusLib::DeviceModel::Snow: 
                if(_index < countSnow)                  sprintf(temp, "Snow Rom %c", 'A' + _index);
                else if(_index < countTI + countSnow)	sprintf(temp, "TI Rom %c", 'A' + (_index - countSnow));
                else			                 		sprintf(temp, "TI2 Rom %c", 'A' + (_index - countTI - countSnow));
                break;
            case virusLib::DeviceModel::TI:
                if(_index < countTI)	                sprintf(temp, "TI Rom %c", 'A' + _index);
                else if(_index < countTI + countTI2)	sprintf(temp, "TI2 Rom %c", 'A' + (_index - countTI));
                else			    		            sprintf(temp, "Snow Rom %c", 'A' + (_index - countTI - countTI2));
                break;
            case virusLib::DeviceModel::TI2: 
                if(_index < countTI2)	                sprintf(temp, "TI2 Rom %c", 'A' + _index);
                else if(_index < countTI2 + countTI)	sprintf(temp, "TI Rom %c", 'A' + (_index - countTI2));
                else			    	            	sprintf(temp, "Snow Rom %c", 'A' + (_index - countTI2 - countTI));
                break;
            default:
                assert(false);
                break;
            }
        }
        return temp;
    }

    bool Controller::activatePatch(const std::vector<unsigned char>& _sysex)
    {
		return activatePatch(_sysex, isMultiMode() ? getCurrentPart() : static_cast<uint8_t>(virusLib::ProgramType::SINGLE));
    }

    bool Controller::activatePatch(const std::vector<unsigned char>& _sysex, uint32_t _part)
    {
        if(_part == virusLib::ProgramType::SINGLE)
        {
            if(isMultiMode())
	            _part = 0;
        }
        else if(_part >= 16)
        {
            return false;
        }
        else if(!isMultiMode() && _part == 0)
        {
            _part = virusLib::ProgramType::SINGLE;
        }

        const auto program = static_cast<uint8_t>(_part);

		// re-pack, force to edit buffer
    	const auto msg = modifySingleDump(_sysex, virusLib::BankNumber::EditBuffer, program);

		if(msg.empty())
			return false;

		sendSysEx(msg);

        // if we have locked parameters, get them, send the preset and then send each locked parameter value afterward.
        // Modifying the preset directly does not work because a preset might be an old version that we do not know
		sendLockedParameters(static_cast<uint8_t>(_part == virusLib::SINGLE ? 0 : _part));

		requestSingle(toMidiByte(virusLib::BankNumber::EditBuffer), program);

		setCurrentPartPresetSource(program == virusLib::ProgramType::SINGLE ? 0 : program, PresetSource::Browser);

		return true;
    }
}; // namespace Virus

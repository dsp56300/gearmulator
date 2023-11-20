#include "VirusController.h"

#include <fstream>

#include "ParameterNames.h"
#include "PluginProcessor.h"

#include "../virusLib/microcontrollerTypes.h"
#include "../synthLib/os.h"

#include "ui3/VirusEditor.h"

using MessageType = virusLib::SysexMessageType;

namespace Virus
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
	    "multidump"
    };

    static_assert(std::size(g_midiPacketNames) == static_cast<size_t>(Controller::MidiPacketType::Count));

    const char* midiPacketName(Controller::MidiPacketType _type)
    {
	    return g_midiPacketNames[static_cast<uint32_t>(_type)];
    }

    Controller::Controller(AudioPluginAudioProcessor &p, unsigned char deviceId) : pluginLib::Controller(p, loadParameterDescriptions()), m_processor(p), m_deviceId(deviceId)
    {
        switch(p.getModel())
        {
        default:
        case virusLib::ROMFile::Model::ABC:  m_singles.resize(8);  break;
        case virusLib::ROMFile::Model::Snow: m_singles.resize(10); break;
        case virusLib::ROMFile::Model::TI:   m_singles.resize(26); break;
        }

    	registerParams(p);

		// add lambda to enforce updating patches when virus switch from/to multi/single.
        const auto paramIdx = getParameterIndexByName(g_paramPlayMode);
		auto* parameter = getParameter(paramIdx);
        if(parameter)
		{
			parameter->onValueChanged.emplace_back(std::make_pair(0, [this] {
				const uint8_t prg = isMultiMode() ? 0x0 : virusLib::SINGLE;
				requestSingle(0, prg);
                requestMulti(0, prg);

				if (onMsgDone)
				{
					onMsgDone();
				}
			}));
		}
		requestTotal();
		requestArrangement();

    	for(uint8_t i=3; i<=getBankCount(); ++i)
			requestSingleBank(i);

    	startTimer(5);
	}

    Controller::~Controller()
    {
	    stopTimer();
    }

	void Controller::parseSysexMessage(const pluginLib::SysEx& _msg)
	{
        std::string name;
    	pluginLib::MidiPacket::Data data;
        pluginLib::MidiPacket::ParamValues parameterValues;

        if(parseMidiPacket(name,  data, parameterValues, _msg))
        {
            const auto deviceId = data[pluginLib::MidiDataType::DeviceId];

            if(deviceId != m_deviceId && deviceId != virusLib::OMNI_DEVICE_ID)
                return; // not intended to this device!

            if(name == midiPacketName(MidiPacketType::SingleDump))
                parseSingle(_msg, data, parameterValues);
            else if(name == midiPacketName(MidiPacketType::MultiDump))
                parseMulti(_msg, data, parameterValues);
            else if(name == midiPacketName(MidiPacketType::ParameterChange))
                parseParamChange(data);
            else
            {
		        LOG("Controller: Begin unhandled SysEx! --");
		        printMessage(_msg);
		        LOG("Controller: End unhandled SysEx! --");
            }
			return;
        }

        LOG("Controller: Begin unknown SysEx! --");
        printMessage(_msg);
        LOG("Controller: End unknown SysEx! --");
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

    void Controller::parseParamChange(const pluginLib::MidiPacket::Data& _data)
    {
    	const auto page  = _data.find(pluginLib::MidiDataType::Page)->second;
		const auto part  = _data.find(pluginLib::MidiDataType::Part)->second;
		const auto index = _data.find(pluginLib::MidiDataType::ParameterIndex)->second;
		const auto value = _data.find(pluginLib::MidiDataType::ParameterValue)->second;

        const auto& partParams = findSynthParam(part, page, index);

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
				param->setValueFromSynth(value, true, pluginLib::Parameter::ChangedBy::ControlChange);
		}
		for (const auto& param : partParams)
			param->setValueFromSynth(value, true, pluginLib::Parameter::ChangedBy::ControlChange);
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

    void Controller::setSinglePresetName(std::vector<uint8_t>& _sysex, const std::string& _name) const
    {
        for (int i = 0; i < kNameLength; i++)
        {
            const std::string paramName = "SingleName" + std::to_string(i);
            const auto idx = getParameterIndexByName(paramName);
            if (idx == InvalidParameterIndex || idx >= _sysex.size())
                break;
            _sysex[idx] = (i < _name.size()) ? _name[i] : ' ';
        }
    }

    void Controller::setSinglePresetName(uint8_t _part, const juce::String& _name) const
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

	bool Controller::isMultiMode() const
	{
        const auto paramIdx = getParameterIndexByName(g_paramPlayMode);
		const auto& value = getParameter(paramIdx)->getValueObject();
		return value.getValue();
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

	bool Controller::parseSingle(pluginLib::MidiPacket::Data& _data, pluginLib::MidiPacket::ParamValues& _parameterValues, const pluginLib::SysEx& _msg) const
	{
        const auto packetName = midiPacketName(MidiPacketType::SingleDump);

    	auto* m = getMidiPacket(packetName);

    	if(!m)
            return false;

        if(_msg.size() > m->size())
        {
	        pluginLib::SysEx temp;
            temp.insert(temp.begin(), _msg.begin(), _msg.begin() + (m->size()-1));
            temp.push_back(0xf7);
	    	return parseMidiPacket(*m, _data, _parameterValues, temp);
        }

    	return parseMidiPacket(*m, _data, _parameterValues, _msg);
    }

	std::string Controller::loadParameterDescriptions()
	{
        const auto name = "parameterDescriptions_C.json";
        const auto path = synthLib::getModulePath() +  name;

        const std::ifstream f(path.c_str(), std::ios::in);
        if(f.is_open())
        {
			std::stringstream buf;
			buf << f.rdbuf();
            return buf.str();
        }

        uint32_t size;
        const auto res = genericVirusUI::VirusEditor::findEmbeddedResource(name, size);
        if(res)
            return {res, size};
        return {};
	}

	void Controller::parseSingle(const pluginLib::SysEx& msg)
	{
		pluginLib::MidiPacket::Data data;
        pluginLib::MidiPacket::ParamValues parameterValues;

    	if(!parseSingle(data, parameterValues, msg))
            return;

        parseSingle(msg, data, parameterValues);
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

            for(auto it = _parameterValues.begin(); it != _parameterValues.end(); ++it)
            {
	            auto* p = getParameter(it->first.second, ch);
				p->setValueFromSynth(it->second, true, pluginLib::Parameter::ChangedBy::PresetChange);

	            for (const auto& derivedParam : p->getDerivedParameters())
		            derivedParam->setValueFromSynth(it->second, true, pluginLib::Parameter::ChangedBy::PresetChange);
            }

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
				onProgramChange();
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

                param->setValueFromSynth(value, true, pluginLib::Parameter::ChangedBy::PresetChange);
			}
		}
    }

	void Controller::parseControllerDump(const synthLib::SMidiEvent& m)
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

		const auto& params = findSynthParam(part, page, m.b);
		for (const auto & p : params)
			p->setValueFromSynth(m.c, true, pluginLib::Parameter::ChangedBy::ControlChange);
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

    void Controller::timerCallback()
    {
        std::vector<synthLib::SMidiEvent> virusOut;
        getPluginMidiOut(virusOut);

    	for (const auto& msg : virusOut)
        {
            if (msg.sysex.empty())
            {
                // no sysex
				parseControllerDump(msg);
			}
            else
			{
				parseSysexMessage(msg.sysex);               
			}
        }
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

    std::vector<uint8_t> Controller::createSingleDump(uint8_t _bank, uint8_t _program, const pluginLib::MidiPacket::ParamValues& _paramValues)
    {
        const auto* m = getMidiPacket(midiPacketName(MidiPacketType::SingleDump));
		assert(m && "midi packet not found");

    	if(!m)
			return {};

		pluginLib::MidiPacket::Data data;
		pluginLib::MidiPacket::NamedParamValues paramValues;

        data.insert(std::make_pair(pluginLib::MidiDataType::DeviceId, m_deviceId));
        data.insert(std::make_pair(pluginLib::MidiDataType::Bank, _bank));
        data.insert(std::make_pair(pluginLib::MidiDataType::Program, _program));

        for (const auto& it : _paramValues)
        {
            const auto* p = getParameter(it.first.second, _program == virusLib::SINGLE ? 0 : _program);
            assert(p);
            if(!p)
                return {};
            const auto key = std::make_pair(it.first.first, p->getDescription().name);
            paramValues.insert(std::make_pair(key, it.second));
        }

        pluginLib::MidiPacket::Sysex dst;
        if(!m->create(dst, data, paramValues))
            return {};
        return dst;
    }

    std::vector<uint8_t> Controller::modifySingleDump(const std::vector<uint8_t>& _sysex, const virusLib::BankNumber _newBank, const uint8_t _newProgram, const bool _modifyBank, const bool _modifyProgram)
    {
		pluginLib::MidiPacket::Data data;
		pluginLib::MidiPacket::ParamValues parameterValues;

		if(!parseSingle(data, parameterValues, _sysex))
			return {};

		return createSingleDump(_modifyBank ? toMidiByte(_newBank) : data[pluginLib::MidiDataType::Bank], _modifyProgram ? _newProgram : data[pluginLib::MidiDataType::Program], parameterValues);
    }

    void Controller::selectPrevPreset(uint8_t _part)
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
        sprintf(temp, "Bank %c", 'A' + _index);
        return temp;
    }
}; // namespace Virus

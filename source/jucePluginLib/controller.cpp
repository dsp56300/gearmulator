#include "controller.h"

#include <cassert>
#include <fstream>

#include "parameter.h"
#include "processor.h"

#include "dsp56kEmu/logging.h"

#include "juce_gui_basics/juce_gui_basics.h"	// juce::NativeMessageBox

#include "synthLib/os.h"

namespace pluginLib
{
	uint8_t getParameterValue(const Parameter* _p)
	{
		return static_cast<uint8_t>(_p->getUnnormalizedValue());
	}

	Controller::Controller(Processor& _processor, const std::string& _parameterDescJsonFilename)
		: m_processor(_processor)
		, m_descriptions(loadParameterDescriptions(_parameterDescJsonFilename))
		, m_locking(*this)
		, m_parameterLinks(*this)
	{
		if(!m_descriptions.isValid())
		{
			juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, 
				_processor.getProperties().name + " - Failed to parse Parameter Descriptions json", 
				"Encountered errors while parsing parameter descriptions:\n\n" + m_descriptions.getErrors(), 
				nullptr, juce::ModalCallbackFunction::create([](int){}));
		}
	}

	Controller::~Controller()
	{
		stopTimer();
		m_softKnobs.clear();
	}

	void Controller::registerParams(juce::AudioProcessor& _processor, Parameter::PartFormatter _partFormatter/* = nullptr*/)
    {
		auto globalParams = std::make_unique<juce::AudioProcessorParameterGroup>("global", "Global", "|");

		if(!_partFormatter)
		{
			_partFormatter = [](const uint8_t& _part, bool)
			{
				return juce::String("Ch ") + juce::String(_part + 1);
			};
		}
		std::map<ParamIndex, int> knownParameterIndices;

    	for (uint8_t part = 0; part < getPartCount(); part++)
		{
			m_paramsByParamType[part].reserve(m_descriptions.getDescriptions().size());

    		const auto partNumber = juce::String(part + 1);
			auto group = std::make_unique<juce::AudioProcessorParameterGroup>("ch" + partNumber, _partFormatter(part, false), "|");

			for (const auto& desc : m_descriptions.getDescriptions())
			{
				const ParamIndex idx = {static_cast<uint8_t>(desc.page), part, desc.index};

				int uid = 0;

				auto itKnownParamIdx = knownParameterIndices.find(idx);

				if(itKnownParamIdx == knownParameterIndices.end())
					knownParameterIndices.insert(std::make_pair(idx, 0));
				else
					uid = ++itKnownParamIdx->second;

				std::unique_ptr<Parameter> p;
				p.reset(createParameter(*this, desc, part, uid, _partFormatter));

				if(uid > 0)
				{
					const auto& existingParams = findSynthParam(idx);

					for (auto& existingParam : existingParams)
					{
						if(isDerivedParameter(*existingParam, *p))
							existingParam->addDerivedParameter(p.get());
					}
				}

				const bool isNonPartExclusive = desc.isNonPartSensitive();

				if (isNonPartExclusive && part != 0)
				{
					// only register on first part!
					m_paramsByParamType[part].push_back(m_paramsByParamType[0][m_paramsByParamType[part].size()]);
					continue;
				}

				m_paramsByParamType[part].push_back(p.get());

				if (p->getDescription().isPublic)
				{
					// lifecycle managed by Juce

					auto itExisting = m_synthParams.find(idx);
					if (itExisting != m_synthParams.end())
					{
						itExisting->second.push_back(p.get());
					}
					else
					{
						ParameterList params;
						params.emplace_back(p.get());
						m_synthParams.insert(std::make_pair(idx, std::move(params)));
					}

					if (isNonPartExclusive)
					{
						jassert(part == 0);
						globalParams->addChild(std::move(p));
					}
					else
						group->addChild(std::move(p));
				}
				else
				{
					// lifecycle handled by us

					auto itExisting = m_synthInternalParams.find(idx);
					if (itExisting != m_synthInternalParams.end())
					{
						itExisting->second.push_back(p.get());
					}
					else
					{
						ParameterList params;
						params.emplace_back(p.get());
						m_synthInternalParams.insert(std::make_pair(idx, std::move(params)));
					}
					m_synthInternalParamList.emplace_back(std::move(p));
				}
			}
			_processor.addParameterGroup(std::move(group));
		}

		_processor.addParameterGroup(std::move(globalParams));

		// initialize all soft knobs for all parts
		std::vector<size_t> softKnobs;

		for (size_t i=0; i<m_descriptions.getDescriptions().size(); ++i)
		{
			const auto& desc = m_descriptions.getDescriptions()[i];
			if(!desc.isSoftKnob())
				continue;
			softKnobs.push_back(i);
		}

		for(size_t part = 0; part<getPartCount(); ++part)
		{
			for (const auto& softKnobParam : softKnobs)
			{
				auto* sk = new SoftKnob(*this, static_cast<uint8_t>(part), static_cast<uint32_t>(softKnobParam));
				m_softKnobs.insert({sk->getParameter(), std::unique_ptr<SoftKnob>(sk)});
			}
		}
    }

	void Controller::sendSysEx(const pluginLib::SysEx& _msg) const
    {
        synthLib::SMidiEvent ev(synthLib::MidiEventSource::Editor);
        ev.sysex = _msg;
		sendMidiEvent(ev);
    }

	void Controller::sendMidiEvent(const synthLib::SMidiEvent& _ev) const
    {
        m_processor.addMidiEvent(_ev);
    }

	void Controller::sendMidiEvent(const uint8_t _a, const uint8_t _b, const uint8_t _c, const uint32_t _offset/* = 0*/, const synthLib::MidiEventSource _source/* = synthLib::MidiEventSource::Editor*/) const
	{
        m_processor.addMidiEvent(synthLib::SMidiEvent(_source, _a, _b, _c, _offset));
	}

	bool Controller::combineParameterChange(uint8_t& _result, const std::string& _midiPacket, const Parameter& _parameter, ParamValue _value) const
	{
		const auto &desc = _parameter.getDescription();

		std::map<MidiDataType, uint8_t> data;

		const auto *packet = getMidiPacket(_midiPacket);

		if (!packet)
		{
			LOG("Failed to find midi packet " << _midiPacket);
			return false;
		}

		const ParamIndex idx = {static_cast<uint8_t>(desc.page), _parameter.getPart(), desc.index};

		const auto params = findSynthParam(idx);

		uint32_t byte = MidiPacket::InvalidIndex;

		for (auto param : params)
		{
			byte = packet->getByteIndexForParameterName(param->getDescription().name);
			if (byte != MidiPacket::InvalidIndex)
				break;
		}

		if (byte == MidiPacket::InvalidIndex)
		{
			LOG("Failed to find byte index for parameter " << desc.name);
			return false;
		}

		std::vector<const MidiPacket::MidiDataDefinition*> definitions;

		if(!packet->getDefinitionsForByteIndex(definitions, byte))
			return false;

		if (definitions.size() == 1)
		{
			_result = static_cast<uint8_t>(_value);
			return true;
		}

		_result = 0;

	    for (const auto& it : definitions)
	    {
			uint32_t i = 0;

	    	if(!m_descriptions.getIndexByName(i, it->paramName))
			{
				LOG("Failed to find index for parameter " << it->paramName);
				return false;
			}

			auto* p = getParameter(i, _parameter.getPart());
			const auto v = p == &_parameter ? _value : getParameterValue(p);
			_result |= it->packValue(v);
	    }

		return true;
	}

	void Controller::applyPatchParameters(const MidiPacket::ParamValues& _params, const uint8_t _part) const
	{
		for (const auto& it : _params)
		{
			auto* p = getParameter(it.first.second, _part);
			p->setValueFromSynth(it.second, pluginLib::Parameter::Origin::PresetChange);

			for (const auto& derivedParam : p->getDerivedParameters())
				derivedParam->setValueFromSynth(it.second, pluginLib::Parameter::Origin::PresetChange);
		}

		getProcessor().updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withProgramChanged(true));
	}

	void Controller::timerCallback()
	{
		processMidiMessages();
	}

	bool Controller::sendSysEx(const std::string& _packetName) const
    {
        return sendSysEx(_packetName, {});
    }

    bool Controller::sendSysEx(const std::string& _packetName, const std::map<MidiDataType, uint8_t>& _params) const
    {
	    std::vector<uint8_t> sysex;

    	if(!createMidiDataFromPacket(sysex, _packetName, _params, 0))
            return false;

        sendSysEx(sysex);
        return true;
    }

	const Controller::ParameterList& Controller::findSynthParam(const uint8_t _part, const uint8_t _page, const uint8_t _paramIndex) const
	{
		const ParamIndex paramIndex{ _page, _part, _paramIndex };

		return findSynthParam(paramIndex);
	}

	const Controller::ParameterList& Controller::findSynthParam(const ParamIndex& _paramIndex) const
    {
		const auto it = m_synthParams.find(_paramIndex);

		if (it != m_synthParams.end())
			return it->second;

    	const auto iti = m_synthInternalParams.find(_paramIndex);

		if (iti == m_synthInternalParams.end())
		{
			static ParameterList empty;
			return empty;
		}

		return iti->second;
    }

	void Controller::sendLockedParameters(const uint8_t _part)
	{
        const auto lockedParameters = m_locking.getLockedParameters(_part);

        for (const auto& p : lockedParameters)
        {
	        const auto v = p->getUnnormalizedValue();
	        sendParameterChange(*p, static_cast<uint8_t>(v));
        }
	}

    juce::Value* Controller::getParamValueObject(const uint32_t _index, const uint8_t _part) const
    {
	    const auto res = getParameter(_index, _part);
		return res ? &res->getValueObject() : nullptr;
    }

    Parameter* Controller::getParameter(const uint32_t _index) const
    {
		return getParameter(_index, 0);
	}

	Parameter* Controller::getParameter(const uint32_t _index, const uint8_t _part) const
	{
		if (_part >= m_paramsByParamType.size())
			return nullptr;

		if (_index >= m_paramsByParamType[_part].size())
			return nullptr;

		return m_paramsByParamType[_part][_index];
	}

	Parameter* Controller::getParameter(const std::string& _name, const uint8_t _part) const
	{
		const auto idx = getParameterIndexByName(_name);
		if(idx == InvalidParameterIndex)
			return nullptr;
		return getParameter(idx, _part);
	}

	uint32_t Controller::getParameterIndexByName(const std::string& _name) const
	{
		uint32_t index;
		return m_descriptions.getIndexByName(index, _name) ? index : InvalidParameterIndex;
	}

	bool Controller::setParameters(const std::map<std::string, ParamValue>& _values, const uint8_t _part, const Parameter::Origin _changedBy) const
	{
		bool res = false;

		for (const auto& it : _values)
		{
			const auto& name = it.first;
			const auto& value = it.second;

			if(auto* param = getParameter(name, _part))
			{
				res = true;
				param->setUnnormalizedValueNotifyingHost(value, _changedBy);
			}
		}

		return res;
	}

	const MidiPacket* Controller::getMidiPacket(const std::string& _name) const
	{
		return m_descriptions.getMidiPacket(_name);
	}

	bool Controller::createNamedParamValues(MidiPacket::NamedParamValues& _params, const std::string& _packetName, const uint8_t _part) const
	{
        const auto* m = getMidiPacket(_packetName);
		assert(m && "midi packet not found");
        if(!m)
            return false;

        MidiPacket::ParamIndices indices;
		m->getParameterIndices(indices, m_descriptions);

		if(indices.empty())
			return true;

		for (const auto& index : indices)
        {
			auto* p = getParameter(index.second, _part);
			if(!p)
				return false;
			const auto* largestP = p;
			// we might have more than 1 parameter per index, use the one with the largest range
			const auto& derived = p->getDerivedParameters();
			for (const auto& parameter : derived)
	        {
				if(parameter->getDescription().range.getLength() > p->getDescription().range.getLength())
					largestP = parameter;
	        }
	        const auto v = getParameterValue(largestP);
	        _params.insert(std::make_pair(std::make_pair(index.first, p->getDescription().name), v));
        }

		return true;
	}

	bool Controller::createNamedParamValues(MidiPacket::NamedParamValues& _dest, const MidiPacket::AnyPartParamValues& _source) const
	{
        for(uint32_t i=0; i<_source.size(); ++i)
        {
            const auto& v = _source[i];
            if(!v)
                continue;
            const auto* p = getParameter(i);
            assert(p);
            if(!p)
                return false;
            const auto key = std::make_pair(MidiPacket::AnyPart, p->getDescription().name);
            _dest.insert(std::make_pair(key, *v));
        }
		return true;
	}

	bool Controller::createMidiDataFromPacket(std::vector<uint8_t>& _sysex, const std::string& _packetName, const std::map<MidiDataType, uint8_t>& _data, uint8_t _part) const
	{
		MidiPacket::NamedParamValues paramValues;

		if(!createNamedParamValues(paramValues, _packetName, _part))
			return false;

		return createMidiDataFromPacket(_sysex, _packetName, _data, paramValues);
	}

	bool Controller::createMidiDataFromPacket(std::vector<uint8_t>& _sysex, const std::string& _packetName, const std::map<MidiDataType, uint8_t>& _data, const MidiPacket::NamedParamValues& _values) const
	{
        const auto* m = getMidiPacket(_packetName);

		if(!m->create(_sysex, _data, _values))
        {
	        assert(false && "failed to create midi packet");
	        _sysex.clear();
	        return false;
        }
        return true;
	}

	bool Controller::createMidiDataFromPacket(std::vector<uint8_t>& _sysex, const std::string& _packetName, const std::map<MidiDataType, uint8_t>& _data, const MidiPacket::AnyPartParamValues& _values) const
	{
		MidiPacket::NamedParamValues namedParams;
		if(!createNamedParamValues(namedParams, _values))
			return false;
		return createMidiDataFromPacket(_sysex, _packetName, _data, namedParams);
	}

	bool Controller::parseMidiPacket(const MidiPacket& _packet, MidiPacket::Data& _data, MidiPacket::ParamValues& _parameterValues, const std::vector<uint8_t>& _src) const
	{
		_data.clear();
		_parameterValues.clear();
		return _packet.parse(_data, _parameterValues, m_descriptions, _src);
	}

	bool Controller::parseMidiPacket(const MidiPacket& _packet, MidiPacket::Data& _data, MidiPacket::AnyPartParamValues& _parameterValues, const std::vector<uint8_t>& _src) const
	{
		_data.clear();
		_parameterValues.clear();
		return _packet.parse(_data, _parameterValues, m_descriptions, _src);
	}

	bool Controller::parseMidiPacket(const MidiPacket& _packet, MidiPacket::Data& _data, const std::function<void(MidiPacket::ParamIndex, ParamValue)>& _parameterValues, const std::vector<uint8_t>& _src) const
	{
		_data.clear();
		return _packet.parse(_data, _parameterValues, m_descriptions, _src);
	}

	bool Controller::parseMidiPacket(const std::string& _name, MidiPacket::Data& _data, MidiPacket::ParamValues& _parameterValues, const std::vector<uint8_t>& _src) const
	{
		auto* m = getMidiPacket(_name);
		assert(m);
		if(!m)
			return false;
		return parseMidiPacket(*m, _data, _parameterValues, _src);
	}

	bool Controller::parseMidiPacket(std::string& _name, MidiPacket::Data& _data, MidiPacket::ParamValues& _parameterValues, const std::vector<uint8_t>& _src) const
	{
		const auto& packets = m_descriptions.getMidiPackets();

		for (const auto& packet : packets)
		{
			if(!parseMidiPacket(packet.second, _data, _parameterValues, _src))
				continue;

			_name = packet.first;
			return true;
		}
		return false;
	}

	bool Controller::setCurrentPart(const uint8_t _part)
	{
		if(_part == m_currentPart)
			return false;
		m_currentPart = _part;
		onCurrentPartChanged(m_currentPart);
		return true;
	}

	bool Controller::parseMidiMessage(const synthLib::SMidiEvent& _e)
	{
		if(_e.sysex.empty())
			return parseControllerMessage(_e);
		return parseSysexMessage(_e.sysex, _e.source);
	}

	void Controller::enqueueMidiMessages(const std::vector<synthLib::SMidiEvent>& _events)
	{
		if(_events.empty())
			return;

        const std::lock_guard l(m_midiMessagesLock);
        m_midiMessages.insert(m_midiMessages.end(), _events.begin(), _events.end());
		if(!isTimerRunning())
			startTimer(1);
	}

	void Controller::loadChunkData(baseLib::ChunkReader& _cr)
	{
		m_parameterLinks.loadChunkData(_cr);
	}

	void Controller::saveChunkData(baseLib::BinaryStream& _s) const
	{
		m_parameterLinks.saveChunkData(_s);
	}

	Parameter::Origin Controller::midiEventSourceToParameterOrigin(const synthLib::MidiEventSource _source)
	{
		switch (_source)
		{
		case synthLib::MidiEventSource::Unknown:
			return Parameter::Origin::Unknown;
		case synthLib::MidiEventSource::Editor:
			return Parameter::Origin::Ui;
		case synthLib::MidiEventSource::Host:
			return Parameter::Origin::HostAutomation;
		case synthLib::MidiEventSource::PhysicalInput:
		case synthLib::MidiEventSource::Plugin:
			return Parameter::Origin::Midi;
		case synthLib::MidiEventSource::Internal:
			return Parameter::Origin::Unknown;
		default:
			assert(false && "implement new midi event source type");
			return Parameter::Origin::Unknown;
		}
	}

	void Controller::getMidiMessages(std::vector<synthLib::SMidiEvent>& _events)
	{
		const std::lock_guard l(m_midiMessagesLock);
        std::swap(m_midiMessages, _events);
		m_midiMessages.clear();
		stopTimer();
	}

	void Controller::processMidiMessages()
	{
	    std::vector<synthLib::SMidiEvent> events;
	    getMidiMessages(events);

	    for (const auto& e : events)
		    parseMidiMessage(e);
	}

	std::string Controller::loadParameterDescriptions(const std::string& _filename) const
	{
	    const auto path = synthLib::getModulePath() + _filename;

	    const std::ifstream f(path.c_str(), std::ios::in);
	    if(f.is_open())
	    {
			std::stringstream buf;
			buf << f.rdbuf();
	        return buf.str();
	    }

	    const auto res = m_processor.findResource(_filename);
	    if(res)
	        return {res->first, res->second};
	    return {};
	}

	std::set<std::string> Controller::getRegionIdsForParameter(const Parameter* _param) const
	{
		if(!_param)
			return {};
		return getRegionIdsForParameter(_param->getDescription().name);
	}

	std::set<std::string> Controller::getRegionIdsForParameter(const std::string& _name) const
	{
		const auto& regions = getParameterDescriptions().getRegions();

		std::set<std::string> result;
		for (const auto& region : regions)
		{
			if(region.second.containsParameter(_name))
				result.insert(region.first);
		}
		return result;
	}

	Parameter* Controller::createParameter(Controller& _controller, const Description& _desc, const uint8_t _part, const int _uid, const Parameter::PartFormatter& _partFormatter)
	{
		return new Parameter(_controller, _desc, _part, _uid, _partFormatter);
	}
}

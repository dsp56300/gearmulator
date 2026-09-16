#include "midiInputRouting.h"
#include <stdexcept>

namespace jucePlayer
{
    namespace
    {
        constexpr auto g_configKey = "midiInputGroups";
    }

    MidiInputRouting::MidiInputRouting(juce::AudioDeviceManager& _manager, juce::PropertiesFile& _config,
                                       Deliver _deliver, const uint8_t _groupCount) :
        m_allGroups(_groupCount >= 1 && _groupCount <= 8 ? static_cast<uint8_t>((1u << _groupCount) - 1) : 0),
        m_manager(_manager), m_config(_config), m_deliver(std::move(_deliver))
    {
        if (!m_allGroups)
            throw std::invalid_argument("MIDI routing needs 1..8 destination groups");
        if (const auto saved = m_config.getXmlValue(g_configKey))
        {
            for (const auto* input : saved->getChildWithTagNameIterator("INPUT"))
            {
                const auto groups = static_cast<uint8_t>(input->getIntAttribute("groups") & m_allGroups);
                if (groups)
                    m_routes[input->getStringAttribute("identifier")] = {input->getStringAttribute("name"), groups};
            }
        }
        // An input opened without a saved choice, from an older config or by --midi-in, plays group A
        // as every input used to.
        for (const auto& input : juce::MidiInput::getAvailableDevices())
            if (m_manager.isMidiInputDeviceEnabled(input.identifier) && !m_routes.count(input.identifier))
                m_routes[input.identifier] = {input.name, GroupA};
        m_manager.addMidiInputDeviceCallback({}, this);
    }

    MidiInputRouting::~MidiInputRouting() { m_manager.removeMidiInputDeviceCallback({}, this); }

    uint8_t MidiInputRouting::groups(const juce::String& _identifier) const
    {
        if (!m_manager.isMidiInputDeviceEnabled(_identifier))
            return 0;
        const std::lock_guard lock(m_mutex);
        const auto it = m_routes.find(_identifier);
        return it != m_routes.end() ? it->second.groups : GroupA;
    }

    void MidiInputRouting::setGroups(const juce::MidiDeviceInfo& _input, const uint8_t _groups)
    {
        const auto groups = static_cast<uint8_t>(_groups & m_allGroups);
        {
            const std::lock_guard lock(m_mutex);
            if (groups)
                m_routes[_input.identifier] = {_input.name, groups};
            else
                m_routes.erase(_input.identifier);
        }
        m_manager.setMidiInputDeviceEnabled(_input.identifier, groups != 0);
        persist();
    }

    void MidiInputRouting::handleIncomingMidiMessage(juce::MidiInput* _source, const juce::MidiMessage& _message)
    {
        if (!_source || !m_deliver)
            return;
        uint8_t groups = GroupA;
        {
            const std::lock_guard lock(m_mutex);
            if (const auto it = m_routes.find(_source->getIdentifier()); it != m_routes.end())
                groups = it->second.groups;
        }
        m_deliver(_message, groups);
    }

    void MidiInputRouting::persist()
    {
        juce::XmlElement saved("MIDIINPUTGROUPS");
        {
            const std::lock_guard lock(m_mutex);
            for (const auto& [identifier, route] : m_routes)
            {
                auto* input = saved.createNewChildElement("INPUT");
                input->setAttribute("identifier", identifier);
                input->setAttribute("name", route.name);
                input->setAttribute("groups", route.groups);
            }
        }
        m_config.setValue(g_configKey, &saved);
        m_config.saveIfNeeded();
    }
} // namespace jucePlayer

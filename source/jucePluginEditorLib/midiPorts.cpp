#include "midiPorts.h"

#include "pluginEditor.h"
#include "pluginProcessor.h"

#include "juceRmlUi/rmlElemComboBox.h"
#include "juceRmlUi/rmlEventListener.h"

#include "juceUiLib/editor.h"
#include "juceUiLib/messageBox.h"

namespace
{
	constexpr const char* const g_none = "<none>";

	void initComboBox(juceRmlUi::ElemComboBox* _combo, const juce::Array<juce::MidiDeviceInfo>& _entries, const juce::String& _selectedEntry)
	{
		int inIndex = 0;

		_combo->addOption(g_none);

		for (int i = 0; i < _entries.size(); i++)
		{
			const auto& input = _entries[i];

			if (input.identifier == _selectedEntry)
				inIndex = i + 1;

			_combo->addOption(input.name.toStdString());
		}

		_combo->setSelectedIndex(inIndex, false);
	}

	uint32_t createMenu(juceRmlUi::Menu& _menu, const juce::Array<juce::MidiDeviceInfo>& _devices, const juce::String& _current, const std::function<void(juce::String)>& _onSelect)
	{
		_menu.addEntry(g_none, true, _current.isEmpty(), [_onSelect]
		{
			_onSelect({});
		});

		for (const auto& device : _devices)
		{
			_menu.addEntry(device.name.toStdString(), true, device.identifier == _current, [id = device.identifier, _onSelect]
			{
				_onSelect(id);
			});
		}
		return _devices.size();
	}
}

namespace jucePluginEditorLib
{
	MidiPorts::MidiPorts(const Editor& _editor, Processor& _processor) : m_processor(_processor)
	{
		m_midiIn  = _editor.findChild<juceRmlUi::ElemComboBox>("MidiIn" , false);
		m_midiOut = _editor.findChild<juceRmlUi::ElemComboBox>("MidiOut", false);

		initInputComboBox(m_processor, m_midiIn);
		initOutputComboBox(m_processor, m_midiOut);
   	}

	void MidiPorts::createMidiInputMenu(juceRmlUi::Menu& _menu, pluginLib::MidiPorts& _ports)
	{
		createMenu(_menu, juce::MidiInput::getAvailableDevices(), _ports.getInputId(), [&_ports](const juce::String& _id)
		{
			if(!_ports.setMidiInput(_id))
				showMidiPortFailedMessage(_ports.getProcessor(), "Input");
		});
	}

	void MidiPorts::createMidiOutputMenu(juceRmlUi::Menu& _menu, pluginLib::MidiPorts& _ports)
	{
		createMenu(_menu, juce::MidiOutput::getAvailableDevices(), _ports.getOutputId(), [&_ports](const juce::String& _id)
		{
			if(!_ports.setMidiOutput(_id))
				showMidiPortFailedMessage(_ports.getProcessor(), "Output");
		});
	}

	void MidiPorts::createMidiPortsMenu(juceRmlUi::Menu& _menu, pluginLib::MidiPorts& _ports)
	{
		juceRmlUi::Menu inputs, outputs, ports;

		createMidiInputMenu(inputs, _ports);
		createMidiOutputMenu(outputs, _ports);

		ports.addSubMenu("Input", std::move(inputs));
		ports.addSubMenu("Output", std::move(outputs));

		_menu.addSubMenu("MIDI Ports", std::move(ports));
	}

	void MidiPorts::initInputComboBox(Processor& _processor, juceRmlUi::ElemComboBox* _combo)
	{
		if(!_combo)
			return;

		initComboBox(_combo, juce::MidiInput::getAvailableDevices(), getMidiPorts(_processor).getInputId());

		juceRmlUi::EventListener::Add(_combo, Rml::EventId::Change, [&_processor, _combo](Rml::Event&)
		{
			updateMidiInput(_processor, _combo, _combo->getSelectedIndex());
		});
	}

	void MidiPorts::initOutputComboBox(Processor& _processor, juceRmlUi::ElemComboBox* _combo)
	{
		if(!_combo)
			return;

		initComboBox(_combo, juce::MidiOutput::getAvailableDevices(), getMidiPorts(_processor).getOutputId());

		juceRmlUi::EventListener::Add(_combo, Rml::EventId::Change, [&_processor, _combo](Rml::Event&)
		{
			updateMidiOutput(_processor, _combo, _combo->getSelectedIndex());
		});
	}

	pluginLib::MidiPorts& MidiPorts::getMidiPorts() const
	{
		return getMidiPorts(m_processor);
	}

	pluginLib::MidiPorts& MidiPorts::getMidiPorts(Processor& _processor)
	{
		return _processor.getMidiPorts();
	}

	void MidiPorts::showMidiPortFailedMessage(const char* _name) const
	{
		showMidiPortFailedMessage(m_processor, _name);
	}

	void MidiPorts::showMidiPortFailedMessage(const pluginLib::Processor& _processor, const char* _name)
	{
		genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning, _processor.getProperties().name, 
			std::string("Failed to open Midi ") + _name + ".\n\n"
			"Make sure that the device is not already in use by another program.");
	}

	void MidiPorts::updateMidiInput(int _index) const
	{
		updateMidiInput(m_processor, m_midiIn, _index);
	}

	void MidiPorts::updateMidiInput(Processor& _processor, juceRmlUi::ElemComboBox* _combo, int _index)
	{
	    const auto list = juce::MidiInput::getAvailableDevices();

	    if (_index <= 0)
	    {
	        _combo->setSelectedIndex(_index, false);
			getMidiPorts(_processor).setMidiInput({});
	        return;
	    }

		_index--;

		const auto newInput = list[_index];

	    if (!getMidiPorts(_processor).setMidiInput(newInput.identifier))
	    {
			showMidiPortFailedMessage(_processor, "Input");
	        _combo->setSelectedIndex(0, false);
	        return;
	    }

	    _combo->setSelectedIndex(_index + 1, false);
	}

	void MidiPorts::updateMidiOutput(const int _index) const
	{
		updateMidiOutput(m_processor, m_midiOut, _index);
	}

	void MidiPorts::updateMidiOutput(Processor& _processor, juceRmlUi::ElemComboBox* _combo, int _index)
	{
	    const auto list = juce::MidiOutput::getAvailableDevices();

	    if (_index == 0)
	    {
	        _combo->setSelectedIndex(_index, false);
	        getMidiPorts(_processor).setMidiOutput({});
	        return;
	    }
	    _index--;
	    const auto newOutput = list[_index];
	    if (!getMidiPorts(_processor).setMidiOutput(newOutput.identifier))
	    {
			showMidiPortFailedMessage(_processor, "Output");
	        _combo->setSelectedIndex(0, false);
	        return;
	    }
	    
	    _combo->setSelectedIndex(_index + 1, false);
	}
}

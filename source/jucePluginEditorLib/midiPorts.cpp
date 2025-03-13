#include "midiPorts.h"

#include "pluginProcessor.h"

#include "juceUiLib/editor.h"
#include "juceUiLib/messageBox.h"

namespace
{
	constexpr const char* const g_none = "<none>";

	void initComboBox(juce::ComboBox* _combo, const juce::Array<juce::MidiDeviceInfo>& _entries, const juce::String& _selectedEntry)
	{
		int inIndex = 0;

		_combo->addItem(g_none, 1);

		for (int i = 0; i < _entries.size(); i++)
		{
			const auto& input = _entries[i];

			if (input.identifier == _selectedEntry)
				inIndex = i + 1;

			_combo->addItem(input.name, i+2);
		}

		_combo->setSelectedItemIndex(inIndex, juce::dontSendNotification);
	}

	uint32_t createMenu(juce::PopupMenu& _menu, const juce::Array<juce::MidiDeviceInfo>& _devices, const juce::String& _current, const std::function<void(juce::String)>& _onSelect)
	{
		_menu.addItem(g_none, true, _current.isEmpty(), [_onSelect]
		{
			_onSelect({});
		});

		for (const auto& device : _devices)
		{
			_menu.addItem(device.name, true, device.identifier == _current, [id = device.identifier, _onSelect]
			{
				_onSelect(id);
			});
		}
		return _devices.size();
	}
}

namespace jucePluginEditorLib
{
	MidiPorts::MidiPorts(const genericUI::Editor& _editor, Processor& _processor) : m_processor(_processor)
	{
		m_midiIn  = _editor.findComponentT<juce::ComboBox>("MidiIn" , false);
		m_midiOut = _editor.findComponentT<juce::ComboBox>("MidiOut", false);

		if(m_midiIn)
			initInputComboBox(getMidiPorts(), m_midiIn);

		if(m_midiOut)
			initOutputComboBox(getMidiPorts(), m_midiOut);
	}

	void MidiPorts::createMidiInputMenu(juce::PopupMenu& _menu, pluginLib::MidiPorts& _ports)
	{
		createMenu(_menu, juce::MidiInput::getAvailableDevices(), _ports.getInputId(), [&_ports](const juce::String& _id)
		{
			if(!_ports.setMidiInput(_id))
				showMidiPortFailedMessage(_ports.getProcessor(), "Input");
		});
	}

	void MidiPorts::createMidiOutputMenu(juce::PopupMenu& _menu, pluginLib::MidiPorts& _ports)
	{
		createMenu(_menu, juce::MidiOutput::getAvailableDevices(), _ports.getOutputId(), [&_ports](const juce::String& _id)
		{
			if(!_ports.setMidiOutput(_id))
				showMidiPortFailedMessage(_ports.getProcessor(), "Output");
		});
	}

	void MidiPorts::createMidiPortsMenu(juce::PopupMenu& _menu, pluginLib::MidiPorts& _ports)
	{
		juce::PopupMenu inputs, outputs, ports;

		createMidiInputMenu(inputs, _ports);
		createMidiOutputMenu(outputs, _ports);

		ports.addSubMenu("Input", inputs);
		ports.addSubMenu("Output", outputs);

		_menu.addSubMenu("MIDI Ports", ports);
	}

	void MidiPorts::initInputComboBox(pluginLib::MidiPorts& _ports, juce::ComboBox* _combo)
	{
		initComboBox(_combo, juce::MidiInput::getAvailableDevices(), _ports.getInputId());
		_combo->onChange = [&_ports, _combo]{ updateMidiInput(_ports, _combo, _combo->getSelectedItemIndex()); };
	}

	void MidiPorts::initOutputComboBox(pluginLib::MidiPorts& _ports, juce::ComboBox* _combo)
	{
		initComboBox(_combo, juce::MidiOutput::getAvailableDevices(), _ports.getOutputId());
		_combo->onChange = [&_ports, _combo]{ updateMidiOutput(_ports, _combo, _combo->getSelectedItemIndex()); };
	}

	void MidiPorts::updateMidiInput(pluginLib::MidiPorts& _ports, juce::ComboBox* _combo, int _index)
	{
	    const auto list = juce::MidiInput::getAvailableDevices();

	    if (_index <= 0)
	    {
	        _combo->setSelectedItemIndex(_index, juce::dontSendNotification);
	        return;
	    }

		_index--;

		const auto newInput = list[_index];

	    if (!_ports.setMidiInput(newInput.identifier))
	    {
			showMidiPortFailedMessage(_ports.getProcessor(), "Input");
	        _combo->setSelectedItemIndex(0, juce::dontSendNotification);
	        return;
	    }

	    _combo->setSelectedItemIndex(_index + 1, juce::dontSendNotification);
	}

	void MidiPorts::updateMidiOutput(pluginLib::MidiPorts& _ports, juce::ComboBox* _combo, int _index)
	{
	    const auto list = juce::MidiOutput::getAvailableDevices();

	    if (_index == 0)
	    {
	        _combo->setSelectedItemIndex(_index, juce::dontSendNotification);
	        _ports.setMidiOutput({});
	        return;
	    }

	    _index--;

		const auto newOutput = list[_index];
	    if (!_ports.setMidiOutput(newOutput.identifier))
	    {
			showMidiPortFailedMessage(_ports.getProcessor(), "Output");
	        _combo->setSelectedItemIndex(0, juce::dontSendNotification);
	        return;
	    }
	    
	    _combo->setSelectedItemIndex(_index + 1, juce::dontSendNotification);
	}

	pluginLib::MidiPorts& MidiPorts::getMidiPorts() const
	{
		return m_processor.getMidiPorts();
	}

	void MidiPorts::showMidiPortFailedMessage(const pluginLib::Processor& _processor, const char* _name)
	{
		genericUI::MessageBox::showOk(juce::MessageBoxIconType::WarningIcon, _processor.getProperties().name, 
			std::string("Failed to open Midi ") + _name + ".\n\n"
			"Make sure that the device is not already in use by another program.");
	}

	void MidiPorts::updateMidiInput(int _index) const
	{
		updateMidiInput(getMidiPorts(), m_midiIn, _index);
	}

	void MidiPorts::updateMidiOutput(int _index) const
	{
		updateMidiOutput(getMidiPorts(), m_midiOut, _index);
	}
}

#include "midiPorts.h"

#include "pluginProcessor.h"

#include "juceUiLib/editor.h"

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
		{
			initComboBox(m_midiIn, juce::MidiInput::getAvailableDevices(), getMidiPorts().getInputId());
			m_midiIn->onChange = [this]{ updateMidiInput(m_midiIn->getSelectedItemIndex()); };
		}

		if(m_midiOut)
		{
			initComboBox(m_midiOut, juce::MidiOutput::getAvailableDevices(), getMidiPorts().getOutputId());
			m_midiOut->onChange = [this]{ updateMidiOutput(m_midiOut->getSelectedItemIndex()); };
		}
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

	pluginLib::MidiPorts& MidiPorts::getMidiPorts() const
	{
		return m_processor.getMidiPorts();
	}

	void MidiPorts::showMidiPortFailedMessage(const char* _name) const
	{
		showMidiPortFailedMessage(m_processor, _name);
	}

	void MidiPorts::showMidiPortFailedMessage(const pluginLib::Processor& _processor, const char* _name)
	{
		juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, _processor.getProperties().name, 
			std::string("Failed to open Midi ") + _name + ".\n\n"
			"Make sure that the device is not already in use by another program.", nullptr, juce::ModalCallbackFunction::create([](int){}));
	}

	void MidiPorts::updateMidiInput(int _index) const
	{
	    const auto list = juce::MidiInput::getAvailableDevices();

	    if (_index <= 0)
	    {
	        m_midiIn->setSelectedItemIndex(_index, juce::dontSendNotification);
	        return;
	    }

		_index--;

		const auto newInput = list[_index];

	    if (!getMidiPorts().setMidiInput(newInput.identifier))
	    {
			showMidiPortFailedMessage("Input");
	        m_midiIn->setSelectedItemIndex(0, juce::dontSendNotification);
	        return;
	    }

	    m_midiIn->setSelectedItemIndex(_index + 1, juce::dontSendNotification);
	}

	void MidiPorts::updateMidiOutput(int _index) const
	{
	    const auto list = juce::MidiOutput::getAvailableDevices();

	    if (_index == 0)
	    {
	        m_midiOut->setSelectedItemIndex(_index, juce::dontSendNotification);
	        getMidiPorts().setMidiOutput({});
	        return;
	    }
	    _index--;
	    const auto newOutput = list[_index];
	    if (!getMidiPorts().setMidiOutput(newOutput.identifier))
	    {
			showMidiPortFailedMessage("Output");
	        m_midiOut->setSelectedItemIndex(0, juce::dontSendNotification);
	        return;
	    }
	    
	    m_midiOut->setSelectedItemIndex(_index + 1, juce::dontSendNotification);
	}
}

#include "midiPorts.h"

#include "pluginProcessor.h"

#include "juceUiLib/editor.h"

namespace
{
	void initComboBox(juce::ComboBox* _combo, const juce::Array<juce::MidiDeviceInfo>& _entries, const juce::String& _selectedEntry)
	{
		int inIndex = 0;

		_combo->addItem("<none>", 1);

		for (int i = 0; i < _entries.size(); i++)
		{
			const auto& input = _entries[i];

			if (input.identifier == _selectedEntry)
				inIndex = i + 1;

			_combo->addItem(input.name, i+2);
		}

		_combo->setSelectedItemIndex(inIndex, juce::dontSendNotification);
	}
}

namespace jucePluginEditorLib
{
	MidiPorts::MidiPorts(const genericUI::Editor& _editor, Processor& _processor) : m_processor(_processor)
	{
		m_midiIn = _editor.findComponentT<juce::ComboBox>("MidiIn", false);
		m_midiOut = _editor.findComponentT<juce::ComboBox>("MidiOut", false);

		if(m_midiIn)
		{
			const auto* in = getMidiPorts().getMidiInput();
			initComboBox(m_midiIn, juce::MidiInput::getAvailableDevices(), in != nullptr ? in->getIdentifier() : juce::String());
			m_midiIn->onChange = [this]{ updateMidiInput(m_midiIn->getSelectedItemIndex()); };
		}

		if(m_midiOut)
		{
			const auto* out = getMidiPorts().getMidiOutput();
			initComboBox(m_midiOut, juce::MidiOutput::getAvailableDevices(), out != nullptr ? out->getIdentifier() : juce::String());
			m_midiOut->onChange = [this]{ updateMidiOutput(m_midiOut->getSelectedItemIndex()); };
		}
   	}

	pluginLib::MidiPorts& MidiPorts::getMidiPorts() const
	{
		return m_processor.getMidiPorts();
	}

	void MidiPorts::showMidiPortFailedMessage(const char* _name) const
	{
		juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, m_processor.getProperties().name, 
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

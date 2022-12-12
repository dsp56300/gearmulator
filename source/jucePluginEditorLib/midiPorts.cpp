#include "midiPorts.h"

#include "pluginProcessor.h"

#include "../../juceUiLib/editor.h"

namespace jucePluginEditorLib
{
	MidiPorts::MidiPorts(const genericUI::Editor& _editor, Processor& _processor) : m_processor(_processor)
	{
		const auto& properties = m_processor.getConfig();

		const auto midiIn = properties.getValue("midi_input", "");
		const auto midiOut = properties.getValue("midi_output", "");

		if (!midiIn.isEmpty())
			m_processor.setMidiInput(midiIn);

		if (!midiOut.isEmpty())
			m_processor.setMidiOutput(midiOut);

		m_midiIn = _editor.findComponentT<juce::ComboBox>("MidiIn");
		m_midiOut = _editor.findComponentT<juce::ComboBox>("MidiOut");

	    m_midiIn->setTextWhenNoChoicesAvailable("-");

		const auto midiInputs = juce::MidiInput::getAvailableDevices();

		int inIndex = 0;

		m_midiIn->addItem("<none>", 1);

		for (int i = 0; i < midiInputs.size(); i++)
		{
			const auto input = midiInputs[i];

			if (m_processor.getMidiInput() != nullptr && input.identifier == m_processor.getMidiInput()->getIdentifier())
				inIndex = i + 1;

			m_midiIn->addItem(input.name, i+2);
		}

		m_midiIn->setSelectedItemIndex(inIndex, juce::dontSendNotification);

		m_midiIn->onChange = [this]() { updateMidiInput(m_midiIn->getSelectedItemIndex()); };

		m_midiOut->setTextWhenNoChoicesAvailable("-");

		const auto midiOutputs = juce::MidiOutput::getAvailableDevices();

		auto outIndex = 0;

		m_midiOut->addItem("<none>", 1);

		for (int i = 0; i < midiOutputs.size(); i++)
		{
			const auto output = midiOutputs[i];
			if (m_processor.getMidiOutput() != nullptr &&
				output.identifier == m_processor.getMidiOutput()->getIdentifier())
			{
				outIndex = i + 1;
			}
			m_midiOut->addItem(output.name, i+2);
		}

		m_midiOut->setSelectedItemIndex(outIndex, juce::dontSendNotification);

		m_midiOut->onChange = [this]() { updateMidiOutput(m_midiOut->getSelectedItemIndex()); };

		deviceManager = new juce::AudioDeviceManager();
   	}

	MidiPorts::~MidiPorts()
	{
		delete deviceManager;
	}

	void MidiPorts::updateMidiInput(int index)
	{
	    const auto list = juce::MidiInput::getAvailableDevices();

		auto& properties = m_processor.getConfig();

	    if (index <= 0)
	    {
	        properties.setValue("midi_input", "");
	        properties.save();
			m_lastInputIndex = 0;
	        m_midiIn->setSelectedItemIndex(index, juce::dontSendNotification);
	        return;
	    }

		index--;

		const auto newInput = list[index];

	    if (!deviceManager->isMidiInputDeviceEnabled(newInput.identifier))
	        deviceManager->setMidiInputDeviceEnabled(newInput.identifier, true);

	    if (!m_processor.setMidiInput(newInput.identifier))
	    {
	        m_midiIn->setSelectedItemIndex(0, juce::dontSendNotification);
	        m_lastInputIndex = 0;
	        return;
	    }

	    properties.setValue("midi_input", newInput.identifier);
	    properties.save();

	    m_midiIn->setSelectedItemIndex(index + 1, juce::dontSendNotification);
	    m_lastInputIndex = index;
	}

	void MidiPorts::updateMidiOutput(int index)
	{
	    const auto list = juce::MidiOutput::getAvailableDevices();

		auto& properties = m_processor.getConfig();

	    if (index == 0)
	    {
	        properties.setValue("midi_output", "");
	        properties.save();
	        m_midiOut->setSelectedItemIndex(index, juce::dontSendNotification);
	        m_lastOutputIndex = index;
	        m_processor.setMidiOutput("");
	        return;
	    }
	    index--;
	    const auto newOutput = list[index];
	    if (!m_processor.setMidiOutput(newOutput.identifier))
	    {
	        m_midiOut->setSelectedItemIndex(0, juce::dontSendNotification);
	        m_lastOutputIndex = 0;
	        return;
	    }
	    properties.setValue("midi_output", newOutput.identifier);
	    properties.save();

	    m_midiOut->setSelectedItemIndex(index + 1, juce::dontSendNotification);
	    m_lastOutputIndex = index;
	}
}

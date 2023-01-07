#include "processor.h"

namespace pluginLib
{
	Processor::Processor(const BusesProperties& _busesProperties) : juce::AudioProcessor(_busesProperties)
	{
	}

	Processor::~Processor()
	{
		m_controller.reset();
	}

	void Processor::getLastMidiOut(std::vector<synthLib::SMidiEvent>& dst)
	{
		juce::ScopedLock lock(getCallbackLock());
		std::swap(dst, m_midiOut);
		m_midiOut.clear();
	}

	void Processor::addMidiEvent(const synthLib::SMidiEvent& ev)
	{
		getPlugin().addMidiEvent(ev);
	}

	juce::MidiOutput *Processor::getMidiOutput() const { return m_midiOutput.get(); }
	juce::MidiInput *Processor::getMidiInput() const { return m_midiInput.get(); }

	bool Processor::setMidiOutput(const juce::String& _out)
	{
		if (m_midiOutput != nullptr && m_midiOutput->isBackgroundThreadRunning())
		{
			m_midiOutput->stopBackgroundThread();
		}
		m_midiOutput = juce::MidiOutput::openDevice(_out);
		if (m_midiOutput != nullptr)
		{
			m_midiOutput->startBackgroundThread();
			return true;
		}
		return false;
	}

	bool Processor::setMidiInput(const juce::String& _in)
	{
		if (m_midiInput != nullptr)
		{
			m_midiInput->stop();
		}
		m_midiInput = juce::MidiInput::openDevice(_in, this);
		if (m_midiInput != nullptr)
		{
			m_midiInput->start();
			return true;
		}
		return false;
	}

	void Processor::handleIncomingMidiMessage(juce::MidiInput *source, const juce::MidiMessage &message)
	{
		const auto* raw = message.getSysExData();
		if (raw)
		{
			const auto count = message.getSysExDataSize();
			auto syx = pluginLib::SysEx();
			syx.push_back(0xf0);
			for (int i = 0; i < count; i++)
			{
				syx.push_back(raw[i]);
			}
			syx.push_back(0xf7);
			synthLib::SMidiEvent sm;
			sm.source = synthLib::MidiEventSourcePlugin;
			sm.sysex = syx;
			getController().parseSysexMessage(syx);

			addMidiEvent(sm);

			if (m_midiOutput)
			{
				std::vector<synthLib::SMidiEvent> data;
				getLastMidiOut(data);
				if (!data.empty())
				{
					const auto msg = juce::MidiMessage::createSysExMessage(data.data(), static_cast<int>(data.size()));

					m_midiOutput->sendMessageNow(msg);
				}
			}
		}
		else
		{
			const auto count = message.getRawDataSize();
			const auto* rawData = message.getRawData();
			if (count >= 1 && count <= 3)
			{
				synthLib::SMidiEvent sm;
				sm.source = synthLib::MidiEventSourcePlugin;
				sm.a = rawData[0];
				sm.b = count > 1 ? rawData[1] : 0;
				sm.c = count > 2 ? rawData[2] : 0;
				addMidiEvent(sm);
			}
			else
			{
				synthLib::SMidiEvent sm;
				sm.source = synthLib::MidiEventSourcePlugin;
				auto syx = SysEx();
				for (int i = 0; i < count; i++)
				{
					syx.push_back(rawData[i]);
				}
				sm.sysex = syx;
				addMidiEvent(sm);
			}
		}
	}

	pluginLib::Controller& Processor::getController()
	{
	    if (m_controller == nullptr)
	        m_controller.reset(createController());

	    return *m_controller;
	}

	bool Processor::setLatencyBlocks(uint32_t _blocks)
	{
		return getPlugin().setLatencyBlocks(_blocks);
	}

	//==============================================================================
	void Processor::getStateInformation (juce::MemoryBlock& destData)
	{
	    // You should use this method to store your parameters in the memory block.
	    // You could do that either as raw data, or use the XML or ValueTree classes
	    // as intermediaries to make it easy to save and load complex data.

		std::vector<uint8_t> state;
		getPlugin().getState(state, synthLib::StateTypeGlobal);
		destData.append(&state[0], state.size());
	}

	void Processor::setStateInformation (const void* data, int sizeInBytes)
	{
	    // You should use this method to restore your parameters from this memory block,
	    // whose contents will have been created by the getStateInformation() call.
		setState(data, sizeInBytes);
	}

	void Processor::getCurrentProgramStateInformation(juce::MemoryBlock& destData)
	{
		std::vector<uint8_t> state;
		getPlugin().getState(state, synthLib::StateTypeCurrentProgram);
		destData.append(&state[0], state.size());
	}

	void Processor::setCurrentProgramStateInformation(const void* data, int sizeInBytes)
	{
		setState(data, sizeInBytes);
	}

	void Processor::setState(const void* _data, size_t _sizeInBytes)
	{
		if(_sizeInBytes < 1)
			return;

		std::vector<uint8_t> state;
		state.resize(_sizeInBytes);
		memcpy(&state[0], _data, _sizeInBytes);
		getPlugin().setState(state);
		if (hasController())
			getController().onStateLoaded();
	}
}

#pragma once

#include "juce_audio_devices/juce_audio_devices.h"

namespace emu88Player
{
	inline juce::String selectAudioDeviceType(juce::AudioDeviceManager& _manager, const juce::String& _type,
	                                        const int _outputChannels)
	{
		if(_type == _manager.getCurrentAudioDeviceType())
			return {};
		if(_type != "ASIO")
		{
			_manager.setCurrentAudioDeviceType(_type, true);
			return {};
		}

		// setCurrentAudioDeviceType opens a default driver (preferentially ASIO4ALL)
		// before the user can choose one. Loading an unavailable/broken driver can
		// crash inside its DLL. Restore an explicitly empty audio setup instead;
		// JUCE scans driver names but creates no device until the user selects it.
		// Keep the MIDI state, including remembered disconnected inputs.
		auto state = _manager.createStateXml();
		if(!state)
			state = std::make_unique<juce::XmlElement>("DEVICESETUP");
		state->setAttribute("deviceType", _type);
		for(const auto* attribute : {"audioDeviceName", "audioInputDeviceName", "audioOutputDeviceName",
		                             "audioDeviceRate", "audioDeviceBufferSize", "audioDeviceInChans",
		                             "audioDeviceOutChans"})
			state->removeAttribute(attribute);
		return _manager.initialise(0, _outputChannels, state.get(), false);
	}

	inline juce::String selectAudioOutputDevice(juce::AudioDeviceManager& _manager, const juce::String& _name)
	{
		auto setup = _manager.getAudioDeviceSetup();
		if(setup.outputDeviceName != _name)
		{
			// Let the new driver choose supported defaults instead of inheriting
			// the previous backend's buffer size, sample rate and channel mask.
			setup.sampleRate = 0;
			setup.bufferSize = 0;
			setup.useDefaultOutputChannels = true;
		}
		setup.outputDeviceName = _name;
		setup.inputDeviceName.clear();
		setup.useDefaultInputChannels = true;
		return _manager.setAudioDeviceSetup(setup, true);
	}

	inline bool showAudioDeviceControlPanel(juce::AudioDeviceManager& _manager)
	{
		auto* device = _manager.getCurrentAudioDevice();
		if(!device || !device->hasControlPanel() || !device->showControlPanel())
			return false;

		// closeAudioDevice deletes the device. Follow JUCE's device selector:
		// show the panel while it is alive, then reopen if its settings changed.
		_manager.closeAudioDevice();
		_manager.restartLastAudioDevice();
		return true;
	}
}

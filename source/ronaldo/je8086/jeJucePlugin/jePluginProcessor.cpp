#include "jePluginProcessor.h"

#include "jeController.h"
#include "jePatchManager.h"
#include "jeEditor.h"
#include "jucePluginEditorLib/pluginEditorState.h"
#include "jucePluginEditorLib/pluginEntry.h"
#include "skins.h"

// ReSharper disable once CppUnusedIncludeDirective
#include "BinaryData.h"
#include "jeLib/device.h"
#include "jeLib/romloader.h"
#include "jucePluginLib/processorPropertiesInit.h"

#include "synthLib/deviceException.h"

namespace jeJucePlugin
{
	class Controller;

	AudioPluginAudioProcessor::AudioPluginAudioProcessor() :
	    Processor(BusesProperties()
	                   .withOutput("Out AB", juce::AudioChannelSet::stereo(), true)
	                   .withInput("Input", juce::AudioChannelSet::stereo(), true)
		, {}, pluginLib::initProcessorProperties())
	{
		m_roms = jeLib::RomLoader::findROMs();

		// default to keyboard for now
		m_selectedRom = std::numeric_limits<size_t>::max();
		for (size_t i=0; i<m_roms.size(); ++i)
		{
			if (m_roms[i].getDeviceType() != jeLib::DeviceType::Keyboard)
				continue;
			m_selectedRom = i;
			break;
		}

		// FIXME clear rom list if there is no keyboard rom to prevent that the rack rom is used
		if (m_selectedRom == std::numeric_limits<size_t>::max())
		{
			m_roms.clear();
			m_selectedRom = 0;
		}

		setPatchManager(new PatchManager(*this));
	}

	jucePluginEditorLib::PluginEditorState* AudioPluginAudioProcessor::createEditorState()
	{
		return new jucePluginEditorLib::PluginEditorStateT<jeJucePlugin::Editor>(*this, g_includedSkins);
	}

	synthLib::Device* AudioPluginAudioProcessor::newDevice(const synthLib::DeviceCreateParams& _params)
	{
		return new jeLib::Device(_params);
	}

	void AudioPluginAudioProcessor::getRemoteDeviceParams(synthLib::DeviceCreateParams& _params) const
	{
		Processor::getRemoteDeviceParams(_params);

		// the selected ROM, for a local and a remote device alike
		const auto& rom = getSelectedRom();

		if (!rom.isValid())
			throw synthLib::DeviceException(synthLib::DeviceError::FirmwareMissing, "A firmware rom (a single 512k .bin file or multiple .mid files) is required, but was not found.");

		const auto& data = rom.getData();
		_params.romData.assign(data.begin(), data.end());
		_params.romName = rom.getName();
	}

	pluginLib::Controller* AudioPluginAudioProcessor::createController()
	{
		return new jeJucePlugin::Controller(*this);
	}

	const jeLib::Rom& AudioPluginAudioProcessor::getSelectedRom() const
	{
		if (m_roms.empty())
		{
			static jeLib::Rom emptyRom;
			return emptyRom;
		}
		if (getSelectedRomIndex() >= m_roms.size())
			return m_roms.back();
		return m_roms[m_selectedRom];
	}
}

TUS_PLUGIN_ENTRY(jeJucePlugin::AudioPluginAudioProcessor)

#include "jePluginProcessor.h"

#include "jeController.h"
#include "jePatchManager.h"
#include "jeEditor.h"
#include "jucePluginEditorLib/pluginEditorState.h"
#include "jucePluginEditorLib/pluginEntry.h"
#include "skins.h"

// ReSharper disable once CppUnusedIncludeDirective
#include "BinaryData.h"
#include "baseLib/binarystream.h"
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
		// findROMs() lists the JP-8000 images first, so the default, index 0, is the keyboard whenever there is one
		m_roms = jeLib::RomLoader::findROMs();

		std::vector<std::string> names;
		names.reserve(m_roms.size());
		for (const auto& rom : m_roms)
			names.push_back(rom.getName());

		// "ROM " v1: the device type, then the name. Of several images that match, the first one wins
		FirmwareChunk chunk{"ROM ", 1};
		chunk.write = [this](baseLib::BinaryStream& _s, const uint32_t _index)
		{
			const auto& rom = m_roms[_index];
			_s.write(static_cast<uint8_t>(rom.getDeviceType()));
			_s.write(rom.getName());
		};
		chunk.find = [this](baseLib::BinaryStream& _s, uint32_t) -> std::optional<uint32_t>
		{
			const auto type = static_cast<jeLib::DeviceType>(_s.read<uint8_t>());
			const auto name = _s.readString();

			for (uint32_t i = 0; i < m_roms.size(); ++i)
			{
				if (m_roms[i].getDeviceType() == type && m_roms[i].getName() == name)
					return i;
			}
			return {};
		};
		setFirmwareImages(std::move(names), std::move(chunk));

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
			throw synthLib::DeviceException(synthLib::DeviceError::FirmwareMissing, "No firmware found. The JP-8000 needs its flash image (.bin) or the complete set of update .mid files, the JP-8080 a complete 1 MB flash dump (.bin).");

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
		if (getFirmwareIndex() >= m_roms.size())
		{
			static jeLib::Rom emptyRom;
			return emptyRom;
		}
		return m_roms[getFirmwareIndex()];
	}
}

TUS_PLUGIN_ENTRY(jeJucePlugin::AudioPluginAudioProcessor)

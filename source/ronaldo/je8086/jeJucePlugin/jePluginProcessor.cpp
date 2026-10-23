#include "jePluginProcessor.h"

#include <algorithm>

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
		// "ROM " v1: the device type, then the name. Of several images that match, the first one wins
		FirmwareChunk chunk{"ROM ", 1};
		chunk.write = [this](baseLib::BinaryStream& _s, const uint32_t _index)
		{
			const std::lock_guard lock(m_romsMutex);
			const auto& rom = *m_roms[_index];
			_s.write(static_cast<uint8_t>(rom.getDeviceType()));
			_s.write(rom.getName());
		};
		chunk.find = [this](baseLib::BinaryStream& _s, uint32_t) -> std::optional<uint32_t>
		{
			const auto type = static_cast<jeLib::DeviceType>(_s.read<uint8_t>());
			const auto name = _s.readString();

			const std::lock_guard lock(m_romsMutex);
			for (uint32_t i = 0; i < m_roms.size(); ++i)
			{
				if (m_roms[i]->getDeviceType() == type && m_roms[i]->getName() == name)
					return i;
			}
			return {};
		};
		// findROMs() lists the JP-8000 images first, so the default, index 0, is the keyboard whenever there is one
		setFirmwareImages(findNewFirmwareImages(), std::move(chunk));

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
		const std::lock_guard lock(m_romsMutex);
		if (getFirmwareIndex() >= m_roms.size())
		{
			static jeLib::Rom emptyRom;
			return emptyRom;
		}
		return *m_roms[getFirmwareIndex()];
	}

	std::vector<const jeLib::Rom*> AudioPluginAudioProcessor::getRoms() const
	{
		const std::lock_guard lock(m_romsMutex);
		std::vector<const jeLib::Rom*> roms;
		for (const auto& rom : m_roms)
			roms.push_back(rom.get());
		return roms;
	}

	std::vector<std::string> AudioPluginAudioProcessor::findNewFirmwareImages()
	{
		std::vector<std::string> names;

		for (auto& rom : jeLib::RomLoader::findROMs())
		{
			// an image is its content, as for findROMs()
			const auto listed = std::any_of(m_roms.begin(), m_roms.end(), [&rom](const auto& _listed)
			{
				return _listed->getData() == rom.getData();
			});

			if (listed)
				continue;

			names.push_back(rom.getName());

			const std::lock_guard lock(m_romsMutex);
			m_roms.push_back(std::make_unique<const jeLib::Rom>(std::move(rom)));
		}
		return names;
	}
}

TUS_PLUGIN_ENTRY(jeJucePlugin::AudioPluginAudioProcessor)

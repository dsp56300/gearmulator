#include "jePluginProcessor.h"

#include "jeController.h"
#include "jePluginEditorState.h"

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

		if (m_selectedRom == std::numeric_limits<size_t>::max())
		{
			m_selectedRom = 0;
		}

		getController();
		const auto latencyBlocks = getConfig().getIntValue("latencyBlocks", static_cast<int>(getPlugin().getLatencyBlocks()));
		Processor::setLatencyBlocks(latencyBlocks);
	}

	AudioPluginAudioProcessor::~AudioPluginAudioProcessor()
	{
		destroyEditorState();
		// the controller listens to evRomChanged, which would be gone before the base class destroys the controller
		destroyController();
	}

	jucePluginEditorLib::PluginEditorState* AudioPluginAudioProcessor::createEditorState()
	{
		return new PluginEditorState(*this);
	}

	synthLib::Device* AudioPluginAudioProcessor::createDevice()
	{
		const auto& rom = getSelectedRom();

		auto* errorMsg = "No firmware found. The JP-8000 needs its flash image (.bin) or the complete set of update .mid files, the JP-8080 a complete 1 MB flash dump (.bin).";

		if (!rom.isValid())
			throw synthLib::DeviceException(synthLib::DeviceError::FirmwareMissing, errorMsg);

		synthLib::DeviceCreateParams params;

		params.romData = rom.getData();
		params.romName = rom.getName();
		params.homePath = getDataFolder();

		return new jeLib::Device(params);
	}

	void AudioPluginAudioProcessor::getRemoteDeviceParams(synthLib::DeviceCreateParams& _params) const
	{
		Processor::getRemoteDeviceParams(_params);

		const auto& rom = getSelectedRom();

		if (rom.isValid())
		{
			const auto& data = rom.getData();
			_params.romData.assign(data.begin(), data.end());
			_params.romName = rom.getName();
		}
	}

	bool AudioPluginAudioProcessor::setSelectedRom(const size_t _index)
	{
		if (_index >= m_roms.size())
			return false;
		if (_index == m_selectedRom)
			return true;

		const auto previous = m_selectedRom;
		m_selectedRom = _index;

		// setDeviceType keeps the running device and shows the error if the new one cannot be created
		const auto* device = m_device.get();
		setDeviceType(getDeviceType(), true);

		if (m_device.get() == device)
		{
			m_selectedRom = previous;
			return false;
		}

		evRomChanged.invoke(&getSelectedRom());
		return true;
	}

	void AudioPluginAudioProcessor::saveChunkData(baseLib::BinaryStream& s)
	{
		const auto& rom = getSelectedRom();

		if (rom.isValid())
		{
			baseLib::ChunkWriter cw(s, "ROM ", 1);
			s.write(static_cast<uint8_t>(rom.getDeviceType()));
			s.write(rom.getName());
		}

		Processor::saveChunkData(s);
	}

	void AudioPluginAudioProcessor::loadChunkData(baseLib::ChunkReader& _cr)
	{
		_cr.add("ROM ", 1, [this](baseLib::BinaryStream& _s, unsigned)
		{
			const auto type = static_cast<jeLib::DeviceType>(_s.read<uint8_t>());
			const auto name = _s.readString();

			for (size_t i = 0; i < m_roms.size(); ++i)
			{
				if (m_roms[i].getDeviceType() == type && m_roms[i].getName() == name)
				{
					setSelectedRom(i);
					return;
				}
			}
		});

		Processor::loadChunkData(_cr);
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

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new jeJucePlugin::AudioPluginAudioProcessor();
}

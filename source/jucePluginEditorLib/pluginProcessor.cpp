#include "pluginProcessor.h"

#include "pluginEditorState.h"
#include "pluginEditorWindow.h"

#include "baseLib/binarystream.h"

#include "synthLib/os.h"

#ifdef GEARMULATOR_BUILD_MCP_SERVER
#include "mcpServerLib/mcpPluginServer.h"
#include "mcpDomTools.h"
#include "mcpPatchManagerTools.h"
#include "networkLib/logging.h"
#endif

#ifdef ZYNTHIAN
#include "dsp56kBase/logging.h"
#endif

namespace jucePluginEditorLib
{
	namespace 
	{
#ifdef ZYNTHIAN
		void noLoggingFunc(const std::string&)
		{
			// https://discourse.zynthian.org/t/deadlock-when-attempting-to-log-to-stdout/10169
		}
#endif

		std::string getPluginFormatName(const juce::AudioProcessor::WrapperType _wrapperType, const std::string& _moduleFilePath)
		{
			switch (_wrapperType)
			{
			case juce::AudioProcessor::wrapperType_VST:         return "vst2";
			case juce::AudioProcessor::wrapperType_VST3:        return "vst3";
			case juce::AudioProcessor::wrapperType_AudioUnit:   return "au";
			case juce::AudioProcessor::wrapperType_AudioUnitv3: return "auv3";
			case juce::AudioProcessor::wrapperType_LV2:         return "lv2";
			case juce::AudioProcessor::wrapperType_Standalone:  return "standalone";
			case juce::AudioProcessor::wrapperType_AAX:         return "aax";
			case juce::AudioProcessor::wrapperType_Undefined:
			default:
				// CLAP reports as wrapperType_Undefined, detect from module file path
				if (_moduleFilePath.find(".clap") != std::string::npos)
					return "clap";
				return {};
			}
		}
	}

	Processor::Processor(const BusesProperties& _busesProperties, const juce::PropertiesFile::Options& _configOptions, const pluginLib::Processor::Properties& _properties)
	: pluginLib::Processor(_busesProperties, _properties)
	, m_configOptions(_configOptions)
	, m_config(initConfigFile(_configOptions), _configOptions)
	{
#ifdef ZYNTHIAN
		Logging::setLogFunc(&noLoggingFunc);
#endif
		savePluginLoadPath();

#ifdef GEARMULATOR_BUILD_MCP_SERVER
		try
		{
			m_mcpServer = std::make_unique<mcpServer::McpPluginServer>(*this);
			registerDomTools(m_mcpServer->getServer(), *this);
			registerPatchManagerTools(m_mcpServer->getServer(), *this);
			if (m_mcpServer->start())
			{
				LOGNET(networkLib::LogLevel::Info, "MCP server started on port " << m_mcpServer->getPort() << " for plugin " << _properties.name);
			}
			else
			{
				LOGNET(networkLib::LogLevel::Warning, "Failed to start MCP server for plugin " << _properties.name);
				m_mcpServer.reset();
			}
		}
		catch (const std::exception& e)
		{
			LOGNET(networkLib::LogLevel::Warning, "MCP server creation failed: " << e.what());
			m_mcpServer.reset();
		}
#endif
	}

	Processor::~Processor()
	{
#ifdef GEARMULATOR_BUILD_MCP_SERVER
		m_mcpServer.reset();
#endif
		assert(!m_editorState && "call destroyEditorState in destructor of derived class");
	}

	bool Processor::setLatencyBlocks(const uint32_t _blocks)
	{
		if(!pluginLib::Processor::setLatencyBlocks(_blocks))
			return false;

		getConfig().setValue("latencyBlocks", static_cast<int>(_blocks));
		getConfig().saveIfNeeded();

		return true;
	}

	bool Processor::hasEditor() const
	{
		return true; // (change this to false if you choose to not supply an editor)
	}

	juce::AudioProcessorEditor* Processor::createEditor()
	{
		assert(hasEditor() && "not supposed to be called as we declared not providing an editor");

		if(!hasEditor())
			return nullptr;

		if(!m_editorState)
		{
			m_editorState.reset(createEditorState());
			if(!m_editorStateData.empty())
				m_editorState->setPerInstanceConfig(m_editorStateData);
		}

	    auto* window = new EditorWindow(*this, *m_editorState, getConfig());

		if(!m_editorState->hasSkin())
		{
			constexpr int w = 600;
			constexpr int h = 300;

			window->setSize(w,h);

			auto* l = new juce::Label({}, 
				"No skins found, check your installation\n"
				"\n"
				"Skins need to be located at:\n"
				"\n" +
				m_editorState->getSkinFolder()
			);

			l->setSize(w,h);
			l->setJustificationType(juce::Justification::centred);
			l->setColour(juce::Label::ColourIds::textColourId, juce::Colour(0xffff0000));
			l->setColour(juce::Label::ColourIds::backgroundColourId, juce::Colour(0xff111111));

			window->addAndMakeVisible(l);
		}
		return window;
	}

	void Processor::destroyEditorState()
	{
		m_editorState.reset();
	}

	void Processor::saveChunkData(baseLib::BinaryStream& s)
	{
		pluginLib::Processor::saveChunkData(s);

		if(m_editorState)
		{
			m_editorStateData.clear();
			m_editorState->getPerInstanceConfig(m_editorStateData);
		}

		if(!m_editorStateData.empty())
		{
			baseLib::ChunkWriter cw(s, "EDST", 1);
			s.write(m_editorStateData);
		}

		getController().saveChunkData(s);
	}

	bool Processor::loadCustomData(const std::vector<uint8_t>& _sourceBuffer)
	{
		// if there is no chunk in the data, but the data is not empty, it's an old non-Vavra chunk that only carries the editor state
		if(!pluginLib::Processor::loadCustomData(_sourceBuffer))
			m_editorStateData = _sourceBuffer;

		if(m_editorState)
			m_editorState->setPerInstanceConfig(m_editorStateData);

		return true;
	}
	
	void Processor::loadChunkData(baseLib::ChunkReader& _cr)
	{
		pluginLib::Processor::loadChunkData(_cr);

		_cr.add("EDST", 1, [this](baseLib::BinaryStream& _binaryStream, unsigned _version)
		{
			_binaryStream.read(m_editorStateData);
		});

		getController().loadChunkData(_cr);
	}

	void Processor::savePluginLoadPath()
	{
		const auto moduleFilePath = synthLib::getModuleFilePath();
		if (moduleFilePath.empty())
			return;

		const auto format = getPluginFormatName(wrapperType, moduleFilePath);
		if (format.empty())
			return;

		const auto key = "pluginPath_" + format;
		getConfig().setValue(juce::String(key), juce::String(moduleFilePath));
		getConfig().saveIfNeeded();
	}

	juce::File Processor::initConfigFile(const juce::PropertiesFile::Options& _o) const
	{
		// copy from old location to new if still exists
		juce::File oldFile(_o.getDefaultFile());

		juce::File newFile(getConfigFile(false));

		if(oldFile.existsAsFile())
		{
			newFile.createDirectory();
			if(!oldFile.copyFileTo(newFile))
				return oldFile;
			oldFile.deleteFile();
			return newFile;
		}
		return newFile;
	}
}

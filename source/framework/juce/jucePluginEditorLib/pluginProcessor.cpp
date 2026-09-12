#include "pluginProcessor.h"

#include "pluginEditorState.h"
#include "pluginEditorWindow.h"

#include "baseLib/binarystream.h"

#include "synthLib/os.h"

#include <cstring>

#include "mcpServerLib/mcpPluginServer.h"
#include "mcpDomTools.h"
#include "mcpPatchManagerTools.h"
#include "networkLib/logging.h"

#ifdef ZYNTHIAN
#include "dsp56kBase/logging.h"
#endif

namespace jucePluginEditorLib
{
	namespace
	{
		constexpr const char* g_skinVariablePrefix = "skinvar_";

#ifdef ZYNTHIAN
		void noLoggingFunc(const std::string&)
		{
			// https://discourse.zynthian.org/t/deadlock-when-attempting-to-log-to-stdout/10169
		}
#endif

		// True when the plugin DLL is hosted by a JUCE build-time helper
		// (juce_vst3_helper / juce_lv2_helper / juce_au_helper). Those exes
		// load the plugin only to enumerate metadata; they do not run an audio
		// host and have no use for the MCP server. Starting it there is pure
		// overhead and was provoking a port-13710 race that crashed n2x builds.
		bool isJuceHelperProcess()
		{
			// hostApplicationPath, not currentExecutableFile: inside a plugin the latter is the module JUCE
			// itself lives in - our own .dll/.so - so the comparison below could never match and every helper
			// went on starting a server. hostApplicationPath is the process, which is what we are asking about.
			const auto exeName = juce::File::getSpecialLocation(
				juce::File::hostApplicationPath).getFileNameWithoutExtension().toLowerCase();
			return exeName.contains("juce_vst3_helper")
				|| exeName.contains("juce_lv2_helper")
				|| exeName.contains("juce_au_helper");
		}

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

		loadGlobalSkinVariables();

		// The resampler mode a state carries wins, but until one is loaded the plugin uses whatever the
		// user picked last, and Mame HQ if they never picked anything. (BUG-10273, BUG-10277)
		const auto resamplerMode = m_config.getIntValue("resamplerMode", static_cast<int>(synthLib::Resampler::Mode::MameHq));

		if (resamplerMode >= 0 && resamplerMode < static_cast<int>(synthLib::Resampler::Mode::Count))
			setResamplerMode(static_cast<synthLib::Resampler::Mode>(resamplerMode));

		if (m_config.getBoolValue("enableMcpServer", false) && !isJuceHelperProcess())
			startMcpServer();
	}

	Processor::~Processor()
	{
		stopMcpServer();
		assert(!m_editorState && "call destroyEditorState in destructor of derived class");
	}

	void Processor::loadGlobalSkinVariables()
	{
		// Globals are stored one config key per variable, prefixed so they cannot collide with the
		// plugin's own settings. Loading them does not report a change: nothing can be listening yet,
		// and a skin must not see its own stored value arrive as if the user had just edited it.
		std::map<std::string, pluginLib::SkinVariables::Value> values;

		const auto& properties = m_config.getAllProperties();

		for (int i = 0; i < properties.size(); ++i)
		{
			const auto key = properties.getAllKeys()[i].toStdString();

			if (key.rfind(g_skinVariablePrefix, 0) != 0)
				continue;

			const auto name = key.substr(std::strlen(g_skinVariablePrefix));

			if (name.empty())
				continue;

			values.insert_or_assign(name, pluginLib::SkinVariables::fromString(properties.getAllValues()[i].toStdString()));
		}

		auto& vars = getSkinVariables();

		vars.setGlobalsFromStorage(std::move(values));

		m_skinVariablesListener.set(vars.evChanged, [this](const std::string& _name, const pluginLib::SkinVariables::Scope _scope)
		{
			if (_scope != pluginLib::SkinVariables::Scope::Global)
				return;

			const auto key = juce::String(g_skinVariablePrefix + _name);

			if (const auto* v = getSkinVariables().get(_name, _scope))
				m_config.setValue(key, juce::String(pluginLib::SkinVariables::toString(*v)));
			else
				m_config.removeValue(key);

			m_config.saveIfNeeded();
		});
	}

	bool Processor::setLatencyBlocks(const uint32_t _blocks)
	{
		if(!pluginLib::Processor::setLatencyBlocks(_blocks))
			return false;

		getConfig().setValue("latencyBlocks", static_cast<int>(_blocks));
		getConfig().saveIfNeeded();

		return true;
	}

	bool Processor::setDspThreads(const uint32_t _threads)
	{
		if(!pluginLib::Processor::setDspThreads(_threads))
			return false;

		getConfig().setValue("dspThreads", static_cast<int>(_threads));
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

	void Processor::startMcpServer()
	{
		if (m_mcpServer)
			return;

		try
		{
			m_mcpServer = std::make_unique<mcpServer::McpPluginServer>(*this);
			registerDomTools(m_mcpServer->getServer(), *this);
			registerPatchManagerTools(m_mcpServer->getServer(), *this);
			if (m_mcpServer->start())
			{
				LOGNET(networkLib::LogLevel::Info, "MCP server started on port " << m_mcpServer->getPort() << " for plugin " << getProperties().name);
			}
			else
			{
				LOGNET(networkLib::LogLevel::Warning, "Failed to start MCP server for plugin " << getProperties().name);
				m_mcpServer.reset();
			}
		}
		catch (const std::exception& e)
		{
			LOGNET(networkLib::LogLevel::Warning, "MCP server creation failed: " << e.what());
			m_mcpServer.reset();
		}
		catch (...)
		{
			// Plugin startup must never crash the host. Swallow any non-
			// std::exception and continue without MCP.
			LOGNET(networkLib::LogLevel::Warning, "MCP server creation failed with unknown exception");
			m_mcpServer.reset();
		}
	}

	void Processor::stopMcpServer()
	{
		if (m_mcpServer)
		{
			LOGNET(networkLib::LogLevel::Info, "MCP server stopped for plugin " << getProperties().name);
			m_mcpServer.reset();
		}
	}

	void Processor::setMcpServerEnabled(const bool _enabled)
	{
		if (_enabled)
			startMcpServer();
		else
			stopMcpServer();
	}
}

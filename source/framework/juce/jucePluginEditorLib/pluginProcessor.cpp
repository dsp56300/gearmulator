#include "pluginProcessor.h"

#include "pluginEditor.h"
#include "pluginEditorState.h"
#include "pluginEditorWindow.h"

#include "patchmanager/patchmanager.h"

#include "baseLib/binarystream.h"

#include "juceUiLib/messageBox.h"

#include <cstring>

#include "mcpPluginServer.h"
#include "mcpEditorTools.h"
#include "mcpServerLib/mcpDomTools.h"
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

	}

	Processor::Processor(const BusesProperties& _busesProperties, const juce::PropertiesFile::Options& _configOptions, const pluginLib::Processor::Properties& _properties)
	: pluginLib::Processor(_busesProperties, _properties)
	, m_configOptions(_configOptions)
	, m_config(initConfigFile(_configOptions), _configOptions)
	{
#ifdef ZYNTHIAN
		Logging::setLogFunc(&noLoggingFunc);
#endif
		// The path each format was loaded from used to be written into the config for the plugin
		// manager to read, which it never did - it looks on disk instead. A key left behind would
		// only get staler, so it is swept out when one is found. getAllKeys() hands out a reference
		// to the live array that removeValue() erases from, which would leave the loop reading past
		// its end, so the sweep runs over a copy of the keys.
		const auto keys = m_config.getAllProperties().getAllKeys();

		for (const auto& key : keys)
		{
			if (key.startsWith("pluginPath_"))
				m_config.removeValue(key);
		}

		loadGlobalSkinVariables();

		// The resampler mode a state carries wins, but until one is loaded the plugin uses whatever the
		// user picked last, and Mame HQ if they never picked anything. (BUG-10273, BUG-10277)
		const auto resamplerMode = m_config.getIntValue("resamplerMode", static_cast<int>(synthLib::Resampler::Mode::MameHq));

		if (resamplerMode >= 0 && resamplerMode < static_cast<int>(synthLib::Resampler::Mode::Count))
			setResamplerMode(static_cast<synthLib::Resampler::Mode>(resamplerMode));
	}

	juce::PropertiesFile::Options Processor::legacyConfigOptions(const char* _appName)
	{
		juce::PropertiesFile::Options opts;
		opts.applicationName = _appName;
		opts.filenameSuffix = ".settings";
		opts.folderName = _appName;
		opts.osxLibrarySubFolder = juce::String("Application Support/") + _appName;
		return opts;
	}

	void Processor::finishConstruction()
	{
		getController();
		setLatencyBlocks(static_cast<uint32_t>(m_config.getIntValue("latencyBlocks", static_cast<int>(getDefaultLatencyBlocks()))));

		// Last: a client calls tools on the server thread as soon as the discovery file lists this instance. Started
		// in the base constructor, a set_plugin_state ran while the product constructor still created the controller.
		// Not in the JUCE build helpers: they load the plugin only to read its properties, and two of them building in
		// parallel raced for port 13710, the loser crashed the build
		if (m_config.getBoolValue("enableMcpServer", false) && !genericUI::MessageBox::isHeadlessHost())
			startMcpServer();
	}

	Processor::~Processor()
	{
		stopMcpServer();
		assert(!m_editorState && "call destroyEditorState in destructor of derived class");
		assert(!m_patchManager && "call destroyPatchManager in destructor of derived class");
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

	// FinalProcessor's destructor calls this first after the MCP server is off, so this is where teardown begins: the
	// MIDI inputs stop calling in before the editor, the patch manager, the controller and the device go away
	void Processor::destroyEditorState()
	{
		stopMcpServer();
		shutdown();
		m_editorState.reset();
	}

	void Processor::setPatchManager(patchManager::PatchManager* _patchManager)
	{
		m_patchManager.reset(_patchManager);
	}

	void Processor::destroyPatchManager()
	{
		// before the derived patch manager is destroyed: the host may delete the plugin on another thread than the
		// message thread, which may be in the middle of processPending()
		if(m_patchManager)
			m_patchManager->shutdown();
		m_patchManager.reset();
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

		if(m_patchManager)
		{
			std::vector<uint8_t> patchManagerData;
			m_patchManager->getPerInstanceConfig(patchManagerData);

			baseLib::ChunkWriter cw(s, "PMST", 1);
			s.write(patchManagerData);
		}

		getController().saveChunkData(s);
	}

	bool Processor::loadCustomData(const std::vector<uint8_t>& _sourceBuffer)
	{
		// if there is no chunk in the data, but the data is not empty, it's an old non-Vavra chunk that only carries the editor state
		if(!pluginLib::Processor::loadCustomData(_sourceBuffer))
		{
			m_editorStateData = _sourceBuffer;
			migratePatchManagerState(m_editorStateData);
		}

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
			migratePatchManagerState(m_editorStateData);
		});

		// saved after EDST, so it wins over a patch manager state migrated from there
		_cr.add("PMST", 1, [this](baseLib::BinaryStream& _binaryStream, unsigned _version)
		{
			std::vector<uint8_t> patchManagerData;
			_binaryStream.read(patchManagerData);
			if(m_patchManager)
				m_patchManager->setPerInstanceConfig(patchManagerData);
		});

		getController().loadChunkData(_cr);
	}

	void Processor::migratePatchManagerState(const std::vector<uint8_t>& _editorStateData) const
	{
		// the editor state carried the patch manager state before the processor owned the patch manager
		if(!m_patchManager || _editorStateData.size() < sizeof(uint32_t))
			return;

		// an old version that didn't use chunks yet
		baseLib::BinaryStream oldStream(_editorStateData);
		if(oldStream.read<uint32_t>() == 1)
		{
			m_patchManager->setPerInstanceConfig(_editorStateData);
			return;
		}

		baseLib::BinaryStream s(_editorStateData);
		baseLib::ChunkReader cr(s);
		cr.add("pmDt", 2, [this](baseLib::BinaryStream& _s, uint32_t/* _version*/)
		{
			std::vector<uint8_t> data;
			_s.read(data);
			m_patchManager->setPerInstanceConfig(data);
		});
		cr.read();
	}

	juce::File Processor::initConfigFile(const juce::PropertiesFile::Options& _o) const
	{
		juce::File newFile(getConfigFile(false));

		// Without options there never was an old location. Their default file is a folder outside the product's own,
		// and on macOS asking for it asserts
		if(_o.applicationName.isEmpty())
			return newFile;

		// copy from old location to new if still exists
		juce::File oldFile(_o.getDefaultFile());

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
			// The DOM tools work on whatever component holds the document, which here is the editor -
			// and it may be closed and opened again while the server keeps running.
			mcpServer::registerDomTools(m_mcpServer->getServer(), [this]() -> juceRmlUi::RmlComponent*
			{
				auto* editorState = getEditorState();
				auto* editor = editorState ? editorState->getEditor() : nullptr;
				return editor ? editor->getRmlComponent() : nullptr;
			});
			registerEditorTools(m_mcpServer->getServer(), *this);
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

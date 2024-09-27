#include "pluginProcessor.h"

#include "pluginEditorState.h"
#include "pluginEditorWindow.h"

#include "baseLib/binarystream.h"

#ifdef ZYNTHIAN
#include "dsp56kEmu/logging.h"
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
	}

	Processor::Processor(const BusesProperties& _busesProperties, const juce::PropertiesFile::Options& _configOptions, const pluginLib::Processor::Properties& _properties)
	: pluginLib::Processor(_busesProperties, _properties)
	, m_configOptions(_configOptions)
	, m_config(_configOptions)
	{
#ifdef ZYNTHIAN
		Logging::setLogFunc(&noLoggingFunc);
#endif
	}

	Processor::~Processor()
	{
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

	    return new EditorWindow(*this, *m_editorState, getConfig());
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
}

#include "pluginProcessor.h"

#include "pluginEditorState.h"
#include "pluginEditorWindow.h"

namespace jucePluginEditorLib
{
	Processor::Processor(const BusesProperties& _busesProperties, const juce::PropertiesFile::Options& _configOptions)
	: pluginLib::Processor(_busesProperties)
	, m_configOptions(_configOptions)
	, m_config(_configOptions)
	{
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

	void Processor::loadCustomData(const std::vector<uint8_t>& _sourceBuffer)
	{
		m_editorStateData = _sourceBuffer;

		if(m_editorState)
			m_editorState->setPerInstanceConfig(m_editorStateData);
	}

	void Processor::saveCustomData(std::vector<uint8_t>& _targetBuffer)
	{
		if(m_editorState)
		{
			m_editorStateData.clear();
			m_editorState->getPerInstanceConfig(m_editorStateData);
		}

		if(!m_editorStateData.empty())
			_targetBuffer.insert(_targetBuffer.end(), m_editorStateData.begin(), m_editorStateData.end());
	}
}

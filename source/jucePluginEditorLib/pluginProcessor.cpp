#include "pluginProcessor.h"

#include "pluginEditorState.h"
#include "pluginEditorWindow.h"

#include "../synthLib/binarystream.h"

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
		// Compatibility with old Vavra versions that only wrote gain parameters
		if(_sourceBuffer.size() == sizeof(float) * 2 + sizeof(uint32_t))
		{
			pluginLib::Processor::loadCustomData(_sourceBuffer);
			return;
		}

		pluginLib::Processor::loadCustomData(_sourceBuffer);

		synthLib::BinaryStream s(_sourceBuffer);

		synthLib::ChunkReader cr(s);
		cr.add("EDST", 1, [this](synthLib::BinaryStream& _binaryStream, unsigned _version)
		{
			_binaryStream.read(m_editorStateData);
		});

		// if there is no chunk in the data, it's an old non-Vavra chunk that only carries the editor state
		if(!cr.tryRead() || cr.numRead() == 0)
		{
			m_editorStateData = _sourceBuffer;
		}

		if(m_editorState)
			m_editorState->setPerInstanceConfig(m_editorStateData);
	}

	void Processor::saveCustomData(std::vector<uint8_t>& _targetBuffer)
	{
		pluginLib::Processor::saveCustomData(_targetBuffer);

		if(m_editorState)
		{
			m_editorStateData.clear();
			m_editorState->getPerInstanceConfig(m_editorStateData);
		}

		if(!m_editorStateData.empty())
		{
			synthLib::BinaryStream s;
			{
				synthLib::ChunkWriter cw(s, "EDST", 1);
				s.write(m_editorStateData);
			}

			s.toVector(_targetBuffer, true);
		}
	}
}

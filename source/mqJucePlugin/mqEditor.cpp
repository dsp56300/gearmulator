#include "mqEditor.h"

#include "BinaryData.h"
#include "PluginProcessor.h"
#include "../jucePluginLib/parameterbinding.h"

namespace mqJucePlugin
{
	Editor::Editor(pluginLib::Processor& _processor, pluginLib::ParameterBinding& _binding) : genericUI::Editor(static_cast<EditorInterface&>(*this)), m_processor(_processor), m_parameterBinding(_binding)
	{
	}

	const char* Editor::findNamedResourceByFilename(const std::string& _filename, uint32_t& _size)
	{
		for(size_t i=0; i<BinaryData::namedResourceListSize; ++i)
		{
			if (BinaryData::originalFilenames[i] != _filename)
				continue;

			int size = 0;
			const auto res = BinaryData::getNamedResource(BinaryData::namedResourceList[i], size);
			_size = static_cast<uint32_t>(size);
			return res;
		}
		return nullptr;
	}

	const char* Editor::getResourceByFilename(const std::string& _name, uint32_t& _dataSize)
	{
		return findNamedResourceByFilename(_name, _dataSize);
	}

	int Editor::getParameterIndexByName(const std::string& _name)
	{
		return static_cast<int>(m_processor.getController().getParameterIndexByName(_name));
	}

	juce::Value* Editor::getParameterValue(int _parameterIndex)
	{
		return m_processor.getController().getParamValueObject(_parameterIndex);
	}

	bool Editor::bindParameter(juce::Slider& _target, int _parameterIndex)
	{
		m_parameterBinding.bind(_target, _parameterIndex);
		return true;
	}

	bool Editor::bindParameter(juce::Button& _target, int _parameterIndex)
	{
		m_parameterBinding.bind(_target, _parameterIndex);
		return true;
	}

	bool Editor::bindParameter(juce::ComboBox& _target, int _parameterIndex)
	{
		m_parameterBinding.bind(_target, _parameterIndex);
		return true;
	}
}

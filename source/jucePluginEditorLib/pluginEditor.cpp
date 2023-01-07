#include "pluginEditor.h"

#include "pluginProcessor.h"
#include "../jucePluginLib/parameterbinding.h"

#include "../synthLib/os.h"

namespace jucePluginEditorLib
{
	Editor::Editor(pluginLib::Processor& _processor, pluginLib::ParameterBinding& _binding, std::string _skinFolder)
		: genericUI::Editor(static_cast<EditorInterface&>(*this))
		, m_processor(_processor)
		, m_binding(_binding)
		, m_skinFolder(std::move(_skinFolder))
	{
	}

	const char* Editor::getResourceByFilename(const std::string& _name, uint32_t& _dataSize)
	{
		if(!m_skinFolder.empty())
		{
			auto readFromCache = [this, &_name, &_dataSize]()
			{
				const auto it = m_fileCache.find(_name);
				if(it == m_fileCache.end())
				{
					_dataSize = 0;
					return static_cast<char*>(nullptr);
				}
				_dataSize = static_cast<uint32_t>(it->second.size());
				return &it->second.front();
			};

			auto* res = readFromCache();

			if(res)
				return res;

			const auto modulePath = synthLib::getModulePath();
			const auto folder = synthLib::validatePath(m_skinFolder.find(modulePath) == 0 ? m_skinFolder : modulePath + m_skinFolder);

			// try to load from disk first
			FILE* hFile = fopen((folder + _name).c_str(), "rb");
			if(hFile)
			{
				fseek(hFile, 0, SEEK_END);
				_dataSize = ftell(hFile);
				fseek(hFile, 0, SEEK_SET);

				std::vector<char> data;
				data.resize(_dataSize);
				const auto readCount = fread(&data.front(), 1, _dataSize, hFile);
				fclose(hFile);

				if(readCount == _dataSize)
					m_fileCache.insert(std::make_pair(_name, std::move(data)));

				res = readFromCache();

				if(res)
					return res;
			}
		}

		uint32_t size = 0;
		const auto res = findResourceByFilename(_name, size);
		if(!res)
			throw std::runtime_error("Failed to find file named " + _name);
		_dataSize = size;
		return res;
	}

	int Editor::getParameterIndexByName(const std::string& _name)
	{
		return static_cast<int>(m_processor.getController().getParameterIndexByName(_name));
	}

	bool Editor::bindParameter(juce::Button& _target, int _parameterIndex)
	{
		m_binding.bind(_target, _parameterIndex);
		return true;
	}

	bool Editor::bindParameter(juce::ComboBox& _target, int _parameterIndex)
	{
		m_binding.bind(_target, _parameterIndex);
		return true;
	}

	bool Editor::bindParameter(juce::Slider& _target, int _parameterIndex)
	{
		m_binding.bind(_target, _parameterIndex);
		return true;
	}

	juce::Value* Editor::getParameterValue(int _parameterIndex, uint8_t _part)
	{
		return m_processor.getController().getParamValueObject(_parameterIndex, _part);
	}
}

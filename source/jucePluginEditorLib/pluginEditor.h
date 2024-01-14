#pragma once

#include "../juceUiLib/editor.h"

#include "../synthLib/buildconfig.h"

namespace pluginLib
{
	class ParameterBinding;
}

namespace jucePluginEditorLib
{
	namespace patchManager
	{
		class PatchManager;
	}

	class Processor;

	class Editor : public genericUI::Editor, genericUI::EditorInterface
	{
	public:
		enum class FileType
		{
			Syx,
			Mid
		};

		Editor(Processor& _processor, pluginLib::ParameterBinding& _binding, std::string _skinFolder);
		~Editor() override;

		Editor(const Editor&) = delete;
		Editor(Editor&&) = delete;
		Editor& operator = (const Editor&) = delete;
		Editor& operator = (Editor&&) = delete;

		virtual const char* findResourceByFilename(const std::string& _filename, uint32_t& _size) = 0;

		void loadPreset(const std::function<void(const juce::File&)>& _callback);
		void savePreset(const std::function<void(const juce::File&)>& _callback);
#if !SYNTHLIB_DEMO_MODE
		bool savePresets(FileType _type, const std::string& _pathName, const std::vector<std::vector<uint8_t>>& _presets) const;
#endif
		static std::string createValidFilename(FileType& _type, const juce::File& _file);

		virtual std::pair<std::string, std::string> getDemoRestrictionText() const = 0;

		void showDemoRestrictionMessageBox() const;

		Processor& getProcessor() const { return m_processor; }

		void setPatchManager(patchManager::PatchManager* _patchManager);

		patchManager::PatchManager* getPatchManager() const
		{
			return m_patchManager.get();
		}

		void setPerInstanceConfig(const std::vector<uint8_t>& _data) override;
		void getPerInstanceConfig(std::vector<uint8_t>& _data) override;

	private:
		const char* getResourceByFilename(const std::string& _name, uint32_t& _dataSize) override;
		int getParameterIndexByName(const std::string& _name) override;
		bool bindParameter(juce::Button& _target, int _parameterIndex) override;
		bool bindParameter(juce::ComboBox& _target, int _parameterIndex) override;
		bool bindParameter(juce::Slider& _target, int _parameterIndex) override;
		juce::Value* getParameterValue(int _parameterIndex, uint8_t _part) override;

		Processor& m_processor;
		pluginLib::ParameterBinding& m_binding;

		const std::string m_skinFolder;

		std::map<std::string, std::vector<char>> m_fileCache;

		std::unique_ptr<juce::FileChooser> m_fileChooser;
		std::unique_ptr<patchManager::PatchManager> m_patchManager;
		std::vector<uint8_t> m_instanceConfig;
	};
}

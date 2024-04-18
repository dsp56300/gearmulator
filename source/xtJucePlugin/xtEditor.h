#pragma once

#include "../jucePluginEditorLib/pluginEditor.h"

class XtLcd;
class Controller;

namespace jucePluginEditorLib
{
	class FocusedParameter;
	class Processor;
}

namespace pluginLib
{
	class ParameterBinding;
}

namespace xtJucePlugin
{
	class FocusedParameter;
	class FrontPanel;
	class PatchManager;

	class Editor final : public jucePluginEditorLib::Editor
	{
	public:
		Editor(jucePluginEditorLib::Processor& _processor, pluginLib::ParameterBinding& _binding, std::string _skinFolder, const std::string& _jsonFilename);
		~Editor() override;

		Editor(Editor&&) = delete;
		Editor(const Editor&) = delete;
		Editor& operator = (Editor&&) = delete;
		Editor& operator = (const Editor&) = delete;

		static const char* findEmbeddedResource(const std::string& _filename, uint32_t& _size);
		const char* findResourceByFilename(const std::string& _filename, uint32_t& _size) override;
		std::pair<std::string, std::string> getDemoRestrictionText() const override;

		Controller& getXtController() const { return m_controller; }

		XtLcd* getLcd() const;
	private:
		void mouseEnter(const juce::MouseEvent& _event) override;

		Controller& m_controller;

		std::unique_ptr<FocusedParameter> m_focusedParameter;
		std::unique_ptr<FrontPanel> m_frontPanel;
	};
}

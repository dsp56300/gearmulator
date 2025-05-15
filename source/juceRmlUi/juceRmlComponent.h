#pragma once

#include "juce_gui_basics/juce_gui_basics.h"

#include "juce_opengl/juce_opengl.h"

namespace genericUI
{
	class Editor;
}

namespace Rml
{
	class Context;
}

namespace juceRmlUi
{
	class RenderInterface;
	class JuceRmlUi;

	class RmlComponent : public juce::Component, juce::OpenGLRenderer
	{
	public:
		RmlComponent(genericUI::Editor& _editor);
		~RmlComponent() override;

		void newOpenGLContextCreated() override;
		void renderOpenGL() override;
		void openGLContextClosing() override;
		void resized() override;

	private:
		genericUI::Editor& m_editor;
		std::vector<std::vector<uint8_t>> m_fonts;
		juce::OpenGLContext m_openGLContext;
		RenderInterface* m_renderInterface = nullptr;
		Rml::Context* m_rmlContext = nullptr;
	};
}

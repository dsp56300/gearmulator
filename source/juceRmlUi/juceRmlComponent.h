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

		void mouseDown(const juce::MouseEvent& _event) override;
		void mouseUp(const juce::MouseEvent& _event) override;
		void mouseMove(const juce::MouseEvent& _event) override;
		void mouseDrag(const juce::MouseEvent& _event) override;
		void mouseDoubleClick(const juce::MouseEvent& _event) override;
		void mouseWheelMove(const juce::MouseEvent& _event, const juce::MouseWheelDetails& _wheel) override;
		void mouseEnter(const juce::MouseEvent& _event) override;
		void mouseExit(const juce::MouseEvent& _event) override;
		bool keyPressed(const juce::KeyPress& _key) override;
		bool keyStateChanged(bool _isKeyDown) override;

	private:
		juce::Point<int> toRmlPosition(const juce::MouseEvent& _e) const;

		genericUI::Editor& m_editor;
		std::vector<std::vector<uint8_t>> m_fonts;
		juce::OpenGLContext m_openGLContext;
		RenderInterface* m_renderInterface = nullptr;
		Rml::Context* m_rmlContext = nullptr;
		std::vector<juce::KeyPress> m_pressedKeys;
	};
}

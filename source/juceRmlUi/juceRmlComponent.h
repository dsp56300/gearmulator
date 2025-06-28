#pragma once

#include "rmlInterfaces.h"
#include "rmlRendererProxy.h"

#include "juce_gui_basics/juce_gui_basics.h"

#include "juce_opengl/juce_opengl.h"

class RenderInterface_GL3;

namespace Rml
{
	class Context;
}

namespace juceRmlUi
{
	namespace gl2
	{
		class RendererGL2;
	}

	struct RmlInterfaces;
	class Renderer;
	class JuceRmlUi;

	class DataProvider;

	class RmlComponent final : public juce::Component, juce::OpenGLRenderer, juce::Timer
	{
	public:
		using DocumentCreatedCallback = std::function<void(RmlComponent&, Rml::ElementDocument*)>;

		explicit RmlComponent(DataProvider& _dataProvider, std::string _rootRmlFilename, float _contentScale = 1.0f);
		~RmlComponent() override;

		void newOpenGLContextCreated() override;
		void renderOpenGL() override;
		void openGLContextClosing() override;

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

		void timerCallback() override;

		RmlInterfaces& getInterfaces() const { return *m_rmlInterfaces; }

		void resized() override;
		void parentSizeChanged() override;

		Rml::ElementDocument* getDocument() const;

	private:
		juce::Point<int> toRmlPosition(const juce::MouseEvent& _e) const;
		void update();
		void createRmlContext();
		void destroyRmlContext();
		void updateRmlContextDimensions();

		DataProvider& m_dataProvider;
		const std::string m_rootRmlFilename;

		juce::OpenGLContext m_openGLContext;

		std::unique_ptr<RmlInterfaces> m_rmlInterfaces;

		std::unique_ptr<RenderInterface_GL3> m_renderInterface;
		std::unique_ptr<RendererProxy> m_renderProxy;

		Rml::Context* m_rmlContext = nullptr;

		std::vector<juce::KeyPress> m_pressedKeys;
		float m_contentScale = 1.0f;
		float m_currentRenderScale = 0.0f;

		std::mutex m_timerMutex;

		DocumentCreatedCallback m_onDocumentCreated;

		JUCE_DECLARE_NON_COPYABLE(RmlComponent)
		JUCE_DECLARE_NON_MOVEABLE(RmlComponent)
	};
}

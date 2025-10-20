#pragma once

#include "frameRateLimiter.h"
#include "juceRmlDrag.h"
#include "rmlInterfaces.h"
#include "rmlRendererProxy.h"
#include "baseLib/event.h"

#include "juce_gui_basics/juce_gui_basics.h"

#include "juce_opengl/juce_opengl.h"

namespace Rml
{
	class Context;
}

namespace juceRmlUi
{
	struct RmlInterfaces;
	class Renderer;
	class JuceRmlUi;

	class DataProvider;

	class RmlComponent final : public juce::Component, juce::OpenGLRenderer, juce::Timer, public juce::FileDragAndDropTarget, public juce::DragAndDropTarget, public juce::DragAndDropContainer
	{
	public:
		enum class ScreenshotState : uint8_t
		{
			NoScreenshot,
			RequestScreenshot,
			ScreenshotReady
		};

		using ScreenshotCallback = std::function<void(const juce::Image&)>;

		baseLib::Event<RmlComponent*> evPreUpdate;
		baseLib::Event<RmlComponent*> evPostUpdate;

		using ContextCreatedCallback = std::function<void(RmlComponent&, Rml::Context&)>;
		using DocumentLoadFailedCallback = std::function<void(RmlComponent&, Rml::Context&)>;
		using DocumentCreatedCallback = std::function<void(RmlComponent&, Rml::ElementDocument*)>;

		explicit RmlComponent(RmlInterfaces& _interfaces, DataProvider& _dataProvider, std::string _rootRmlFilename, float _contentScale, const ContextCreatedCallback& _contextCreatedCallback, const DocumentLoadFailedCallback& _docLoadFailedCallback);
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
		void modifierKeysChanged(const juce::ModifierKeys& _modifiers) override;
		void focusLost(FocusChangeType cause) override;

		void timerCallback() override;

		RmlInterfaces& getInterfaces() const { return m_rmlInterfaces; }

		void resized() override;
		void parentSizeChanged() override;

		Rml::ElementDocument* getDocument() const;
		Rml::Context* getContext() const;

		void addPostFrameCallback(std::function<void()> _callback)
		{
			m_postFrameCallbacks.emplace_back(std::move(_callback));
		}

		bool isInterestedInFileDrag(const juce::StringArray& _files) override;
		void fileDragEnter(const juce::StringArray& _files, int _x, int _y) override;
		void fileDragMove(const juce::StringArray& _files, int _x, int _y) override;
		void fileDragExit(const juce::StringArray& _files) override;
		void filesDropped(const juce::StringArray& _files, int _x, int _y) override;
		bool isInterestedInDragSource(const SourceDetails& _dragSourceDetails) override;
		void itemDragEnter(const SourceDetails& _dragSourceDetails) override;
		void itemDragExit(const SourceDetails& _dragSourceDetails) override;
		void itemDropped(const SourceDetails& _dragSourceDetails) override;
		bool shouldDropFilesWhenDraggedExternally(const SourceDetails& _sourceDetails, juce::StringArray& _files, bool& _canMoveFiles) override;

		juce::Point<int> toRmlPosition(const juce::MouseEvent& _e) const;
		juce::Point<int> toRmlPosition(int _x, int _y) const;

		Rml::Vector2i getDocumentSize() const { return m_documentSize; }

		void resize(int _width, int _height);

		static void requestUpdate(const Rml::Element* _elem);

		static RmlComponent* fromElement(const Rml::Element* _element);

		void enqueueUpdate();

		void enableDebugger(bool _enable);

		bool takeScreenshot(const ScreenshotCallback& _callback);

	private:
		void update();
		void createRmlContext(const ContextCreatedCallback& _contextCreatedCallback);
		void destroyRmlContext();
		void updateRmlContextDimensions();

		Rml::Vector2i getRenderSize() const;

		int toRmlModifiers(const juce::MouseEvent& _event);
		int toRmlModifiers(const juce::KeyPress& _event);
		int toRmlModifiers(const juce::ModifierKeys& _mods);

		RmlInterfaces& m_rmlInterfaces;
		Rml::CoreInstance& m_coreInstance;
		DataProvider& m_dataProvider;
		const std::string m_rootRmlFilename;

		juce::OpenGLContext m_openGLContext;

		std::unique_ptr<Rml::RenderInterface> m_renderInterface;
		std::unique_ptr<RendererProxy> m_renderProxy;

		Rml::Context* m_rmlContext = nullptr;
		Rml::ElementDocument* m_document = nullptr;

		std::vector<juce::KeyPress> m_pressedKeys;
		float m_contentScale = 1.0f;
		float m_currentRenderScale = 0.0f;

		std::mutex m_timerMutex;
		std::mutex m_contextRenderMutex;

		std::vector<std::function<void()>> m_postFrameCallbacks;
		std::vector<std::function<void()>> m_tempPostFrameCallbacks;

		DocumentCreatedCallback m_onDocumentCreated;

		RmlDrag m_drag;
		juce::ModifierKeys m_currentModifierKeys;

		bool m_updating = true;

		Rml::Vector2i m_documentSize{0,0};

		JUCE_DECLARE_NON_COPYABLE(RmlComponent)
		JUCE_DECLARE_NON_MOVEABLE(RmlComponent)

		double m_time = 0;
		float m_fps = 0;

		uint32_t m_pendingUpdates = 0;
		std::atomic<bool> m_renderDone;
		double m_nextFrameTime;

		bool m_debuggerActive = false;

		uint32_t m_openGLversion = 0;
		uint32_t m_lastOpenGLversion = 0;

		juce::Image m_screenshot;
		ScreenshotState m_screenshotState = ScreenshotState::NoScreenshot;
		ScreenshotCallback m_screenshotCallback;

		bool m_mouseActive = false;
	};
}

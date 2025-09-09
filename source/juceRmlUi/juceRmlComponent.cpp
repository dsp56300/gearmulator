#include "juceRmlComponent.h"

#include <cassert>

#include "rmlDataProvider.h"
#include "rmlHelper.h"
#include "rmlInterfaces.h"

#include "RmlUi_Renderer_GL3.h"

#include "baseLib/filesystem.h"

#include "juceUiLib/messageBox.h"

#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/Core.h"
#include "RmlUi/Core/ElementDocument.h"
#include "RmlUi/Debugger/Debugger.h"

namespace juceRmlUi
{
	RmlComponent::RmlComponent(RmlInterfaces& _interfaces, DataProvider& _dataProvider, std::string _rootRmlFilename, const float _contentScale/* = 1.0f*/, const ContextCreatedCallback& _contextCreatedCallback, const DocumentLoadFailedCallback& _docLoadFailedCallback)
		: m_rmlInterfaces(_interfaces)
		, m_coreInstance(_interfaces.getCoreInstance())
		, m_dataProvider(_dataProvider)
		, m_rootRmlFilename(std::move(_rootRmlFilename))
		, m_contentScale(_contentScale)
		, m_drag(*this)
		, m_nextFrameTime(_interfaces.getSystemInterface().GetElapsedTime())
	{
		m_renderProxy.reset(new RendererProxy(m_coreInstance, m_dataProvider));

		m_openGLContext.setMultisamplingEnabled(true);
		m_openGLContext.setRenderer(this);
		m_openGLContext.setComponentPaintingEnabled(false);
		m_openGLContext.attachTo(*this);
		m_openGLContext.setContinuousRepainting(false);

		// this is optional on Win/Linux (but doesn't hurt), but required on macOS to get a core profile, not get a compatibility profile
		m_openGLContext.setOpenGLVersionRequired(juce::OpenGLContext::openGL4_1);

		setWantsKeyboardFocus(true);
		// set some reasonable default size, correct size will be set when loading the RML document
		setSize(1280, 720);

		{
			RmlInterfaces::ScopedAccess access(*this);

			const auto files = m_dataProvider.getAllFilenames();

			for (const auto & file : files)
			{
				if (baseLib::filesystem::hasExtension(file, ".ttf"))
				{
					Rml::Log::Message(Rml::Log::LT_INFO, "Loading font face from file %s", file.c_str());
					Rml::LoadFontFace(m_coreInstance, file, true);
				}
			}
		}

		try
		{
			createRmlContext(_contextCreatedCallback);
		}
		catch (std::runtime_error& e)
		{
			Rml::Log::Message(Rml::Log::LT_ERROR, "%s", e.what());

			m_openGLContext.detach();
			_docLoadFailedCallback(*this, *m_rmlContext);
			destroyRmlContext();
			deleteAllChildren();
			throw;
		}

		m_drag.onDocumentLoaded();

		m_updating = false;
		enqueueUpdate();
	}

	RmlComponent::~RmlComponent()
	{
		m_openGLContext.detach();
		destroyRmlContext();

		deleteAllChildren();
	}

	void RmlComponent::newOpenGLContextCreated()
	{
		RmlInterfaces::ScopedAccess access(*this);

		m_openGLContext.setSwapInterval(1);

		m_renderInterface.reset(new RenderInterface_GL3(m_coreInstance));
		m_renderProxy->setRenderer(m_renderInterface.get());

		{
			std::scoped_lock lock(m_timerMutex);
			startTimer(1);
		}
	}

	void RmlComponent::renderOpenGL()
	{
		{
			// although we set that we render only manually, juce still calls this function eventhough we didn't
			// request a repaint, for example when the window is resized.
			// This results in massive flickering if the render queue is empty so we ask RmlUi to just do a
			// render for us from the OpenGL thread
			std::scoped_lock lock(m_contextRenderMutex);
			if (!m_renderProxy->hasRenderFunctions())
			{
				m_rmlContext->Render();
				m_renderProxy->finishFrame();
			}
		}

		using namespace juce::gl;

        GLint viewport[4];
        glGetIntegerv(GL_VIEWPORT, viewport);

		const Rml::Vector2i size{viewport[2], viewport[3]};

		glDisable(GL_DEBUG_OUTPUT);
		glDisable(GL_DEBUG_OUTPUT_SYNCHRONOUS);

		m_renderInterface->SetViewport(size.x, size.y);
		m_renderInterface->BeginFrame();
		bool haveMore = true;
		while (haveMore)
			haveMore = m_renderProxy->executeRenderFunctions();
		m_renderInterface->EndFrame();

		GLenum err = glGetError();
		if (err != GL_NO_ERROR)
		DBG("OpenGL error: " << juce::String::toHexString((int)err));

		const auto t = m_rmlInterfaces.getSystemInterface().GetElapsedTime();

		const auto dt = m_nextFrameTime - t;
		const auto remainingMillis = static_cast<int>(dt * 1000.0f);

		m_renderDone = true;

		std::scoped_lock lockTimer(m_timerMutex);

		if (remainingMillis > 0)
			startTimer(remainingMillis);
		else
			startTimer(1);
	}

	void RmlComponent::openGLContextClosing()
	{
		m_renderProxy->setRenderer(nullptr);
		m_renderInterface.reset();
	}

	void RmlComponent::mouseDown(const juce::MouseEvent& _event)
	{
		Component::mouseDown(_event);
		RmlInterfaces::ScopedAccess access(*this);
		m_rmlContext->ProcessMouseButtonDown(static_cast<int>(helper::toRmlMouseButton(_event)), toRmlModifiers(_event));
		enqueueUpdate();
	}

	void RmlComponent::mouseUp(const juce::MouseEvent& _event)
	{
		Component::mouseUp(_event);
		RmlInterfaces::ScopedAccess access(*this);
		m_rmlContext->ProcessMouseButtonUp(static_cast<int>(helper::toRmlMouseButton(_event)), toRmlModifiers(_event));
		enqueueUpdate();
	}

	void RmlComponent::mouseMove(const juce::MouseEvent& _event)
	{
		Component::mouseMove(_event);
		RmlInterfaces::ScopedAccess access(*this);

		const auto pos = toRmlPosition(_event);
		m_rmlContext->ProcessMouseMove(pos.x, pos.y, toRmlModifiers(_event));
		enqueueUpdate();
	}

	void RmlComponent::mouseDrag(const juce::MouseEvent& _event)
	{
		Component::mouseDrag(_event);
		RmlInterfaces::ScopedAccess access(*this);

		const auto pos = toRmlPosition(_event);

		m_rmlContext->ProcessMouseMove(pos.x, pos.y, toRmlModifiers(_event));

		// forward out-of-bounds drag events to the drag handler to allow it to convert to a juce drag if the drag source can export files
		if (pos.x < 0 || pos.y < 0 || pos.x >= m_rmlContext->GetDimensions().x || pos.y >= m_rmlContext->GetDimensions().y)
			m_drag.processOutOfBoundsDrag(pos);

		enqueueUpdate();
	}

	void RmlComponent::mouseExit(const juce::MouseEvent& _event)
	{
		Component::mouseExit(_event);
		RmlInterfaces::ScopedAccess access(*this);
		if (m_rmlContext)
			m_rmlContext->ProcessMouseLeave();
		enqueueUpdate();
	}

	void RmlComponent::mouseEnter(const juce::MouseEvent& _event)
	{
		Component::mouseEnter(_event);
		RmlInterfaces::ScopedAccess access(*this);

		const auto pos = toRmlPosition(_event);
		m_rmlContext->ProcessMouseMove(pos.x, pos.y, toRmlModifiers(_event));
		enqueueUpdate();
	}

	void RmlComponent::mouseWheelMove(const juce::MouseEvent& _event, const juce::MouseWheelDetails& _wheel)
	{
		Component::mouseWheelMove(_event, _wheel);

		RmlInterfaces::ScopedAccess access(*this);

		// wheel direction is right/down for positive values, in juce its the other way around, thats why we flip if NOT reversed
		const auto deltaX = _wheel.isReversed ? _wheel.deltaX : -_wheel.deltaX;
		const auto deltaY = _wheel.isReversed ? _wheel.deltaY : -_wheel.deltaY;

		m_rmlContext->ProcessMouseWheel(Rml::Vector2f(deltaX, deltaY), toRmlModifiers(_event));
		enqueueUpdate();
	}

	void RmlComponent::mouseDoubleClick(const juce::MouseEvent& _event)
	{
		Component::mouseDoubleClick(_event);
		// this confuses rml as it has its own double click handling. This causes a double click plus another single click to be triggered
/*
		RmlInterfaces::ScopedAccess access(*this);

		m_rmlContext->ProcessMouseButtonDown(helper::toRmlMouseButton(_event), toRmlModifiers(_event));
		enqueueUpdate();
*/	}

	bool RmlComponent::keyPressed(const juce::KeyPress& _key)
	{
		RmlInterfaces::ScopedAccess access(*this);

		m_pressedKeys.push_back(_key);

		bool res = false;

		auto juceChar = _key.getTextCharacter();
		if (juce::CharacterFunctions::isPrintable(juceChar) || juceChar == '\n')
		{
			auto string = juce::String::charToString(juceChar);
			m_rmlContext->ProcessTextInput(string.toStdString());
			res = true;
		}

		const Rml::Input::KeyIdentifier key = helper::toRmlKey(_key);

		if (key != Rml::Input::KI_UNKNOWN)
		{
			m_rmlContext->ProcessKeyDown(key, toRmlModifiers(_key));
			res = true;
		}
		enqueueUpdate();
		return res;
	}

	bool RmlComponent::keyStateChanged(const bool _isKeyDown)
	{
		// this API is so WTF...

		bool res = false;

		if (!_isKeyDown)
		{
			RmlInterfaces::ScopedAccess access(*this);

			if (m_rmlContext)
			{
				for (auto it = m_pressedKeys.begin(); it != m_pressedKeys.end();)
				{
					if (!it->isCurrentlyDown())
					{
						const auto& key = *it;
						m_rmlContext->ProcessKeyUp(helper::toRmlKey(key), toRmlModifiers(key));
						enqueueUpdate();
						res = true;
						it = m_pressedKeys.erase(it);
					}
					else
					{
						++it;
					}
				}
			}
		}
		if (res)
			return res;
		return Component::keyStateChanged(_isKeyDown);
	}

	void RmlComponent::modifierKeysChanged(const juce::ModifierKeys& _modifiers)
	{
		Component::modifierKeysChanged(_modifiers);

		const auto changes = _modifiers.getRawFlags() - m_currentModifierKeys.getRawFlags();

		if (!changes)
			return;

		m_currentModifierKeys = _modifiers;

		RmlInterfaces::ScopedAccess access(*this);

		// generate a fake key event to trigger the modifier change
		if (changes > 0)
			m_rmlContext->ProcessKeyDown(Rml::Input::KI_UNKNOWN, toRmlModifiers(_modifiers));
		else
			m_rmlContext->ProcessKeyUp(Rml::Input::KI_UNKNOWN, toRmlModifiers(_modifiers));
		enqueueUpdate();
	}

	void RmlComponent::timerCallback()
	{
		{
			std::scoped_lock lock(m_timerMutex);
			stopTimer();
		}
		m_renderDone = true;
		update();
	}

	juce::Point<int> RmlComponent::toRmlPosition(const juce::MouseEvent& _e) const
	{
		return toRmlPosition(_e.x, _e.y);
	}

	juce::Point<int> RmlComponent::toRmlPosition(int _x, int _y) const
	{
		return {
			juce::roundToInt(static_cast<float>(_x) * m_openGLContext.getRenderingScale()), 
			juce::roundToInt(static_cast<float>(_y) * m_openGLContext.getRenderingScale())
		};
	}

	void RmlComponent::resize(const int _width, const int _height)
	{
		setSize(_width, _height);
		m_renderDone = true;
		m_updating = false;
		update();
	}

	RmlComponent* RmlComponent::fromElement(const Rml::Element* _element)
	{
		if (!_element)
			return nullptr;
		auto* doc = _element->GetOwnerDocument();
		if (!doc)
			return nullptr;
		auto* p = doc->GetAttribute<void*>("rmlComponent", nullptr);
		return static_cast<RmlComponent*>(p);
	}

	void RmlComponent::update()
	{
		RmlInterfaces::ScopedAccess access(*this);

		if (m_updating || !m_renderDone || !m_renderInterface)
			return;

		m_updating = true;

		updateRmlContextDimensions();

		evPreUpdate(this);

		{
			std::scoped_lock lock(m_contextRenderMutex);

			m_rmlContext->Update();
			m_rmlContext->Render();

			m_renderProxy->finishFrame();
		}

		evPostUpdate(this);

		const auto t = m_rmlInterfaces.getSystemInterface().GetElapsedTime();
		const auto dt = static_cast<float>(t - m_time);
		m_time = t;

		auto fps = (dt > 0) ? (1.0f / dt) : 0.0f;
		m_fps += (fps - m_fps) * 0.1f;

//		LOG("FPS: " << m_fps << ", next update delay " << m_rmlContext->GetNextUpdateDelay());

		if (m_pendingUpdates > 0)
		{
			--m_pendingUpdates;
			m_nextFrameTime = 0; // force immediate update
		}
		else
		{
			// render every 0.5 seconds if there is no update pending
			m_rmlContext->RequestNextUpdate(0.5f);
			m_nextFrameTime = t + m_rmlContext->GetNextUpdateDelay();
		}

		// ensure that new post frame callbacks that are added by other post frame callbacks are executed in the next frame
		std::swap(m_postFrameCallbacks, m_tempPostFrameCallbacks);

		for (const auto& postFrameCallback : m_tempPostFrameCallbacks)
			postFrameCallback();
		m_tempPostFrameCallbacks.clear();

		m_updating = false;

		// trigger a repaint and wait for OpenGL to be done with it
		m_renderDone = false;
		m_openGLContext.triggerRepaint();

		// just in case the repaint does not happen, we set a timer to ensure we get a new update
		std::scoped_lock lock(m_timerMutex);
		startTimer(500);
	}

	void RmlComponent::enqueueUpdate()
	{
		// One is not enough, because RmlUi might do property changes that in turn require another update. We do three to be sure.
		m_pendingUpdates = 3;
		if (m_renderDone)
			update();
	}

	void RmlComponent::createRmlContext(const ContextCreatedCallback& _contextCreatedCallback)
	{
		const auto size = getScreenBounds();

		if (size.isEmpty())
			return;

		{
			RmlInterfaces::ScopedAccess access(*this);

			if (m_rmlContext)
				return;

			m_rmlContext = CreateContext(m_coreInstance, getName().toStdString(), {size.getWidth(), size.getHeight()}, m_renderProxy.get(), nullptr);

			m_rmlContext->SetDensityIndependentPixelRatio(static_cast<float>(m_openGLContext.getRenderingScale()) * m_contentScale);

			m_rmlContext->SetDefaultScrollBehavior(Rml::ScrollBehavior::Smooth, 5.0f);

			if (_contextCreatedCallback)
				_contextCreatedCallback(*this, *m_rmlContext);

			auto& sys = m_rmlInterfaces.getSystemInterface();
			sys.beginLogRecording();

			m_document = m_rmlContext->LoadDocument(m_rootRmlFilename);

			if (m_document)
			{
				m_document->SetAttribute("rmlComponent", static_cast<void*>(this));

				const Rml::Vector2f s = m_document->GetBox().GetSize(Rml::BoxArea::Margin);

				if (s.x > 0 && s.y > 0)
				{
					m_documentSize.x = static_cast<int>(s.x);
					m_documentSize.y = static_cast<int>(s.y);
				}
				else
					throw std::runtime_error("RMLUI document '" + m_rootRmlFilename + "' has no valid size, explicit default size needs to be specified on the <body> element.");
				m_document->Show();
			}
			else
			{
				throw std::runtime_error("Failed to load RMLUI document: " + m_rootRmlFilename);
			}

			sys.endLogRecording();

			const auto logs = sys.getRecordedLogEntries();

			if (!logs.empty())
			{
				std::stringstream ss;
				ss << "Errors while loading RMLUI document '" << m_rootRmlFilename << "':\n\n";
				for (const auto& log : logs)
					ss << '[' << SystemInterface::logTypeToString(log.first) << "]: " << log.second << '\n';
				genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning, "Error loading RMLUI document", ss.str(), this);
			}

			if (m_document->GetAttribute("debugger", 0))
			{
				Rml::Debugger::Initialise(m_rmlContext);
				Rml::Debugger::SetVisible(true);
			}
		}

		setSize(m_documentSize.x, m_documentSize.y);
	}

	void RmlComponent::destroyRmlContext()
	{
		{
			std::scoped_lock lock(m_timerMutex);
			stopTimer();
		}

		RmlInterfaces::ScopedAccess access(*this);

		m_rmlContext->UnloadAllDocuments();
		Rml::RemoveContext(m_coreInstance, m_rmlContext->GetName());
		m_rmlContext = nullptr;

		Rml::ReleaseRenderManagers(m_coreInstance);
	}

	void RmlComponent::updateRmlContextDimensions()
	{
		if (!m_rmlContext)
			return;

		auto contextDims = m_rmlContext->GetDimensions();

		const auto size = getRenderSize();

		const float renderScale = static_cast<float>(size.x) / static_cast<float>(m_documentSize.x);// * static_cast<float>(m_openGLContext.getRenderingScale());

		if (contextDims.x != size.x || contextDims.y != size.y || m_currentRenderScale != renderScale)
		{
			m_currentRenderScale = renderScale;
			m_rmlContext->SetDensityIndependentPixelRatio(renderScale * m_contentScale);
			m_rmlContext->SetDimensions({ size.x, size.y });
		}
	}

	Rml::Vector2i RmlComponent::getRenderSize() const
	{
		const auto s = m_openGLContext.getRenderingScale();
		const auto b = getLocalBounds();
		return { static_cast<int>(b.getWidth() * s), static_cast<int>(b.getHeight() * s) };
	}

	int RmlComponent::toRmlModifiers(const juce::MouseEvent& _event)
	{
		return toRmlModifiers(_event.mods);
	}

	int RmlComponent::toRmlModifiers(const juce::KeyPress& _event)
	{
		return toRmlModifiers(_event.getModifiers());
	}

	int RmlComponent::toRmlModifiers(const juce::ModifierKeys& _mods)
	{
		m_currentModifierKeys = _mods;
		return helper::toRmlModifiers(_mods);
	}

	void RmlComponent::resized()
	{
		{
			RmlInterfaces::ScopedAccess access(*this);
			updateRmlContextDimensions();

			// enqueueUpdate might cause rendering immediately, we want the update to happen *after* the resize so we queue a timer to do the update
			std::scoped_lock lock(m_timerMutex);
			startTimer(1);
		}
		Component::resized();
	}

	void RmlComponent::parentSizeChanged()
	{
		Component::parentSizeChanged();
	}

	Rml::ElementDocument* RmlComponent::getDocument() const
	{
		return m_document;
	}

	Rml::Context* RmlComponent::getContext() const
	{
		return m_rmlContext;
	}

	bool RmlComponent::isInterestedInFileDrag(const juce::StringArray& _files)
	{
		RmlInterfaces::ScopedAccess access(*this);
		return m_drag.isInterestedInFileDrag(_files);
	}

	void RmlComponent::fileDragEnter(const juce::StringArray& _files, const int _x, const int _y)
	{
		RmlInterfaces::ScopedAccess access(*this);
		m_drag.fileDragEnter(_files, _x, _y);
	}

	void RmlComponent::fileDragMove(const juce::StringArray& _files, const int _x, const int _y)
	{
		RmlInterfaces::ScopedAccess access(*this);
		m_drag.fileDragMove(_files, _x, _y);
	}

	void RmlComponent::fileDragExit(const juce::StringArray& _files)
	{
		RmlInterfaces::ScopedAccess access(*this);
		m_drag.fileDragExit(_files);
	}

	void RmlComponent::filesDropped(const juce::StringArray& _files, int _x, int _y)
	{
		RmlInterfaces::ScopedAccess access(*this);
		m_drag.filesDropped(_files, _x, _y);
	}

	bool RmlComponent::isInterestedInDragSource(const SourceDetails& _dragSourceDetails)
	{
		RmlInterfaces::ScopedAccess access(*this);
		return m_drag.isInterestedInDragSource(_dragSourceDetails);
	}

	void RmlComponent::itemDragEnter(const SourceDetails& _dragSourceDetails)
	{
		RmlInterfaces::ScopedAccess access(*this);
		m_drag.itemDragEnter(_dragSourceDetails);
	}

	void RmlComponent::itemDragExit(const SourceDetails& _dragSourceDetails)
	{
		RmlInterfaces::ScopedAccess access(*this);
		m_drag.itemDragExit(_dragSourceDetails);
	}

	void RmlComponent::itemDropped(const SourceDetails& _dragSourceDetails)
	{
		RmlInterfaces::ScopedAccess access(*this);
		m_drag.itemDropped(_dragSourceDetails);
	}

	bool RmlComponent::shouldDropFilesWhenDraggedExternally(const SourceDetails& _sourceDetails, juce::StringArray& _files, bool& _canMoveFiles)
	{
		return m_drag.shouldDropFilesWhenDraggedExternally(_sourceDetails, _files, _canMoveFiles);
	}
}

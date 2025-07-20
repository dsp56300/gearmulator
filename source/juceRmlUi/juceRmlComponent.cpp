#include "juceRmlComponent.h"

#include <cassert>

#include "rmlDataProvider.h"
#include "rmlHelper.h"
#include "rmlInterfaces.h"

#include "RmlUi_Renderer_GL3.h"

#include "baseLib/filesystem.h"

#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/Core.h"
#include "RmlUi/Core/ElementDocument.h"
#include "RmlUi/Debugger/Debugger.h"

namespace juceRmlUi
{
	RmlComponent::RmlComponent(DataProvider& _dataProvider, std::string _rootRmlFilename, const float _contentScale/* = 1.0f*/, const ContextCreatedCallback& _contextCreatedCallback)
		: m_dataProvider(_dataProvider)
		, m_rootRmlFilename(std::move(_rootRmlFilename))
		, m_contentScale(_contentScale)
		, m_drag(*this)
	{
		m_renderProxy.reset(new RendererProxy(m_dataProvider));

		m_openGLContext.setMultisamplingEnabled(true);
		m_openGLContext.setRenderer(this);
		m_openGLContext.attachTo(*this);
		m_openGLContext.setContinuousRepainting(true);
		m_openGLContext.setSwapInterval(1);

		// this is ootional on Win/Linux (but doesn't hurt), but required on macOS to not get a compatibility profile
		m_openGLContext.setOpenGLVersionRequired(juce::OpenGLContext::openGL4_1);

		setWantsKeyboardFocus(true);

		m_rmlInterfaces.reset(new RmlInterfaces(m_dataProvider));

		// set some reasonable default size, correct size will be set when loading the RML document
		setSize(1280, 720);

		{
			RmlInterfaces::ScopedAccess access(*this);

			const auto files = m_dataProvider.getAllFilenames();

			for (const auto & file : files)
			{
				if (baseLib::filesystem::hasExtension(file, ".ttf"))
					Rml::LoadFontFace(file, true);
			}
		}

		createRmlContext(_contextCreatedCallback);

		m_drag.onDocumentLoaded();
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

		m_renderInterface.reset(new RenderInterface_GL3());
		m_renderProxy->setRenderer(m_renderInterface.get());

		{
			std::scoped_lock lock(m_timerMutex);
			startTimer(1);
		}
	}

	void RmlComponent::renderOpenGL()
	{
		using namespace juce::gl;

		int width = getScreenBounds().getWidth();
		int height = getScreenBounds().getHeight();

		glDisable(GL_DEBUG_OUTPUT);
        glDisable(GL_DEBUG_OUTPUT_SYNCHRONOUS);

		m_renderInterface->SetViewport(width, height);
		m_renderInterface->BeginFrame();
		m_renderProxy->executeRenderFunctions();
		m_renderInterface->EndFrame();

		GLenum err = glGetError();
		if (err != GL_NO_ERROR)
			DBG("OpenGL error: " << juce::String::toHexString((int)err));

		std::scoped_lock lock(m_timerMutex);
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
		if (!m_rmlContext)
			return;
		m_rmlContext->ProcessMouseButtonDown(helper::toRmlMouseButton(_event), toRmlModifiers(_event));
	}

	void RmlComponent::mouseUp(const juce::MouseEvent& _event)
	{
		Component::mouseUp(_event);
		RmlInterfaces::ScopedAccess access(*this);
		if (!m_rmlContext)
			return;
		m_rmlContext->ProcessMouseButtonUp(helper::toRmlMouseButton(_event), toRmlModifiers(_event));
	}

	void RmlComponent::mouseMove(const juce::MouseEvent& _event)
	{
		Component::mouseMove(_event);
		RmlInterfaces::ScopedAccess access(*this);
		if (!m_rmlContext)
			return;

		const auto pos = toRmlPosition(_event);
		m_rmlContext->ProcessMouseMove(pos.x, pos.y, toRmlModifiers(_event));
	}

	void RmlComponent::mouseDrag(const juce::MouseEvent& _event)
	{
		Component::mouseDrag(_event);
		RmlInterfaces::ScopedAccess access(*this);
		if (!m_rmlContext)
			return;
		const auto pos = toRmlPosition(_event);

		m_rmlContext->ProcessMouseMove(pos.x, pos.y, toRmlModifiers(_event));

		// forward out-of-bounds drag events to the drag handler to allow it to convert to a juce drag if the drag source can export files
		if (pos.x < 0 || pos.y < 0 || pos.x >= m_rmlContext->GetDimensions().x || pos.y >= m_rmlContext->GetDimensions().y)
			m_drag.processOutOfBoundsDrag(pos);
	}

	void RmlComponent::mouseExit(const juce::MouseEvent& _event)
	{
		Component::mouseExit(_event);
		RmlInterfaces::ScopedAccess access(*this);
		if (m_rmlContext)
			m_rmlContext->ProcessMouseLeave();
	}

	void RmlComponent::mouseEnter(const juce::MouseEvent& _event)
	{
		Component::mouseEnter(_event);
		RmlInterfaces::ScopedAccess access(*this);
		if (!m_rmlContext)
			return;
		const auto pos = toRmlPosition(_event);
		m_rmlContext->ProcessMouseMove(pos.x, pos.y, toRmlModifiers(_event));
	}

	void RmlComponent::mouseWheelMove(const juce::MouseEvent& _event, const juce::MouseWheelDetails& _wheel)
	{
		Component::mouseWheelMove(_event, _wheel);

		RmlInterfaces::ScopedAccess access(*this);
		if (!m_rmlContext)
			return;
		m_rmlContext->ProcessMouseWheel(Rml::Vector2f(-_wheel.deltaX, -_wheel.deltaY), toRmlModifiers(_event));
	}

	void RmlComponent::mouseDoubleClick(const juce::MouseEvent& _event)
	{
		Component::mouseDoubleClick(_event);
		// this confuses rml as it has its own double click handling. This causes a double click plus another single click to be triggered
/*
		RmlInterfaces::ScopedAccess access(*this);

		if (!m_rmlContext)
			return;
		m_rmlContext->ProcessMouseButtonDown(helper::toRmlMouseButton(_event), toRmlModifiers(_event));
*/	}

	bool RmlComponent::keyPressed(const juce::KeyPress& _key)
	{
		RmlInterfaces::ScopedAccess access(*this);

		if (!m_rmlContext)
			return Component::keyPressed(_key);

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

		if (!m_rmlContext)
			return;

		// generate a fake key event to trigger the modifier change
		if (changes > 0)
			m_rmlContext->ProcessKeyDown(Rml::Input::KI_UNKNOWN, toRmlModifiers(_modifiers));
		else
			m_rmlContext->ProcessKeyUp(Rml::Input::KI_UNKNOWN, toRmlModifiers(_modifiers));
	}

	void RmlComponent::timerCallback()
	{
		{
			std::scoped_lock lock(m_timerMutex);
			stopTimer();
		}
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

	void RmlComponent::update()
	{
		RmlInterfaces::ScopedAccess access(*this);

		if (!m_rmlContext)
			return;

		updateRmlContextDimensions();

		m_rmlContext->Update();
		m_rmlContext->Render();
		m_renderProxy->finishFrame();

		// ensure that new post frame callbacks that are added by other post frame callbacks are executed in the next frame
		std::swap(m_postFrameCallbacks, m_tempPostFrameCallbacks);

		for (const auto& postFrameCallback : m_tempPostFrameCallbacks)
			postFrameCallback();
		m_tempPostFrameCallbacks.clear();
	}

	void RmlComponent::createRmlContext(const ContextCreatedCallback& _contextCreatedCallback)
	{
		const auto size = getScreenBounds();

		if (size.isEmpty())
			return;

		Rml::Vector2i documentSize;

		{
			RmlInterfaces::ScopedAccess access(*this);

			if (m_rmlContext)
				return;

			m_rmlContext = CreateContext(getName().toStdString(), {size.getWidth(), size.getHeight()}, m_renderProxy.get(), nullptr);

			m_rmlContext->SetDensityIndependentPixelRatio(static_cast<float>(m_openGLContext.getRenderingScale()) * m_contentScale);

			m_rmlContext->SetDefaultScrollBehavior(Rml::ScrollBehavior::Smooth, 3.0f);

			if (_contextCreatedCallback)
				_contextCreatedCallback(*this, *m_rmlContext);

			auto& sys = m_rmlInterfaces->getSystemInterface();
			sys.beginLogRecording();

			auto* document = m_rmlContext->LoadDocument(m_rootRmlFilename);
			if (document)
			{
				const Rml::Vector2f s = document->GetBox().GetSize(Rml::BoxArea::Margin);

				if (s.x > 0 && s.y > 0)
				{
					documentSize.x = static_cast<int>(s.x);
					documentSize.y = static_cast<int>(s.y);
				}
				else
					throw std::runtime_error("RMLUI document '" + m_rootRmlFilename + "' has no valid size, explicit default size needs to be specified on the <body> element.");
				document->Show();
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
				juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Error loading RMLUI document", ss.str(), this);
			}

			Rml::Debugger::Initialise(m_rmlContext);
			Rml::Debugger::SetVisible(true);
		}

		setSize(documentSize.x, documentSize.y);
	}

	void RmlComponent::destroyRmlContext()
	{
		RmlInterfaces::ScopedAccess access(*this);

		if (!m_rmlContext)
			return;

		m_rmlContext->UnloadAllDocuments();
		Rml::RemoveContext(m_rmlContext->GetName());
		m_rmlContext = nullptr;

		Rml::ReleaseRenderManagers();
	}

	void RmlComponent::updateRmlContextDimensions()
	{
		if (!m_rmlContext)
			return;

		auto contextDims = m_rmlContext->GetDimensions();

		const float renderScale = static_cast<float>(m_openGLContext.getRenderingScale());

		const int width = getScreenBounds().getWidth();
		const int height = getScreenBounds().getHeight();

		if (contextDims.x != width || contextDims.y != height || m_currentRenderScale != renderScale)
		{
			m_currentRenderScale = renderScale;
			m_rmlContext->SetDensityIndependentPixelRatio(renderScale * m_contentScale);
			m_rmlContext->SetDimensions({ width, height });
		}
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
		Component::resized();

		RmlInterfaces::ScopedAccess access(*this);
		updateRmlContextDimensions();
	}

	void RmlComponent::parentSizeChanged()
	{
		Component::parentSizeChanged();
		RmlInterfaces::ScopedAccess access(*this);
		updateRmlContextDimensions();
	}

	Rml::ElementDocument* RmlComponent::getDocument() const
	{
		return m_rmlContext ? m_rmlContext->GetDocument(0) : nullptr;
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

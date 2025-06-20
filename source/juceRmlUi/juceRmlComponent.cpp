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
	RmlComponent::RmlComponent(DataProvider& _dataProvider, std::string _rootRmlFilename, const float _contentScale/* = 1.0f*/, const DocumentCreatedCallback& _onDocumentCreated/* = {}*/)
		: m_dataProvider(_dataProvider)
		, m_rootRmlFilename(std::move(_rootRmlFilename))
		, m_contentScale(_contentScale)
		, m_onDocumentCreated(_onDocumentCreated)
	{
		m_renderProxy.reset(new RendererProxy(m_dataProvider));

		m_openGLContext.setMultisamplingEnabled(true);
		m_openGLContext.setRenderer(this);
		m_openGLContext.attachTo(*this);
		m_openGLContext.setContinuousRepainting(true);
		m_openGLContext.setSwapInterval(1);

		setWantsKeyboardFocus(true);

		m_rmlInterfaces.reset(new RmlInterfaces(m_dataProvider));

		RmlInterfaces::ScopedAccess access(*this);

		const auto files = m_dataProvider.getAllFilenames();

		for (const auto & file : files)
		{
			if (baseLib::filesystem::hasExtension(file, ".ttf"))
				Rml::LoadFontFace(file, true);
		}
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
		m_rmlContext->ProcessMouseButtonDown(helper::toRmlMouseButton(_event), helper::toRmlModifiers(_event));
	}

	void RmlComponent::mouseUp(const juce::MouseEvent& _event)
	{
		Component::mouseUp(_event);
		RmlInterfaces::ScopedAccess access(*this);
		if (!m_rmlContext)
			return;
		m_rmlContext->ProcessMouseButtonUp(helper::toRmlMouseButton(_event), helper::toRmlModifiers(_event));
	}

	void RmlComponent::mouseMove(const juce::MouseEvent& _event)
	{
		Component::mouseMove(_event);
		RmlInterfaces::ScopedAccess access(*this);
		if (!m_rmlContext)
			return;

		const auto pos = toRmlPosition(_event);
		m_rmlContext->ProcessMouseMove(pos.x, pos.y, helper::toRmlModifiers(_event));
	}

	void RmlComponent::mouseDrag(const juce::MouseEvent& _event)
	{
		Component::mouseDrag(_event);
		RmlInterfaces::ScopedAccess access(*this);
		if (!m_rmlContext)
			return;
		const auto pos = toRmlPosition(_event);
		m_rmlContext->ProcessMouseMove(pos.x, pos.y, helper::toRmlModifiers(_event));
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
		m_rmlContext->ProcessMouseMove(pos.x, pos.y, helper::toRmlModifiers(_event));
	}

	void RmlComponent::mouseWheelMove(const juce::MouseEvent& _event, const juce::MouseWheelDetails& _wheel)
	{
		Component::mouseWheelMove(_event, _wheel);

		RmlInterfaces::ScopedAccess access(*this);
		if (!m_rmlContext)
			return;
		m_rmlContext->ProcessMouseWheel(Rml::Vector2f(-_wheel.deltaX, -_wheel.deltaY), helper::toRmlModifiers(_event));
	}

	void RmlComponent::mouseDoubleClick(const juce::MouseEvent& _event)
	{
		Component::mouseDoubleClick(_event);
		// this confuses rml as it has its own double click handling. This causes a double click plus another single click to be triggered
/*
		RmlInterfaces::ScopedAccess access(*this);

		if (!m_rmlContext)
			return;
		m_rmlContext->ProcessMouseButtonDown(helper::toRmlMouseButton(_event), helper::toRmlModifiers(_event));
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
			m_rmlContext->ProcessKeyDown(key, helper::toRmlModifiers(_key));
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
						m_rmlContext->ProcessKeyUp(helper::toRmlKey(key), helper::toRmlModifiers(key));
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
		return {
			juce::roundToInt(static_cast<float>(_e.x) * m_openGLContext.getRenderingScale()), 
			juce::roundToInt(static_cast<float>(_e.y) * m_openGLContext.getRenderingScale())
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
	}

	void RmlComponent::createRmlContext()
	{
		const auto size = getScreenBounds();

		if (size.isEmpty())
			return;

		RmlInterfaces::ScopedAccess access(*this);

		if (m_rmlContext)
			return;

		m_rmlContext = CreateContext(getName().toStdString(), {size.getWidth(), size.getHeight()}, m_renderProxy.get(), nullptr);

		m_rmlContext->SetDensityIndependentPixelRatio(static_cast<float>(m_openGLContext.getRenderingScale()) * m_contentScale);

		auto& sys = m_rmlInterfaces->getSystemInterface();
		sys.beginLogRecording();

        auto* document = m_rmlContext->LoadDocument(m_rootRmlFilename);
        if (document)
        {
			if (m_onDocumentCreated)
				m_onDocumentCreated(*this, document);
	        document->Show();
        }
		else
		{
			assert(false);
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

//		Rml::Debugger::Initialise(m_rmlContext);
//		Rml::Debugger::SetVisible(true);
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

	void RmlComponent::resized()
	{
		Component::resized();
		createRmlContext();

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
}

#include "juceRmlComponent.h"

#include <cassert>

#include "rmlDataProvider.h"
#include "rmlDocuments.h"
#include "rmlHelper.h"
#include "rmlInterfaces.h"
#include "rmlRenderInterface.h"

#include "baseLib/filesystem.h"

#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/Core.h"
#include "RmlUi/Core/ElementDocument.h"

namespace juceRmlUi
{
	RmlComponent::RmlComponent(DataProvider& _dataProvider, std::string _rootRmlFilename) : m_dataProvider(_dataProvider), m_rootRmlFilename(std::move(_rootRmlFilename))
	{
		m_openGLContext.setMultisamplingEnabled(true);
		m_openGLContext.setRenderer(this);
		m_openGLContext.attachTo(*this);
		m_openGLContext.setContinuousRepainting(true);
//		m_openGLContext.setSwapInterval(1);
		m_openGLContext.setComponentPaintingEnabled(false);

		setWantsKeyboardFocus(true);

		auto label = new juce::Label("JUCE LABEL", "JUCE LABEL");
		label->setFont(juce::Font(20.0f));
		label->setJustificationType(juce::Justification::centred);
		label->setBounds(200, 300, 500, 150);
		addAndMakeVisible(label);

		m_rmlInterfaces.reset(new RmlInterfaces(m_dataProvider));

		RmlInterfaces::ScopedAccess access(*m_rmlInterfaces);

		const auto files = m_dataProvider.getAllFilenames();

		for (const auto & file : files)
		{
			if (baseLib::filesystem::hasExtension(file, ".ttf"))
				Rml::LoadFontFace(file, true);
		}
	}

	RmlComponent::~RmlComponent()
	{
		deleteAllChildren();
		m_openGLContext.detach();
	}

	void RmlComponent::newOpenGLContextCreated()
	{
		RmlInterfaces::ScopedAccess access(*m_rmlInterfaces);

		m_renderInterface.reset(new RenderInterface(m_dataProvider));

		const auto size = getScreenBounds();

		m_rmlContext = CreateContext(getName().toStdString(), {size.getWidth(), size.getHeight()}, m_renderInterface.get(), nullptr);

        auto* document = m_rmlContext->LoadDocument(m_rootRmlFilename);
        if (document)
	        document->Show();
		else
			assert(false);
	}

	void RmlComponent::renderOpenGL()
	{
		juce::OpenGLHelpers::clear(juce::Colours::grey);

		using namespace juce::gl;

		RmlInterfaces::ScopedAccess access(*m_rmlInterfaces);
		if (!m_rmlContext)
			return;
		
		auto contextDims = m_rmlContext->GetDimensions();

		int width = getScreenBounds().getWidth();
		int height = getScreenBounds().getHeight();

		const float invContentScale = 1.0f / m_contentScale;

		if (contextDims.x != width || contextDims.y != height)
		{
			m_rmlContext->SetDimensions({ static_cast<int>(static_cast<float>(width) * invContentScale), static_cast<int>(static_cast<float>(height) * invContentScale)});
			m_rmlContext->SetDensityIndependentPixelRatio(5.0f);
		}

		glViewport(0, 0, width, height);

		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glOrtho(0, static_cast<float>(width) * invContentScale, static_cast<float>(height) * invContentScale, 0, -1, 1);

		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();

		glDisable(GL_DEPTH_TEST);
		glDisable(GL_LIGHTING);
		glDisable(GL_CULL_FACE);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		m_rmlContext->Update();

		glDisable(GL_DEBUG_OUTPUT);
        glDisable(GL_DEBUG_OUTPUT_SYNCHRONOUS);

		m_renderInterface->beginFrame(width, height);
		m_rmlContext->Render();
		m_renderInterface->endFrame();

		GLenum err = glGetError();
		if (err != GL_NO_ERROR)
			DBG("OpenGL error: " << juce::String::toHexString((int)err));
	}

	void RmlComponent::openGLContextClosing()
	{
		RmlInterfaces::ScopedAccess access(*m_rmlInterfaces);
		if (m_rmlContext)
		{
			m_rmlContext->UnloadAllDocuments();
			Rml::RemoveContext(m_rmlContext->GetName());
			m_rmlContext = nullptr;

			Rml::ReleaseRenderManagers();
		}
		m_renderInterface.reset();
	}

	void RmlComponent::mouseDown(const juce::MouseEvent& _event)
	{
		Component::mouseDown(_event);
		RmlInterfaces::ScopedAccess access(*m_rmlInterfaces);
		if (!m_rmlContext)
			return;
		m_rmlContext->ProcessMouseButtonDown(helper::toRmlMouseButton(_event), helper::toRmlModifiers(_event));
	}

	void RmlComponent::mouseUp(const juce::MouseEvent& _event)
	{
		Component::mouseUp(_event);
		RmlInterfaces::ScopedAccess access(*m_rmlInterfaces);
		if (!m_rmlContext)
			return;
		m_rmlContext->ProcessMouseButtonUp(helper::toRmlMouseButton(_event), helper::toRmlModifiers(_event));
	}

	void RmlComponent::mouseMove(const juce::MouseEvent& _event)
	{
		Component::mouseMove(_event);
		RmlInterfaces::ScopedAccess access(*m_rmlInterfaces);
		if (!m_rmlContext)
			return;

		const auto pos = toRmlPosition(_event);
		m_rmlContext->ProcessMouseMove(pos.x, pos.y, helper::toRmlModifiers(_event));
	}

	void RmlComponent::mouseDrag(const juce::MouseEvent& _event)
	{
		Component::mouseDrag(_event);
		RmlInterfaces::ScopedAccess access(*m_rmlInterfaces);
		if (!m_rmlContext)
			return;
		const auto pos = toRmlPosition(_event);
		m_rmlContext->ProcessMouseMove(pos.x, pos.y, helper::toRmlModifiers(_event));
	}

	void RmlComponent::mouseExit(const juce::MouseEvent& _event)
	{
		Component::mouseExit(_event);
		RmlInterfaces::ScopedAccess access(*m_rmlInterfaces);
		if (m_rmlContext)
			m_rmlContext->ProcessMouseLeave();
	}

	void RmlComponent::mouseEnter(const juce::MouseEvent& _event)
	{
		Component::mouseEnter(_event);
		RmlInterfaces::ScopedAccess access(*m_rmlInterfaces);
		if (!m_rmlContext)
			return;
		const auto pos = toRmlPosition(_event);
		m_rmlContext->ProcessMouseMove(pos.x, pos.y, helper::toRmlModifiers(_event));
	}

	void RmlComponent::mouseWheelMove(const juce::MouseEvent& _event, const juce::MouseWheelDetails& _wheel)
	{
		Component::mouseWheelMove(_event, _wheel);

		RmlInterfaces::ScopedAccess access(*m_rmlInterfaces);
		if (!m_rmlContext)
			return;
		m_rmlContext->ProcessMouseWheel(Rml::Vector2f(_wheel.deltaX, _wheel.deltaY), helper::toRmlModifiers(_event));
	}

	void RmlComponent::mouseDoubleClick(const juce::MouseEvent& _event)
	{
		Component::mouseDoubleClick(_event);

		RmlInterfaces::ScopedAccess access(*m_rmlInterfaces);

		if (!m_rmlContext)
			return;
		m_rmlContext->ProcessMouseButtonDown(helper::toRmlMouseButton(_event), helper::toRmlModifiers(_event));
	}

	bool RmlComponent::keyPressed(const juce::KeyPress& _key)
	{
		RmlInterfaces::ScopedAccess access(*m_rmlInterfaces);

		if (!m_rmlContext)
			return Component::keyPressed(_key);

		const Rml::Input::KeyIdentifier key = helper::toRmlKey(_key);

		if (key == Rml::Input::KI_UNKNOWN)
			return Component::keyPressed(_key);

		m_pressedKeys.push_back(_key);

		return m_rmlContext->ProcessKeyDown(key, helper::toRmlModifiers(_key));
	}

	bool RmlComponent::keyStateChanged(const bool _isKeyDown)
	{
		// this API is so WTF...

		bool res = false;

		if (!_isKeyDown)
		{
			RmlInterfaces::ScopedAccess access(*m_rmlInterfaces);

			if (m_rmlContext)
			{
				for (auto it = m_pressedKeys.begin(); it != m_pressedKeys.end();)
				{
					if (!it->isCurrentlyDown())
					{
						const auto& key = *it;
						res |= m_rmlContext->ProcessKeyUp(helper::toRmlKey(key), helper::toRmlModifiers(key));
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

	juce::Point<int> RmlComponent::toRmlPosition(const juce::MouseEvent& _e) const
	{
		return {
			juce::roundToInt(static_cast<float>(_e.x) * m_openGLContext.getRenderingScale() / m_contentScale), 
			juce::roundToInt(static_cast<float>(_e.y) * m_openGLContext.getRenderingScale() / m_contentScale)
		};
	}
}

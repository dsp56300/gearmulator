#include "parameterOverlay.h"
#include "parameterOverlays.h"
#include "pluginEditor.h"
#include "imagePool.h"

#include "../jucePluginLib/parameter.h"
#include "../juceUiLib/uiObjectStyle.h"

namespace jucePluginEditorLib
{
	ParameterOverlay::ParameterOverlay(ParameterOverlays& _overlays, juce::Component* _component) : m_overlays(_overlays), m_component(_component)
	{
	}

	ParameterOverlay::~ParameterOverlay()
	{
		delete m_imageLock;
		setParameter(nullptr);
	}

	void ParameterOverlay::onBind(const pluginLib::ParameterBinding::BoundParameter& _parameter)
	{
		setParameter(_parameter.parameter);
	}

	void ParameterOverlay::onUnbind(const pluginLib::ParameterBinding::BoundParameter&)
	{
		setParameter(nullptr);
	}

	ParameterOverlay::OverlayProperties ParameterOverlay::getOverlayProperties() const
	{
		OverlayProperties result;

		const auto& editor = m_overlays.getEditor();

		auto& root = editor.getRootObject();
		const auto& props = m_component->getProperties();

		result.scale = root.getPropertyFloat("overlayScale", 0.2f);
		result.scale = props.getWithDefault("overlayScale", result.scale);

		std::string color = root.getProperty("overlayColor", "ffffffff");
		color = props.getWithDefault("overlayColor", juce::String(color)).toString().toStdString();
		genericUI::UiObjectStyle::parseColor(result.color, color);

		result.position.x = root.getPropertyFloat("overlayPosX", 0.0f);
		result.position.y = root.getPropertyFloat("overlayPosY", 0.0f);

		result.position.x = props.getWithDefault("overlayPosX", result.position.x);
		result.position.y = props.getWithDefault("overlayPosY", result.position.y);

		return result;
	}

	void ParameterOverlay::toggleOverlay(juce::DrawableImage*& _image, ImagePool::Type _type, bool _enable) const
	{
		if(_enable)
		{
			const auto props = getOverlayProperties();

			auto updatePosition = [&]()
			{
				_image->setCentrePosition(static_cast<int>(props.position.x) + m_component->getPosition().x, static_cast<int>(props.position.y) + m_component->getPosition().y);
			};

			if(!_image)
			{
				auto& editor = m_overlays.getEditor();

				auto* image = editor.getImagePool().getImage(_type, props.scale);

				if(image)
				{
					_image = new juce::DrawableImage(*image);

//					_image->setOverlayColour(props.color);	// juce cannot do it, it does not multiply but replaced the color entirely
					_image->setInterceptsMouseClicks(false, false);
					_image->setAlwaysOnTop(true);

					m_component->getParentComponent()->addAndMakeVisible(_image);
				}
			}
			else
			{
				_image->setVisible(true);
			}

			updatePosition();
		}
		else if(_image)
		{
			_image->setVisible(false);
		}
	}

	void ParameterOverlay::updateOverlays()
	{
		const auto isLocked = m_parameter != nullptr && m_parameter->isLocked();
		const auto isLinked = m_parameter != nullptr && (m_parameter->getLinkState() & pluginLib::Source);

		toggleOverlay(m_imageLock, ImagePool::Type::Lock, isLocked);
		toggleOverlay(m_imageLock, ImagePool::Type::Link, isLinked);
	}

	void ParameterOverlay::setParameter(pluginLib::Parameter* _parameter)
	{
		if(m_parameter == _parameter)
			return;

		if(m_parameter)
		{
			m_parameter->onLockedChanged.removeListener(m_parameterLockChangedListener);
			m_parameter->onLinkStateChanged.removeListener(m_parameterLinkChangedListener);
			m_parameterLockChangedListener = InvalidListenerId;
			m_parameterLinkChangedListener = InvalidListenerId;
		}

		m_parameter = _parameter;

		if(m_parameter)
		{
			m_parameterLockChangedListener = m_parameter->onLockedChanged.addListener([this](pluginLib::Parameter*, const bool&)
			{
				updateOverlays();
			});
			m_parameterLinkChangedListener = m_parameter->onLinkStateChanged.addListener([this](pluginLib::Parameter*, const pluginLib::ParameterLinkType&)
			{
				updateOverlays();
			});
		}

		updateOverlays();
	}
}

#include "parameterOverlay.h"
#include "parameterOverlays.h"
#include "pluginEditor.h"
#include "imagePool.h"

#include "jucePluginLib/parameter.h"
#include "juceUiLib/uiObjectStyle.h"

namespace jucePluginEditorLib
{
	ParameterOverlay::ParameterOverlay(ParameterOverlays& _overlays, juce::Component* _component) : m_overlays(_overlays), m_component(_component)
	{
	}

	ParameterOverlay::~ParameterOverlay()
	{
		setParameter(nullptr);

		for (const auto* image : m_images)
			delete image;
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

	void ParameterOverlay::toggleOverlay(ImagePool::Type _type, const bool _enable, float _opacity/* = 1.0f*/)
	{
		juce::DrawableImage*& drawableImage = m_images[static_cast<uint32_t>(_type)];

		if(_enable)
		{
			const auto props = getOverlayProperties();

			auto updatePosition = [&]()
			{
				const auto x = static_cast<int>(props.position.x) + m_component->getPosition().x;
				const auto y = static_cast<int>(props.position.y) + m_component->getPosition().y;
				const auto w = drawableImage->getWidth();
				const auto h = drawableImage->getHeight();

				drawableImage->setBoundingBox(juce::Rectangle(x - (w>>1), y - (h>>1), w, h).toFloat());
			};

			if(!drawableImage)
			{
				auto& editor = m_overlays.getEditor();

				auto* image = editor.getImagePool().getImage(_type, props.scale);

				if(image)
				{
					drawableImage = new juce::DrawableImage(*image);

//					_image->setOverlayColour(props.color);	// juce cannot do it, it does not multiply but replaced the color entirely
					drawableImage->setInterceptsMouseClicks(false, false);
					drawableImage->setAlwaysOnTop(true);
					m_component->getParentComponent()->addAndMakeVisible(drawableImage);
				}
			}
			else
			{
				drawableImage->setVisible(true);
			}

			drawableImage->setOpacity(_opacity);

			updatePosition();
		}
		else if(drawableImage)
		{
			drawableImage->setVisible(false);
		}
	}

	void ParameterOverlay::updateOverlays()
	{
		if(m_component->getParentComponent() == nullptr)
			return;

		const auto isLocked = m_parameter != nullptr && m_parameter->isLocked();
		const auto isLinkSource = m_parameter != nullptr && (m_parameter->getLinkState() & pluginLib::Source);
		const auto isLinkTarget = m_parameter != nullptr && (m_parameter->getLinkState() & pluginLib::Target);

		const auto linkAlpha = isLinkSource ? 1.0f : 0.5f;

		toggleOverlay(ImagePool::Type::Lock, isLocked);
		toggleOverlay(ImagePool::Type::Link, isLinkSource || isLinkTarget, linkAlpha);

		std::array<juce::DrawableImage*, OverlayCount> visibleOverlays;

		uint32_t count = 0;
		int totalWidth = 0;

		for (auto* image : m_images)
		{
			if(image && image->isVisible())
			{
				visibleOverlays[count++] = image;
				totalWidth += image->getWidth();
			}
		}

		if(count <= 1)
			return;

		const auto avgWidth = totalWidth / count;

		int x = -static_cast<int>(totalWidth >> 1) + static_cast<int>(avgWidth >> 1) + static_cast<int>(visibleOverlays[0]->getBoundingBox().topLeft.x);

		for(uint32_t i=0; i<count; ++i)
		{
			auto bounds = visibleOverlays[i]->getBoundingBox();
			const auto w = bounds.getWidth();
			const auto fx = static_cast<float>(x);
			bounds.topLeft.x = fx;
			bounds.bottomLeft.x = fx;
			bounds.topRight.x = fx + w;
			visibleOverlays[i]->setBoundingBox(bounds);

			x += visibleOverlays[i]->getWidth();
		}
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

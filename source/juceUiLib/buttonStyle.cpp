#include "buttonStyle.h"

#include "uiObject.h"

namespace genericUI
{
	void ButtonStyle::apply(Editor& _editor, const UiObject& _object)
	{
		UiObjectStyle::apply(_editor, _object);

		m_isToggle = _object.getPropertyInt("isToggle") != 0;
		m_radioGroupId = _object.getPropertyInt("radioGroupId");

		if(!m_drawable)
			return;

		juce::Drawable** imageDrawables[] = {
			&m_normalImage,
			&m_overImage,
			&m_downImage,
			&m_disabledImage,
			&m_normalImageOn,
			&m_overImageOn,
			&m_downImageOn,
			&m_disabledImageOn
		};

		const char* properties[] = {
			"normalImage",
			"overImage",
			"downImage",
			"disabledImage",
			"normalImageOn",
			"overImageOn",
			"downImageOn",
			"disabledImageOn"
		};

		static_assert(std::size(imageDrawables) == std::size(properties), "arrays must have same size");

		const bool isVerticalTiling = m_tileSizeY <= (m_drawable->getHeight()>>1);

		std::map<int, juce::Drawable*> drawables;

		const auto targetW = _object.getPropertyInt("width", 0);
		const auto targetH = _object.getPropertyInt("height", 0);

		for(size_t i=0; i<std::size(imageDrawables); ++i)
		{
			const auto imageIndex = _object.getPropertyInt(properties[i], -1);

			if(imageIndex < 0)
				continue;

			const auto it = drawables.find(imageIndex);

			if(it != drawables.end())
			{
				*imageDrawables[i] = it->second;
			}
			else
			{
				const auto needsScale = targetW && targetH && (targetW != m_tileSizeX || targetH != m_tileSizeY);
				juce::Drawable* d = m_drawable;
				if(needsScale || imageIndex > 0)
				{
					m_createdDrawables.emplace_back(d->createCopy());
					d = m_createdDrawables.back().get();
					if(isVerticalTiling)
						d->setOriginWithOriginalSize({0, static_cast<float>(-imageIndex * m_tileSizeY)});
					else
						d->setOriginWithOriginalSize({static_cast<float>(-imageIndex * m_tileSizeX), 0.0f});
				}

				*imageDrawables[i] = d;

				drawables.insert(std::make_pair(imageIndex, d));

				if(needsScale)
				{
					auto* d = *imageDrawables[i];
					const auto scaleX = static_cast<float>(targetW) / static_cast<float>(m_tileSizeX);
					const auto scaleY = static_cast<float>(targetH) / static_cast<float>(m_tileSizeY);
					d->setTransform(d->getTransform().scaled(scaleX, scaleY));
				}
			}
		}
	}

	void ButtonStyle::apply(juce::DrawableButton& _button) const
	{
		_button.setColour(juce::DrawableButton::ColourIds::backgroundColourId, juce::Colours::transparentBlack);
        _button.setColour(juce::DrawableButton::ColourIds::backgroundOnColourId, juce::Colours::transparentBlack);

		_button.setClickingTogglesState(m_isToggle);

		if(m_radioGroupId)
			_button.setRadioGroupId(m_radioGroupId);

		_button.setImages(m_normalImage, m_overImage, m_downImage, m_disabledImage, m_normalImageOn, m_overImageOn, m_downImageOn, m_disabledImageOn);
	}
}

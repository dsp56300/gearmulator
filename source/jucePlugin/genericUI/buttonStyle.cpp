#include "buttonStyle.h"

#include "uiObject.h"

namespace genericUI
{
	void ButtonStyle::apply(Editor& _editor, const UiObject& _object)
	{
		UiObjectStyle::apply(_editor, _object);

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

		std::map<int, juce::Drawable*> drawables;

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
				juce::Drawable* d = m_drawable;
				if(imageIndex > 0)
				{
					m_createdDrawables.emplace_back(d->createCopy());
					d = m_createdDrawables.back().get();
					d->setOriginWithOriginalSize({0, static_cast<float>(-imageIndex * m_tileSizeY)});
				}
				*imageDrawables[i] = d;
				drawables.insert(std::make_pair(imageIndex, d));
			}
		}
	}

	void ButtonStyle::setImages(juce::DrawableButton& _button) const
	{
		_button.setImages(m_normalImage, m_overImage, m_downImage, m_disabledImage, m_normalImageOn, m_overImageOn, m_downImageOn, m_disabledImageOn);
	}
}

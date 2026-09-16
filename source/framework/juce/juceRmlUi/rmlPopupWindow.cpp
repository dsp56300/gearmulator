#include "rmlPopupWindow.h"

#include <memory>

#include "juceRmlComponent.h"
#include "rmlInterfaces.h"

#include "juce_gui_basics/juce_gui_basics.h"

namespace juceRmlUi::popupWindow
{
	namespace
	{
		// Compact popup rows with a fixed palette shared across documents.
		class PopupLookAndFeel final : public juce::LookAndFeel_V4
		{
		public:
			PopupLookAndFeel()
			{
				setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff252b30));
				setColour(juce::PopupMenu::textColourId, juce::Colour(0xffdce2e6));
				setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff3a4650));
				setColour(juce::PopupMenu::highlightedTextColourId, juce::Colour(0xffffffff));
			}

			juce::Font getPopupMenuFont() override
			{
				return juce::Font(13.0f);
			}
		};

		// One instance serves every open popup and goes away with the last one - deliberately,
		// rather than a plain function-local static, which would be destroyed after juce has
		// already shut down.
		std::shared_ptr<juce::LookAndFeel> getLookAndFeel()
		{
			static std::weak_ptr<juce::LookAndFeel> cache;
			auto laf = cache.lock();
			if (!laf)
			{
				laf = std::make_shared<PopupLookAndFeel>();
				cache = laf;
			}
			return laf;
		}
	}

	void show(juce::PopupMenu& _menu, RmlComponent& _component, const Rml::Vector2f& _position,
	          const Rml::Vector2f& _size, std::function<void(int)> _onResult)
	{
		// Held by the completion callback below, which is what keeps it alive while the menu
		// window uses it.
		auto lookAndFeel = getLookAndFeel();
		_menu.setLookAndFeel(lookAndFeel.get());

		// Element coordinates are in the context's pixels, i.e. the component's logical size times
		// the render scale; the popup wants screen coordinates in logical pixels.
		const auto scale = _component.getOpenGLRenderingScale();
		const juce::Rectangle<int> local(
			juce::roundToInt(_position.x / scale), juce::roundToInt(_position.y / scale),
			juce::roundToInt(_size.x / scale), juce::roundToInt(_size.y / scale));
		const auto screenArea = _component.localAreaToGlobal(local);

		// With a target component, juce swallows a click inside it while the list is open and only
		// then closes the list. Without one, the click that closes the list also reaches the element
		// that opened it and opens it again. Set before the screen area, which it would otherwise
		// replace with the whole component's bounds.
		const auto options = juce::PopupMenu::Options()
			.withTargetComponent(&_component)
			.withTargetScreenArea(screenArea)
			.withMinimumWidth(screenArea.getWidth())
			.withStandardItemHeight(22)
			// one column, scrolling: a long list such as buffer sizes stays a list
			.withMaximumNumColumns(1)
			.withDeletionCheck(_component);

		juce::Component::SafePointer<RmlComponent> component(&_component);
		_menu.showMenuAsync(options, [component, lookAndFeel, onResult = std::move(_onResult)](const int _result)
		{
			if (_result <= 0 || !component || !onResult)
				return;
			RmlInterfaces::ScopedAccess access(*component);
			onResult(_result);
			if (component)
				component->enqueueUpdate();
		});
	}
}

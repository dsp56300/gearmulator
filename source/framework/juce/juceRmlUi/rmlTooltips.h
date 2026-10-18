#pragma once

#include <string>

#include "baseLib/event.h"

#include "RmlUi/Core/EventListener.h"
#include "RmlUi/Core/ObserverPtr.h"

namespace Rml
{
	class Element;
	class ElementDocument;
}

namespace juceRmlUi
{
	class RmlComponent;

	// The title attribute of whatever the pointer rests on, shown in a div of class "tooltip" under it
	// once it has stayed there a moment. The element with the attribute counts as one place, children
	// included, so moving onto the icon inside a button does not start the wait again. The text is read
	// every update, so a button that changes what it does can change its title and the tooltip follows.
	// Pressing anything puts it away until the pointer moves on to somewhere else.
	class Tooltips final : Rml::EventListener
	{
	public:
		static constexpr double DefaultDelaySeconds = 0.6;

		Tooltips(RmlComponent& _component, Rml::ElementDocument& _document, double _delaySeconds = DefaultDelaySeconds);
		~Tooltips() override;

		Tooltips(const Tooltips&) = delete;
		Tooltips& operator = (const Tooltips&) = delete;

	private:
		void ProcessEvent(Rml::Event& _event) override;

		void onPreUpdate();
		void show(const std::string& _text);
		void hide();

		RmlComponent& m_component;
		Rml::ObserverPtr<Rml::Element> m_document;
		const double m_delaySeconds;

		baseLib::EventListener<RmlComponent*> m_onPreUpdate;

		Rml::ObserverPtr<Rml::Element> m_source;
		Rml::ObserverPtr<Rml::Element> m_tooltip;
		std::string m_text;
		double m_since = 0.0;
		bool m_dismissed = false;
	};
}

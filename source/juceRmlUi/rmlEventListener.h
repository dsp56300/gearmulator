#pragma once

#include "RmlUi/Core/EventListener.h"

namespace juceRmlUi
{
	class EventListener : Rml::EventListener
	{
	public:
		EventListener(Rml::Element* _element, Rml::EventId _event, const std::function<void(Rml::Event&)>& _callback);
		~EventListener() override;

		void ProcessEvent(Rml::Event& _event) override;

		void OnDetach(Rml::Element*) override;

		static void Add(Rml::Element* _element, const Rml::EventId _event, const std::function<void(Rml::Event&)>& _callback)
		{
			new EventListener(_element, _event, _callback);
		}

		static void Add(const Rml::ElementPtr& _element, const Rml::EventId _event, const std::function<void(Rml::Event&)>& _callback)
		{
			Add(_element.get(), _event, _callback);
		}
	private:
		Rml::Element* m_element;
		const Rml::EventId m_event;
		std::function<void(Rml::Event&)> m_callback;
	};
}

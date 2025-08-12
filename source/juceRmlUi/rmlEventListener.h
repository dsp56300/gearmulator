#pragma once

#include "RmlUi/Core/EventListener.h"

namespace juceRmlUi
{
	class EventListener : Rml::EventListener
	{
	public:
		EventListener(Rml::Element* _element, Rml::EventId _event, const std::function<void(Rml::Event&)>& _callback, bool _inCapturePhase = false);
		~EventListener() override;

		void ProcessEvent(Rml::Event& _event) override;

		void OnDetach(Rml::Element*) override;

		static void Add(Rml::Element* _element, const Rml::EventId _event, const std::function<void(Rml::Event&)>& _callback, const bool _inCapturePhase = false)
		{
			new EventListener(_element, _event, _callback, _inCapturePhase);
		}

		static void Add(const Rml::ElementPtr& _element, const Rml::EventId _event, const std::function<void(Rml::Event&)>& _callback, const bool _inCapturePhase = false)
		{
			Add(_element.get(), _event, _callback, _inCapturePhase);
		}

		static void AddClick(Rml::Element* _element, const std::function<void()>& _callback)
		{
			Add(_element, Rml::EventId::Click, [_callback](Rml::Event& _event) { _callback(); });
		}

	private:
		Rml::Element* m_element;
		const Rml::EventId m_event;
		std::function<void(Rml::Event&)> m_callback;
		const bool m_inCapturePhase;
	};

	class OnDetachListener : public Rml::EventListener
	{
	public:
		using Callback = std::function<void(Rml::Element*)>;

		static void add(Rml::Element* _element, const Callback& _callback);

		void ProcessEvent(Rml::Event&) override {}

	private:
		OnDetachListener(Rml::Element* _element, Callback _callback);

		void OnDetach(Rml::Element*) override;

	private:
		Callback m_callback;
	};
}

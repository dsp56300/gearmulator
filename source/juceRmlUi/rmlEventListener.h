#pragma once

#include "baseLib/event.h"

#include "RmlUi/Core/EventListener.h"

namespace juceRmlUi
{
	class RmlComponent;
}

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

		~OnDetachListener() override;

		static void add(Rml::Element* _element, const Callback& _callback);

		void ProcessEvent(Rml::Event&) override {}

		Rml::Element* getElement() const { return m_element; }

		bool getAutoDestroy() const { return m_autoDestroy; }

	protected:
		OnDetachListener(Rml::Element* _element, Callback _callback, bool _autoDestroy = true);

		void OnDetach(Rml::Element*) override;

	private:
		Callback m_callback;
		Rml::Element* m_element;
		bool m_autoDestroy;
	};

	class ScopedListener : Rml::EventListener
	{
		using Callback = std::function<void(Rml::Event&)>;

	public:
		ScopedListener() = default;
		ScopedListener(Rml::Element* _elem, Rml::EventId _id, const Callback& _callback, bool _inCapturePhase = true);

		~ScopedListener() override;

		void add(Rml::Element* _elem, Rml::EventId _id, const Callback& _callback, bool _inCapturePhase = true);
		void reset();

	private:
		void ProcessEvent(Rml::Event& _event) override;

		Rml::Element* m_element = nullptr;
		Callback m_callback;
		Rml::EventId m_eventId;
		bool m_inCapturePhase = false;
	};

	class DelayedCall : OnDetachListener
	{
	public:
		DelayedCall(Rml::Element* _element, float _delaySeconds, const std::function<void()>& _callback, bool _autoDestroy = true);

		static DelayedCall* create(Rml::Element* _element, float _delaySeconds, const std::function<void()>& _callback, bool _autoDestroy = true);

		using OnDetachListener::getElement;

	private:
		void onUpdate();

		baseLib::EventListener<RmlComponent*> m_onUpdate;
		double m_targetTime;
		std::function<void()> m_callback;
	};
}

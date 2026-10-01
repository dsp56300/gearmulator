#pragma once

#include <algorithm>
#include <functional>
#include <memory>
#include <cassert>
#include <optional>
#include <vector>

#include "hybridcontainer.h"

namespace baseLib
{
	template<typename ...Ts>
	class Event
	{
	public:
		using ListenerId = size_t;
		using Callback = std::function<void(const Ts&...)>;
		using MyTuple = std::tuple<std::decay_t<Ts>...>;

		static constexpr ListenerId InvalidListenerId = ~0;

		Event() = default;

		// a copy has listeners of its own: removing one from it leaves the original's alone
		Event(const Event& _source)
			: m_hasRetainedValue(_source.m_hasRetainedValue)
			, m_retainedValue(_source.m_retainedValue)
		{
			m_listeners.reserve(_source.m_listeners.size());
			for (const auto& [id, listener] : _source.m_listeners)
				m_listeners.emplace_back(id, std::make_shared<Listener>(listener->callback));
		}

		Event(Event&&) noexcept = default;

		Event& operator = (const Event& _source)
		{
			if(&_source != this)
				*this = Event(_source);
			return *this;
		}

		Event& operator = (Event&& _source) noexcept
		{
			if(&_source == this)
				return *this;
			clear();
			m_listeners = std::move(_source.m_listeners);
			m_hasRetainedValue = _source.m_hasRetainedValue;
			m_retainedValue = std::move(_source.m_retainedValue);
			return *this;
		}

		~Event() = default;

		ListenerId addListener(const Callback& _callback)
		{
			// the listeners are sorted by id, the last one has the highest
			const ListenerId id = m_listeners.empty() ? 0 : m_listeners.back().first + 1;
			addListener(id, _callback);
			return id;
		}

		void addListener(ListenerId _id, const Callback& _callback)
		{
			const auto it = lowerBound(_id);
			if(it == m_listeners.end() || it->first != _id)
				m_listeners.emplace(it, _id, std::make_shared<Listener>(_callback));

			if(m_hasRetainedValue)
				std::apply(_callback, m_retainedValue);
		}

		void removeListener(const ListenerId _id)
		{
			const auto it = lowerBound(_id);
			if(it == m_listeners.end() || it->first != _id)
				return;
			it->second->removed = true;
			m_listeners.erase(it);
		}

		std::optional<Callback> getListener(const ListenerId _id) const
		{
			const auto it = lowerBound(_id);
			if(it != m_listeners.end() && it->first == _id)
				return it->second->callback;
			return {};
		}

		void clear()
		{
			for (const auto& it : m_listeners)
				it.second->removed = true;
			m_listeners.clear();
		}

		void invoke(const Ts& ..._args) const
		{
			// A listener may add and remove listeners while it is called, itself too. The ones to call are held here
			// meanwhile: one added waits for the next invoke, one removed before its turn is not called, it may be gone
			// with the object it belongs to
			HybridContainer<std::shared_ptr<Listener>, 8> listeners;
			listeners.reserve(m_listeners.size(), true);
			for (const auto& it : m_listeners)
				listeners.push_back(it.second);

			for (const auto& listener : listeners)
			{
				if (!listener->removed)
					listener->callback(_args...);
			}
		}

		void operator ()(const Ts& ..._args) const
		{
			invoke(_args...);
		}

		void retain(Ts ..._args)
		{
			invoke(_args...);
			m_hasRetainedValue = true;
			m_retainedValue = MyTuple(_args...);
		}

		void clearRetained()
		{
			m_hasRetainedValue = false;
		}

	private:
		struct Listener
		{
			explicit Listener(const Callback& _callback) : callback(_callback) {}

			Callback callback;
			bool removed = false;
		};

		using Listeners = std::vector<std::pair<ListenerId, std::shared_ptr<Listener>>>;

		typename Listeners::iterator lowerBound(const ListenerId _id)
		{
			return std::lower_bound(m_listeners.begin(), m_listeners.end(), _id, isBefore);
		}

		typename Listeners::const_iterator lowerBound(const ListenerId _id) const
		{
			return std::lower_bound(m_listeners.begin(), m_listeners.end(), _id, isBefore);
		}

		static bool isBefore(const typename Listeners::value_type& _listener, const ListenerId _id)
		{
			return _listener.first < _id;
		}

		Listeners m_listeners;

		bool m_hasRetainedValue = false;
		MyTuple m_retainedValue;
	};

	template<typename ...Ts>
	class EventListener
	{
	public:
		using MyEvent = Event<Ts...>;
		using MyCallback = typename MyEvent::Callback;
		using MyListenerId = typename MyEvent::ListenerId;

		static constexpr MyListenerId InvalidListenerId = MyEvent::InvalidListenerId;

		EventListener() = default;

		explicit EventListener(MyEvent& _event) : m_event(&_event), m_listenerId(InvalidListenerId)
		{
		}

		EventListener(MyEvent& _event, const MyCallback& _callback) : m_event(&_event), m_listenerId(_event.addListener(_callback))
		{
		}

		EventListener(EventListener&& _listener) noexcept : m_event(_listener.m_event), m_listenerId(_listener.m_listenerId)
		{
			_listener.m_listenerId = InvalidListenerId;
		}
		
		EventListener(const EventListener&) = delete;
		EventListener& operator = (const EventListener&) = delete;

		EventListener& operator = (EventListener&& _source) noexcept
		{
			if(&_source == this)
				return *this;

			removeListener();

			m_event = _source.m_event;
			m_listenerId = _source.m_listenerId;

			_source.m_listenerId = InvalidListenerId;

			return *this;
		}

		~EventListener()
		{
			removeListener();
		}

		void set(const MyCallback& _func)
		{
			removeListener();
			assert(m_event);
			if(m_event)
				m_listenerId = m_event->addListener(_func);
		}

		void set(MyEvent& _event, const MyCallback& _func)
		{
			removeListener();
			m_event = &_event;
			m_listenerId = _event.addListener(_func);
		}

		void set(MyEvent& _event)
		{
			if(&_event == m_event)
				return;

			if(isBound())
			{
				if(auto callback = m_event->getListener(m_listenerId))
				{
					m_event->removeListener(m_listenerId);
					m_listenerId = _event.addListener(*callback);
				}
				else
				{
					removeListener();
				}
			}

			m_event = &_event;
		}

		bool isBound() const { return m_listenerId != InvalidListenerId; }
		bool isValid() const { return m_event != nullptr; }

		EventListener& operator = (const MyCallback& _callback)
		{
			set(_callback);
			return *this;
		}

		EventListener& operator = (MyEvent& _event) noexcept
		{
			set(_event);
			return *this;
		}

		void reset()
		{
			removeListener();
		}

	private:
		void removeListener()
		{
			if(m_listenerId == InvalidListenerId)
				return;

			m_event->removeListener(m_listenerId);
			m_listenerId = InvalidListenerId;
		}

		MyEvent* m_event = nullptr;
		MyListenerId m_listenerId = InvalidListenerId;
	};
}

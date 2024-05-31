#pragma once

#include <functional>
#include <map>
#include <cassert>

namespace pluginLib
{
	template<typename ...Ts>
	class Event
	{
	public:
		using ListenerId = size_t;
		using Callback = std::function<void(const Ts&...)>;
		using MyTuple = std::tuple<std::decay_t<Ts>...>;

		static constexpr ListenerId InvalidListenerId = ~0;

		ListenerId addListener(const Callback& _callback)
		{
			ListenerId id;

			if(m_listeners.empty())
			{
				id = 0;
			}
			else
			{
				id = m_listeners.rbegin()->first + 1;

				// ReSharper disable once CppUseAssociativeContains - wrong, exists in cpp20+ only
				while(m_listeners.find(id) != m_listeners.end())
					++id;
			}
			addListener(id, _callback);
			return id;
		}

		void addListener(ListenerId _id, const Callback& _callback)
		{
			m_listeners.insert(std::make_pair(_id, _callback));

			if(m_hasRetainedValue)
				std::apply(_callback, m_retainedValue);
		}

		void removeListener(const ListenerId _id)
		{
			m_listeners.erase(_id);
		}

		void clear()
		{
			m_listeners.clear();
		}

		void invoke(const Ts& ..._args) const
		{
			for (const auto& it : m_listeners)
				it.second(_args...);
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
		std::map<ListenerId, Callback> m_listeners;

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

		void set(Event<Ts...>& _event, const MyCallback& _func)
		{
			removeListener();
			m_event = &_event;
			m_listenerId = _event.addListener(_func);
		}

		bool isBound() const { return m_listenerId != InvalidListenerId; }
		bool isValid() const { return m_event != nullptr; }

		EventListener& operator = (const MyCallback& _callback)
		{
			set(_callback);
			return *this;
		}

		EventListener& operator = (Event<Ts...>& _event) noexcept
		{
			if(&_event == m_event)
				return *this;

			removeListener();
			m_event = &_event;
			return *this;
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

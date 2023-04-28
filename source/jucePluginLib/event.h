#pragma once

#include <functional>
#include <map>

namespace pluginLib
{
	class Event
	{
	public:
		using Callback = std::function<void()>;

		Event() = default;

		void addListener(size_t _id, const Callback& _callback)
		{
			m_listeners.insert(std::make_pair(_id, _callback));
		}

		void removeListener(const size_t _id)
		{
			m_listeners.erase(_id);
		}

		void clear()
		{
			m_listeners.clear();
		}

		void invoke() const
		{
			for (const auto& it : m_listeners)
				it.second();
		}

		void operator ()() const
		{
			invoke();
		}

	private:
		std::map<size_t, Callback> m_listeners;
	};
}

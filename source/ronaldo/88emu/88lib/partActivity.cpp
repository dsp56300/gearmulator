#include "88lib/partActivity.h"

namespace emu88Lib
{
	void PartActivity::setEnabled(const bool _enabled)
	{
		m_enabled.store(_enabled, std::memory_order_relaxed);
		m_activity.store(0, std::memory_order_relaxed);
	}

	void PartActivity::setListeners(const Listeners& _listeners)
	{
		for(size_t i = 0; i < _listeners.size(); ++i)
			m_listeners[i].store(_listeners[i], std::memory_order_relaxed);
	}
}

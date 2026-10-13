#include "88lib/mt32/stateTracker.h"

namespace emu88Lib::mt32
{
	namespace
	{
		// The channel messages that write to a board's memory
		bool touchesMemory(const synthLib::SMidiEvent& _event)
		{
			const auto type = static_cast<uint8_t>(_event.a & 0xf0);
			if(type == 0xc0)
				return true;
			if(type != 0xb0)
				return false;
			switch(_event.b)
			{
			case 6: case 7: case 10: case 98: case 99: case 100: case 101:
				return true;
			default:
				return false;
			}
		}
	}

	void StateTracker::setModel(const DeviceModel _model, const Presets& _presets)
	{
		std::lock_guard lock(m_mutex);
		m_memory = Memory(_model);
		for(const auto& [source, data] : _presets)
			m_memory.addPreset(source, data);
		changed();
	}

	DeviceModel StateTracker::model() const
	{
		std::lock_guard lock(m_mutex);
		return m_memory.model();
	}

	void StateTracker::reset()
	{
		std::lock_guard lock(m_mutex);
		m_memory.reset();
		changed();
	}

	void StateTracker::process(const synthLib::SMidiEvent& _event)
	{
		if(_event.sysex.empty())
		{
			if(!touchesMemory(_event))
				return;
			std::lock_guard lock(m_mutex);
			if(m_memory.processShort(_event.a, _event.b, _event.c))
				changed();
			return;
		}
		if(_event.sysex.size() < 11 || _event.sysex[1] != g_rolandId || _event.sysex[3] != g_modelId)
			return;
		std::lock_guard lock(m_mutex);
		if(m_memory.processSysex(_event.sysex.data(), _event.sysex.size()))
			changed();
	}

	void StateTracker::processReply(const synthLib::SMidiEvent& _event)
	{
		if(_event.sysex.empty())
			return;
		std::lock_guard lock(m_mutex);
		if(m_memory.processReply(_event.sysex.data(), _event.sysex.size()))
			changed();
	}

	Memory StateTracker::memory() const
	{
		std::lock_guard lock(m_mutex);
		return m_memory;
	}

	Memory StateTracker::memory(uint64_t& _revision) const
	{
		std::lock_guard lock(m_mutex);
		_revision = m_revision.load(std::memory_order_relaxed);
		return m_memory;
	}

	std::vector<Message> StateTracker::createRestore() const
	{
		std::lock_guard lock(m_mutex);
		return m_memory.createRestore();
	}

	void StateTracker::changed()
	{
		m_revision.fetch_add(1, std::memory_order_acq_rel);
	}
}

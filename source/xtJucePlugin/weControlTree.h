#pragma once

#include "weControlTreeItem.h"
#include "weTree.h"

#include "jucePluginLib/event.h"
#include "xtLib/xtMidiTypes.h"

namespace xtJucePlugin
{
	class ControlTree : public Tree
	{
	public:
		ControlTree(WaveEditor& _editor);

		void setTable(uint32_t _index);

	private:
		void onTableChanged();

		pluginLib::EventListener<uint32_t> m_onTableChanged;
		uint32_t m_table = ~0;
		std::array<ControlTreeItem*, xt::Wave::g_wavesPerTable> m_items;
	};
}

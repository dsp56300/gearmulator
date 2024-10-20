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

		void setTable(xt::TableId _index);

	private:
		void onTableChanged(bool _tableHasChanged) const;

		pluginLib::EventListener<xt::TableId> m_onTableChanged;
		xt::TableId m_table;
		std::array<ControlTreeItem*, xt::wave::g_wavesPerTable> m_items;
	};
}

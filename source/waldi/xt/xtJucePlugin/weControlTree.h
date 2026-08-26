#pragma once

#include "weControlTreeItem.h"
#include "weTree.h"

#include "baseLib/event.h"

#include "xtLib/xtMidiTypes.h"

namespace xtJucePlugin
{
	class ControlTree : public Tree
	{
	public:
		ControlTree(Rml::CoreInstance& _coreInstance, const std::string& _tag, WaveEditor& _editor);

		void setTable(xt::TableId _index);

	private:
		Rml::ElementPtr createChild(const std::string& _tag) override;
		void onTableChanged(bool _tableHasChanged) const;

		baseLib::EventListener<xt::TableId> m_onTableChanged;
		xt::TableId m_table;
		std::array<juceRmlUi::TreeNodePtr, xt::wave::g_wavesPerTable> m_items;
	};
}

#pragma once

#include "weTreeItem.h"

#include "jucePluginLib/event.h"

namespace xtJucePlugin
{
	class WaveEditor;

	class TablesTreeItem : public TreeItem
	{
	public:
		TablesTreeItem(WaveEditor& _editor, uint32_t _tableIndex);

		bool mightContainSubItems() override { return false; }

		void itemSelectionChanged(bool _isNowSelected) override;

	private:
		void onTableChanged(uint32_t _index);
		void onTableChanged();

		WaveEditor& m_editor;
		const uint32_t m_index;
		pluginLib::EventListener<uint32_t> m_onTableChanged;
	};
}

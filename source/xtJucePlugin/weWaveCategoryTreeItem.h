#pragma once

#include "weTreeItem.h"
#include "weTypes.h"

namespace xtJucePlugin
{
	class WaveEditor;

	class WaveCategoryTreeItem : public TreeItem
	{
	public:
		explicit WaveCategoryTreeItem(WaveEditor& _editor, WaveCategory _category);
		bool mightContainSubItems() override { return true; }
		bool setSelectedWave(xt::WaveId _id);

		static std::string getCategoryName(WaveCategory _category);

	private:
		void addItems(uint32_t _first, uint32_t _count);
		void addItem(uint32_t _index);

		WaveEditor& m_editor;
		const WaveCategory m_category;
	};
}

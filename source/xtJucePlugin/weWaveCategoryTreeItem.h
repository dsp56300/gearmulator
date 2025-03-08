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

		juce::var getDragSourceDescription() override;

		void itemClicked(const juce::MouseEvent&) override;

	private:
		void addItems(uint16_t _first, uint16_t _count);
		void addItem(uint16_t _index);

		void exportAll(bool _midi) const;

		WaveEditor& m_editor;
		const WaveCategory m_category;
	};
}

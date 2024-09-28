#pragma once

#include "weTree.h"
#include "weTypes.h"

namespace xtJucePlugin
{
	class WaveCategoryTreeItem;

	class WaveTree : public Tree
	{
	public:
		explicit WaveTree(WaveEditor& _editor);

		bool setSelectedWave(xt::WaveId _id);

	private:
		void addCategory(WaveCategory _category);

		std::map<WaveCategory, WaveCategoryTreeItem*> m_items;
	};
}

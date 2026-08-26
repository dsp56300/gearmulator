#pragma once

#include "weTree.h"
#include "weTypes.h"

namespace xtJucePlugin
{
	class WaveCategoryTreeItem;

	class WaveTree : public Tree
	{
	public:
		enum class NextChildType
		{
			Category,
			Wave
		};

		explicit WaveTree(Rml::CoreInstance& _coreInstance, const std::string& _tag, WaveEditor& _editor);

		Rml::ElementPtr createChild(const std::string& _tag) override;

		bool setSelectedWave(xt::WaveId _id);

	private:
		void addCategory(WaveCategory _category);

		std::map<WaveCategory, juceRmlUi::TreeNodePtr> m_items;

		NextChildType m_nextChildType = NextChildType::Category;
	};
}

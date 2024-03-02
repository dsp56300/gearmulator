#pragma once

#include "search.h"

namespace jucePluginEditorLib::patchManager
{
	class Tree;

	class SearchTree : public Search
	{
	public:
		explicit SearchTree(Tree& _tree);

		void onTextChanged(const std::string& _text) override;

	private:
		Tree& m_tree;
	};
}

#include "searchtree.h"

#include "tree.h"

namespace jucePluginEditorLib::patchManager
{
	SearchTree::SearchTree(Tree& _tree): m_tree(_tree)
	{
	}

	void SearchTree::onTextChanged(const std::string& _text)
	{
		m_tree.setFilter(_text);
	}
}

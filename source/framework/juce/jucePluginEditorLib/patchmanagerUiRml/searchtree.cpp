#include "searchtree.h"

#include "tree.h"

namespace jucePluginEditorLib::patchManagerRml
{
	SearchTree::SearchTree(Rml::ElementFormControlInput* _input, Tree& _tree) : Search(_input), m_tree(_tree)
	{
	}

	void SearchTree::onTextChanged(const std::string& _text)
	{
		m_tree.setFilter(_text);
	}
}

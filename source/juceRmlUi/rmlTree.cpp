#include "rmlTree.h"

namespace juceRmlUi
{
	Tree::Tree() : m_root(std::make_shared<TreeNode>(*this))
	{
	}

	Tree::~Tree()
	{
		m_root->clear();
		m_root.reset();
	}
}

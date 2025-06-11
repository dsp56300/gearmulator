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

	bool Tree::setSelectedNode(const TreeNodePtr& _node)
	{
		if (_node && &_node->getTree() != this)
			return false;
		if (_node == m_selectedNode)
			return false;
		m_selectedNode = _node;
		evSelectedNodeChanged(this, m_selectedNode);
		return true;
	}
}

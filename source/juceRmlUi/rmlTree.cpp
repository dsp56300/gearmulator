#include "rmlTree.h"

#include "rmlElemTree.h"

namespace juceRmlUi
{
	Tree::Tree(ElemTree& _treeElem) : m_treeElem(_treeElem), m_root(std::make_shared<TreeNode>(*this))
	{
		m_root->setOpened(true);
	}

	Tree::~Tree()
	{
		m_root->clear();
		m_root.reset();
	}

	void Tree::childAdded(const TreeNodePtr& _parent, const TreeNodePtr& _child)
	{
		m_treeElem.childAdded(_parent, _child);
	}

	void Tree::childRemoved(const TreeNodePtr& _parent, const TreeNodePtr& _child)
	{
		m_treeElem.childRemoved(_parent, _child);
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

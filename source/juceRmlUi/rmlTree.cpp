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

	void Tree::clearSelectedNodes() const
	{
		while (!m_selectedNodes.empty())
		{
			auto n = *m_selectedNodes.begin();
			n->setSelected(false, false);
		}
	}

	void Tree::childAdded(const TreeNodePtr& _parent, const TreeNodePtr& _child)
	{
		m_treeElem.childAdded(_parent, _child);
	}

	void Tree::childRemoved(const TreeNodePtr& _parent, const TreeNodePtr& _child)
	{
		m_treeElem.childRemoved(_parent, _child);
	}

	bool Tree::addSelectedNode(const TreeNodePtr& _node)
	{
		if (_node && &_node->getTree() != this)
			return false;
		if (!m_selectedNodes.insert(_node).second)
			return false;
		evNodeSelected(this, _node);
		return true;
	}

	bool Tree::removeSelectedNode(const TreeNodePtr& _node)
	{
		if (m_selectedNodes.erase(_node) == 0)
			return false;
		evNodeDeselected(this, _node);
		return true;
	}
}

#include "rmlTreeNode.h"

namespace juceRmlUi
{
	TreeNode::TreeNode(Tree& _tree) : m_tree(_tree)
	{
	}

	void TreeNode::clear()
	{
		while (!m_children.empty())
		{
			const auto child = m_children.back();
			child->clear();
			child->setParent(nullptr);
		}
	}

	bool TreeNode::setParent(const TreeNodePtr& _parent)
	{
		if (m_parent.lock() == _parent)
			return true;

		if (_parent)
		{
			if (&_parent->m_tree != &m_tree)
				return false; // parent must belong to the same tree
		}

		auto self = shared_from_this();

		if (auto parent = m_parent.lock())
		{
			const auto it = std::find(parent->m_children.begin(), parent->m_children.end(), self);
			assert(it != parent->m_children.end() && "TreeNode::setParent: Node is not a child of its parent.");
			parent->m_children.erase(it);
			parent->evChildRemoved(parent, *it);
		}

		m_parent = _parent;

		if (_parent)
		{
			_parent->m_children.push_back(self);
			_parent->evChildAdded(_parent, self);
		}

		evParentChanged(self);

		return true;
	}

	TreeNodePtr TreeNode::getChild(const size_t _index) const
	{
		if (_index < m_children.size())
			return m_children[_index];
		return nullptr;
	}

	bool TreeNode::addChild(const TreeNodePtr& _child)
	{
		return _child->setParent(shared_from_this());
	}

	bool TreeNode::removeChild(const TreeNodePtr& _child)
	{
		if (_child->getParent().get() != this)
			return false;
		return _child->setParent(nullptr);
	}

	bool TreeNode::isChild(const TreeNodePtr& _child) const
	{
		return std::find(m_children.begin(), m_children.end(), _child) != m_children.end();
	}

	size_t TreeNode::getChildIndex(const TreeNodePtr& _child) const
	{
		const auto it = std::find(m_children.begin(), m_children.end(), _child);
		if (it != m_children.end())
			return std::distance(m_children.begin(), it);
		return InvalidIndex;
	}

	TreeNodePtr TreeNode::getPreviousNode()
	{
		const auto childIndex = getChildIndex(shared_from_this());
		if (childIndex == 0)
		{
			auto parent = getParent();
			if (!parent)
				return {};
			return parent->getPreviousNode();
		}
		return m_children[childIndex - 1];
	}

	TreeNodePtr TreeNode::getNextNode()
	{
		const auto childIndex = getChildIndex(shared_from_this());
		const auto nextChild = childIndex +1;
		if (nextChild < m_children.size())
			return m_children[nextChild];

		auto parent = getParent();
		if (!parent)
			return {};
		return parent->getNextNode();
	}

	size_t TreeNode::getDepth() const
	{
		size_t depth = 0;
		auto parent = getParent();
		while (parent)
		{
			++depth;
			parent = parent->getParent();
		}
		return depth;
	}
}

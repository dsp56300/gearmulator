#include "rmlTreeNode.h"

#include "rmlElemTree.h"
#include "rmlTree.h"

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

	bool TreeNode::setParent(const TreeNodePtr& _parent, const size_t _position/* = InvalidIndex*/)
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
			const auto elem = *it;  // NOLINT(performance-unnecessary-copy-initialization)
			parent->m_children.erase(it);
			parent->evChildRemoved(parent, elem);
			m_tree.childRemoved(parent, elem);
		}

		m_parent = _parent;

		if (_parent)
		{
			if (_position >= _parent->m_children.size())
				_parent->m_children.push_back(self);
			else
				_parent->m_children.insert(_parent->m_children.begin() + static_cast<ptrdiff_t>(_position), self);
			_parent->evChildAdded(_parent, self);
			m_tree.childAdded(_parent, self);
		}
		else
		{
			setSelected(false);
		}

		evParentChanged(self);

		return true;
	}

	bool TreeNode::setParent(const TreeNodePtr& _parent, const SortedInsertionComparer& _sortedInsertionComparer)
	{
		if (!_parent || getParent() == _parent)
			return setParent(_parent);

		const auto self = shared_from_this();

		const auto& children = _parent->m_children;
		const auto it = std::lower_bound(children.begin(), children.end(), self, _sortedInsertionComparer);

		const auto position = std::distance(children.begin(), it);

		// now insert at the found position
		return setParent(_parent, position);
	}

	TreeNodePtr TreeNode::getChild(const size_t _index) const
	{
		if (_index < m_children.size())
			return m_children[_index];
		return nullptr;
	}

	bool TreeNode::addChild(const TreeNodePtr& _child, const size_t _position/* = InvalidIndex*/)
	{
		return _child->setParent(shared_from_this(), _position);
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

	TreeNodePtr TreeNode::getPreviousNode(const bool _visibleOnly)
	{
		auto parent = getParent();
		if (!parent)
			return {};

		auto sibling = getPreviousSibling();
		if (!sibling)
			return parent;

		while(!sibling->empty() && (!_visibleOnly || sibling->isOpened()))
			sibling = sibling->getChildren().back();
		return sibling;
	}

	TreeNodePtr TreeNode::getPreviousSibling()
	{
		auto parent = getParent();
		if (!parent)
			return {};
		const auto childIndex = parent->getChildIndex(shared_from_this());
		assert(childIndex != InvalidIndex && "TreeNode::getPreviousNode: Node is not a child of its parent.");
		if (childIndex == 0)
			return {};
		return parent->getChild(childIndex - 1);
	}

	TreeNodePtr TreeNode::getNextNode(const bool _visibleOnly)
	{
		if (!empty() && (!_visibleOnly || isOpened()))
			return m_children.front();

		auto parent = getParent();
		if (!parent)
			return {};

		auto sibling = getNextSibling();
		if (sibling)
			return sibling;

		while (parent)
		{
			auto s = parent->getNextSibling();
			if (s)
				return s;
			parent = parent->getParent();
		}
		return {};
	}

	TreeNodePtr TreeNode::getNextSibling()
	{
		auto parent = getParent();
		if (!parent)
			return {};

		const auto childIndex = parent->getChildIndex(shared_from_this());
		assert(childIndex != InvalidIndex && "TreeNode::getNextSibling: Node is not a child of its parent.");
		const auto nextChild = childIndex + 1;
		if (nextChild < parent->getChildren().size())
			return parent->getChild(nextChild);
		return {};
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

	bool TreeNode::setOpened(const bool _isOpened)
	{
		if (m_isOpened == _isOpened)
			return false;
		m_isOpened = _isOpened;
		evOpenedChanged(shared_from_this(), m_isOpened);

		for (auto& child : m_children)
			child->notifyVisibilityChanged();

		return true;
	}

	bool TreeNode::isVisible() const
	{
		const auto parent = getParent();
		if (!parent)
			return true;
		if (parent->isClosed())
			return false;
		return parent->isVisible();
	}

	void TreeNode::makeVisible()
	{
		auto p = getParent();
		while (p)
		{
			p->setOpened(true);
			p = p->getParent();
		}
	}

	bool TreeNode::setSelected(const bool _selected, bool _allowMultiselect)
	{
		if (m_isSelected == _selected)
			return false;

		m_isSelected = _selected;

		auto self = shared_from_this();

		if (_selected)
		{
			const auto allowMultiselect = _allowMultiselect && m_tree.isMultiSelectEnabled();

			auto prev = m_tree.getSelectedNodes();

			if (!allowMultiselect)
			{
				for (const auto& treeNode : prev)
					treeNode->setSelected(false, false);
			}

			m_tree.addSelectedNode(self);
		}
		else if (!_selected)
		{
			m_tree.removeSelectedNode(self);
		}

		evSelectedChanged(self, m_isSelected);

		m_tree.evNodeSelectionChanged(self, m_isSelected);

		return true;
	}

	bool TreeNode::handleNavigationKey(const Rml::Input::KeyIdentifier _key)
	{
		using namespace Rml::Input;

		switch (_key)
		{
		case KI_RIGHT:
			if (!empty() && isClosed())
			{
				setOpened(true);
			}
			else
			{
				auto child = getChild(0);
				if (child)
					return child->setSelected(true);
				auto next = getNextSibling();
				if (next)
					next->setSelected(true);
			}
			break;
		case KI_LEFT:
			if (!empty() && isOpened())
			{
				setOpened(false);
			}
			else
			{
				auto parent = getParent();
				if (parent && !parent->isRoot())
					return parent->setSelected(true);
			}
			break;
		case KI_UP:
			{
				auto prev = getPreviousNode(true);
				if (prev && !prev->isRoot())
					return prev->setSelected(true);
			}
			break;
		case KI_DOWN:
			{
				const auto next = getNextNode(true);
				if (next)
					return next->setSelected(true);
			}
			break;
		default:
			return false;
		}
		return false;
	}

	void TreeNode::notifyVisibilityChanged()
	{
		evVisibilityChanged(shared_from_this(), isVisible());
		for (auto& child : m_children)
			child->notifyVisibilityChanged();
	}
}

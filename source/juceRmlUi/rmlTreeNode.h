#pragma once

#include <memory>
#include <vector>
#include <limits>

#include "baseLib/event.h"

#include "RmlUi/Core/Input.h"

namespace Rml
{
	class Element;
}

namespace juceRmlUi
{
	class ElemTreeNode;
	class Tree;
	class TreeNode;
	using TreeNodePtr = std::shared_ptr<TreeNode>;

	class TreeNode : public std::enable_shared_from_this<TreeNode>
	{
	public:
		using Children = std::vector<TreeNodePtr>;
		static constexpr size_t InvalidIndex = std::numeric_limits<size_t>::max();

		baseLib::Event<TreeNodePtr> evParentChanged;
		baseLib::Event<TreeNodePtr, TreeNodePtr> evChildAdded;
		baseLib::Event<TreeNodePtr, TreeNodePtr> evChildRemoved;
		baseLib::Event<TreeNodePtr, bool> evOpenedChanged;
		baseLib::Event<TreeNodePtr, bool> evSelectedChanged;
		baseLib::Event<TreeNodePtr, bool> evVisibilityChanged;

		TreeNode(Tree& _tree);
		virtual ~TreeNode() = default;

		bool isRoot() const { return m_parent.expired(); }
		bool empty() const { return m_children.empty(); }
		size_t size() const { return m_children.size(); }
		void clear();

		TreeNodePtr getParent() const { return m_parent.lock(); }
		bool setParent(const TreeNodePtr& _parent);

		void removeFromParent()
		{
			setParent(nullptr);
		}

		const Children& getChildren() const { return m_children; }
		TreeNodePtr getChild(size_t _index) const;

		bool addChild(const TreeNodePtr& _child);
		bool removeChild(const TreeNodePtr& _child);

		template<typename T, typename... Args>
		std::shared_ptr<T> createChild(Args&& ..._args)
		{
			auto child = std::make_shared<T>(m_tree, std::forward<Args>(_args)...);
			if (addChild(child))
				return child;
			return {};
		}
		bool isChild(const TreeNodePtr& _child) const;
		size_t getChildIndex(const TreeNodePtr& _child) const;

		operator TreeNodePtr() { return shared_from_this(); }

		TreeNodePtr getPreviousNode(bool _visibleOnly);
		TreeNodePtr getPreviousSibling();
		TreeNodePtr getNextNode(bool _visibleOnly);
		TreeNodePtr getNextSibling();

		size_t getDepth() const;

		bool isOpened() const { return m_isOpened; }
		bool isClosed() const { return !m_isOpened; }
		bool setOpened(bool _isOpened);
		bool isVisible() const;	// returns true if all nodes in the path to the root are opened
		void makeVisible();

		bool isSelected() const { return m_isSelected; }
		bool setSelected(bool _selected, bool _allowMultiselect = false);

		bool handleNavigationKey(Rml::Input::KeyIdentifier _key);

		Tree& getTree() const { return m_tree; }

		void setElement(ElemTreeNode* _element)
		{
			m_element = _element;
		}

		ElemTreeNode* getElement() const
		{
			return m_element;
		}

	private:
		void notifyVisibilityChanged();

		Tree& m_tree;

		std::weak_ptr<TreeNode> m_parent;
		Children m_children;
		bool m_isOpened = false;
		bool m_isSelected = false;
		ElemTreeNode* m_element = nullptr;
	};
}

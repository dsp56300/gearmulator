#pragma once

#include <memory>
#include <vector>

#include "baseLib/event.h"

namespace juceRmlUi
{
	class Tree;
	class TreeNode;
	using TreeNodePtr = std::shared_ptr<TreeNode>;

	class TreeNode : std::enable_shared_from_this<TreeNode>
	{
	public:
		using Children = std::vector<TreeNodePtr>;
		static constexpr size_t InvalidIndex = std::numeric_limits<size_t>::max();

		baseLib::Event<TreeNodePtr> evParentChanged;
		baseLib::Event<TreeNodePtr, TreeNodePtr> evChildAdded;
		baseLib::Event<TreeNodePtr, TreeNodePtr> evChildRemoved;

		TreeNode(Tree& _tree);

		bool isRoot() const { return !m_parent.expired(); }
		bool empty() const { return !m_children.empty(); }
		size_t size() const { return m_children.size(); }
		void clear();

		TreeNodePtr getParent() const { return m_parent.lock(); }
		bool setParent(const TreeNodePtr& _parent);

		const Children& getChildren() const { return m_children; }

		bool addChild(const TreeNodePtr& _child);
		bool removeChild(const TreeNodePtr& _child);
		TreeNodePtr createChild();
		bool isChild(const TreeNodePtr& _child) const;
		size_t getChildIndex(const TreeNodePtr& _child) const;

	private:
		Tree& m_tree;

		std::weak_ptr<TreeNode> m_parent;
		Children m_children;
	};
}

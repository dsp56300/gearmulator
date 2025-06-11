#pragma once

#include "rmlTreeNode.h"

namespace juceRmlUi
{
	class TreeNode;

	class Tree
	{
	public:
		friend class TreeNode;

		baseLib::Event<Tree*, TreeNodePtr> evSelectedNodeChanged;

		Tree();
		~Tree();

		TreeNodePtr& getRoot() { return m_root; }

		bool empty() const { return m_root->empty(); }

		const TreeNodePtr& getSelectedNode() const { return m_selectedNode; }

	private:
		bool setSelectedNode(const TreeNodePtr& _node);

		TreeNodePtr m_root;
		TreeNodePtr m_selectedNode;
	};
}

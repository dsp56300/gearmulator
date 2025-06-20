#pragma once

#include "rmlTreeNode.h"

namespace juceRmlUi
{
	class ElemTree;
	class TreeNode;

	class Tree
	{
	public:
		friend class TreeNode;

		baseLib::Event<Tree*, TreeNodePtr> evSelectedNodeChanged;

		Tree(ElemTree& _treeElem);
		~Tree();

		TreeNodePtr& getRoot() { return m_root; }

		bool empty() const { return m_root->empty(); }

		const TreeNodePtr& getSelectedNode() const { return m_selectedNode; }

		void childAdded(TreeNodePtr _parent, TreeNodePtr _child);
		void childRemoved(TreeNodePtr _parent, TreeNodePtr _child);

	private:
		bool setSelectedNode(const TreeNodePtr& _node);

		ElemTree& m_treeElem;

		TreeNodePtr m_root;
		TreeNodePtr m_selectedNode;
	};
}

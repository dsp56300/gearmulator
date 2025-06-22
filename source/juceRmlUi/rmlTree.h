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
		baseLib::Event<TreeNodePtr, bool> evNodeSelectionChanged;

		Tree(ElemTree& _treeElem);
		~Tree();

		TreeNodePtr& getRoot() { return m_root; }

		bool empty() const { return m_root->empty(); }

		const TreeNodePtr& getSelectedNode() const { return m_selectedNode; }

		void childAdded(const TreeNodePtr& _parent, const TreeNodePtr& _child);
		void childRemoved(const TreeNodePtr& _parent, const TreeNodePtr& _child);

		void setEnableMultiSelect(const bool _multiselect)
		{
			m_enableMultiselect = _multiselect;
		}
		bool isMultiSelectEnabled() const
		{
			return m_enableMultiselect;
		}

	private:
		bool setSelectedNode(const TreeNodePtr& _node);

		ElemTree& m_treeElem;

		TreeNodePtr m_root;
		TreeNodePtr m_selectedNode;

		bool m_enableMultiselect = false;
	};
}

#pragma once

#include <set>

#include "rmlTreeNode.h"

namespace juceRmlUi
{
	class ElemTree;
	class TreeNode;

	class Tree
	{
	public:
		friend class TreeNode;

		baseLib::Event<Tree*, TreeNodePtr> evNodeSelected;
		baseLib::Event<Tree*, TreeNodePtr> evNodeDeselected;
		baseLib::Event<TreeNodePtr, bool> evNodeSelectionChanged;

		Tree(ElemTree& _treeElem);
		~Tree();

		TreeNodePtr& getRoot() { return m_root; }

		bool empty() const { return m_root->empty(); }

		const auto& getSelectedNodes() const { return m_selectedNodes; }
		void clearSelectedNodes() const;

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

		void setAllowDeselectOnSecondClick(const bool _allow) { m_allowDeselectOnSecondClick = _allow; }
		bool getAllowDeselectOnSecondClick() const { return m_allowDeselectOnSecondClick; }

		ElemTree& getElement() const { return m_treeElem; }

	private:
		bool addSelectedNode(const TreeNodePtr& _node);
		bool removeSelectedNode(const TreeNodePtr& _node);

		ElemTree& m_treeElem;

		TreeNodePtr m_root;
		std::set<TreeNodePtr> m_selectedNodes;

		bool m_allowDeselectOnSecondClick = false;
		bool m_enableMultiselect = false;
	};
}

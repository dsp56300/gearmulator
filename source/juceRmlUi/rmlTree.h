#pragma once

#include "rmlTreeNode.h"

namespace juceRmlUi
{
	class Tree
	{
	public:
		Tree();
		~Tree();

		TreeNode& getRoot() { return m_root; }

		bool empty() const { return m_root.empty(); }
	private:
		TreeNode m_root;
	};
}

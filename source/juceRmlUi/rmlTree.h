#pragma once

#include "rmlTreeNode.h"

namespace juceRmlUi
{
	class Tree
	{
	public:
		Tree();
		~Tree();

		TreeNodePtr& getRoot() { return m_root; }

		bool empty() const { return m_root->empty(); }
	private:
		TreeNodePtr m_root;
	};
}

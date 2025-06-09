#pragma once

#include "rmlElement.h"
#include "rmlTreeNode.h"

namespace juceRmlUi
{
	class ElemTree;

	class ElemTreeNode : public Element
	{
	public:
		explicit ElemTreeNode(const Rml::String& _tag);

		void setTree(ElemTree* _elemTree);
		void setNode(const TreeNodePtr& _node);

	private:
		ElemTree* m_tree = nullptr;
		TreeNodePtr m_node;
	};
}

#include "rmlElemTreeNode.h"

namespace juceRmlUi
{
	ElemTreeNode::ElemTreeNode(const Rml::String& _tag) : Element(_tag)
	{
	}

	void ElemTreeNode::setTree(ElemTree* _elemTree)
	{
		m_tree = _elemTree;
	}

	void ElemTreeNode::setNode(const TreeNodePtr& _node)
	{
		m_node = _node;
	}
}

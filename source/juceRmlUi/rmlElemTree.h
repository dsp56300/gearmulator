#pragma once

#include "rmlElement.h"
#include "rmlTree.h"

namespace juceRmlUi
{
	class ElemTreeNode;

	class ElemTree : public Element
	{
	public:
		explicit ElemTree(const Rml::String& _tag);

		void OnChildAdd(Rml::Element* _child) override;

	private:
		void updateNodeElements();
		void updateNodeElements(const TreeNodePtr& _node);
		void createNodeElement(const TreeNodePtr& _node);

		Tree m_tree;
		std::map<TreeNodePtr, Rml::Element*> m_activeNodeElements;
		ElemTreeNode* m_template = nullptr;
	};
}

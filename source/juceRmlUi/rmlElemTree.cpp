#include "rmlElemTree.h"

#include "rmlElemTreeNode.h"
#include "RmlUi/Core/ElementDocument.h"

namespace juceRmlUi
{
	class TestNode : public TreeNode
	{
	public:
		TestNode(Tree& _tree, std::string _text)
			: TreeNode(_tree),
			  m_text(std::move(_text))
		{
		}

		const auto& text() const { return m_text; }

	private:
		std::string m_text;
	};

	ElemTree::ElemTree(const Rml::String& _tag) : Element(_tag)
	{
		auto r = m_tree.getRoot();
		const auto child = r->createChild<TestNode>("entry A");
		child->createChild<TestNode>("subentry A");
		child->createChild<TestNode>("subentry B");

		const auto child2 = child->createChild<TestNode>("subentry C");
		child2->createChild<TestNode>("subsubentry A");
		child2->createChild<TestNode>("subsubentry B");

		r->createChild<TestNode>("entry B");
	}

	void ElemTree::OnChildAdd(Rml::Element* _child)
	{
		Element::OnChildAdd(_child);

		if (!m_template)
		{
			m_template = dynamic_cast<ElemTreeNode*>(_child);
			if (m_template)
			{
				updateNodeElements();
			}
		}
	}

	void ElemTree::updateNodeElements()
	{
		updateNodeElements(m_tree.getRoot());
	}

	void ElemTree::updateNodeElements(const TreeNodePtr& _node)
	{
		const auto it = m_activeNodeElements.find(_node);
		if (it == m_activeNodeElements.end())
		{
			createNodeElement(_node);
		}
		for (const auto& child : _node->getChildren())
		{
			updateNodeElements(child);
		}
	}

	void ElemTree::createNodeElement(const TreeNodePtr& _node)
	{
		Rml::ElementPtr elem = m_template->Clone();

		Rml::Element* insertBefore = nullptr;

		auto next = _node->getNextNode();

		if (next)
		{
			const auto itNext = m_activeNodeElements.find(next);
			if (itNext != m_activeNodeElements.end())
				insertBefore = itNext->second;
		}

		Rml::Element* e = insertBefore ? InsertBefore(std::move(elem), insertBefore) : AppendChild(std::move(elem));

		auto nodeElem = dynamic_cast<ElemTreeNode*>(e);
		if (nodeElem)
		{
			nodeElem->setTree(this);
			nodeElem->setNode(_node);

			auto* testNode = dynamic_cast<TestNode*>(_node.get());
			if (testNode)
				nodeElem->SetInnerRML(testNode->text());
		}

		m_activeNodeElements.insert({_node, e});
	}
}

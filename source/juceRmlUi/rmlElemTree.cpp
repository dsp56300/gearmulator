#include "rmlElemTree.h"

#include "rmlElemTreeNode.h"
#include "rmlHelper.h"

#include "RmlUi/Core/ElementInstancer.h"

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

	class TestNodeElement : public ElemTreeNode
	{
	public:
		explicit TestNodeElement(const Rml::String& _tag) : ElemTreeNode(_tag)
		{
		}

		void setNode(const TreeNodePtr& _node) override
		{
			ElemTreeNode::setNode(_node);
			auto* testNode = dynamic_cast<TestNode*>(_node.get());
			if (testNode)
				SetInnerRML(testNode->text());
		}
	};

	class TestNodeElementInstancer final : public Rml::ElementInstancer
	{
		public:
		TestNodeElementInstancer() = default;
		~TestNodeElementInstancer() override = default;

		Rml::ElementPtr InstanceElement(Rml::Element*/* _parent*/, const Rml::String& _tag, const Rml::XMLAttributes&/* _attributes*/) override
		{
			return Rml::ElementPtr(new TestNodeElement(_tag));
		}
		void ReleaseElement(Rml::Element* element) override
		{
			delete element;
		}
	};

	static TestNodeElementInstancer g_instancer;

	ElemTree::ElemTree(const Rml::String& _tag) : Element(_tag)
	{
		auto r = m_tree.getRoot();
		const auto child = r->createChild<TestNode>("entry A");
		child->createChild<TestNode>("subentry A");
		child->createChild<TestNode>("subentry B");

		const auto child2 = child->createChild<TestNode>("subentry C");
		child2->createChild<TestNode>("subsubentry A");
		child2->createChild<TestNode>("subsubentry B");

		for (size_t i=0; i<100; ++i)
			r->createChild<TestNode>("entry " + std::to_string(i));

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
				m_template->SetProperty(Rml::PropertyId::Display, Rml::Style::Display::None);
				updateNodeElements();
			}
		}
	}

	void ElemTree::OnPropertyChange(const Rml::PropertyIdSet& changed_properties)
	{
		Element::OnPropertyChange(changed_properties);
	}

	void ElemTree::onPropertyChanged(const std::string& _key)
	{
		if (_key == "indent-margin-left")
			updateElementsDepthProperty("indent-margin-left", "margin-left");
		else if (_key == "indent-padding-left")
			updateElementsDepthProperty("indent-padding-left", "padding-left");

		Element::onPropertyChanged(_key);
	}

	void ElemTree::updateNodeElements()
	{
		updateNodeElements(m_tree.getRoot());
	}

	void ElemTree::updateNodeElements(const TreeNodePtr& _node)
	{
		if (_node->getParent())
		{
			const auto it = m_activeNodeElements.find(_node);
			if (it == m_activeNodeElements.end())
			{
				createNodeElement(_node);
			}
		}
		for (const auto& child : _node->getChildren())
		{
			updateNodeElements(child);
		}
	}

	void ElemTree::createNodeElement(const TreeNodePtr& _node)
	{
		Rml::ElementPtr elem = helper::clone(m_template, &g_instancer);

		Rml::Element* insertBefore = nullptr;

		auto next = _node->getNextNode();

		if (next)
		{
			const auto itNext = m_activeNodeElements.find(next);
			if (itNext != m_activeNodeElements.end())
			{
				insertBefore = itNext->second;
				_node->getNextNode();
			}
		}

		Rml::Element* e = insertBefore ? InsertBefore(std::move(elem), insertBefore) : AppendChild(std::move(elem));

		if (const auto nodeElem = dynamic_cast<ElemTreeNode*>(e))
		{
			nodeElem->setTree(this);
			nodeElem->setNode(_node);
		}

		e->SetPseudoClass("depth" + std::to_string(_node->getDepth()), true);

		updateElementDepthProperty(_node, *e, "indent-margin-left", "margin-left");
		updateElementDepthProperty(_node, *e, "indent-padding-left", "padding-left");

		m_activeNodeElements.insert({_node, e});
	}

	void ElemTree::updateElementsDepthProperty(const std::string& _sourceProperty, const std::string& _targetProperty)
	{
		const auto source = GetProperty(_sourceProperty);

		if (!source)
			return;

		auto prop = *source;
		const auto base = prop.GetNumericValue();

		if (base.number <= 0)
			return;

		for (auto& [node, elem] : m_activeNodeElements)
		{
			prop.value = getIndent(base.number, node);
			elem->SetProperty(_targetProperty, prop.ToString());
		}
	}

	void ElemTree::updateElementDepthProperty(const TreeNodePtr& _node, Rml::Element& _elem, const std::string& _sourceProperty, const std::string& _targetProperty)
	{
		const auto source = GetProperty(_sourceProperty);
		if (!source)
			return;

		auto prop = *source;
		const auto base = prop.GetNumericValue();
		if (base.number <= 0)
			return;
		prop.value = getIndent(base.number, _node);
		_elem.SetProperty(_targetProperty, prop.ToString());
	}

	float ElemTree::getIndent(float _base, const TreeNodePtr& _node)
	{
		return _base * static_cast<float>(_node->getDepth() - 1);
	}
}

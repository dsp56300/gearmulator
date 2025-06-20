#include "rmlElemTree.h"

#include "rmlElemTreeNode.h"
#include "rmlHelper.h"

#include "RmlUi/Core/ElementInstancer.h"

namespace juceRmlUi
{

	ElemTree::ElemTree(const Rml::String& _tag) : Element(_tag), m_tree(*this)
	{
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

	void ElemTree::setNodeInstancer(Rml::ElementInstancer* _instancer)
	{
		m_instancer = _instancer;
	}

	void ElemTree::setNodeInstancerCallback(InstancerCallback _callback)
	{
		m_instancerCallback = std::move(_callback);
	}

	void ElemTree::childAdded(const TreeNodePtr& _parent, const TreeNodePtr& _child)
	{
		createNodeElement(_child);
	}

	void ElemTree::childRemoved(const TreeNodePtr& _parent, const TreeNodePtr& _child)
	{
		auto it = m_activeNodeElements.find(_child);
		if (it == m_activeNodeElements.end())
			return;

		Rml::Element* elem = it->second;

		m_activeNodeElements.erase(it);

		if (elem)
		{
			auto parentNode = elem->GetParentNode();
			if (parentNode)
				parentNode->RemoveChild(elem);
		}
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
				createNodeElement(_node);
		}
		for (const auto& child : _node->getChildren())
		{
			updateNodeElements(child);
		}
	}

	void ElemTree::createNodeElement(const TreeNodePtr& _node)
	{
		Rml::ElementInstancer* instancer = m_instancer;

		if (m_instancerCallback)
		{
			instancer = m_instancerCallback(_node);
			if (!instancer)
				instancer = m_instancer;
		}

		Rml::ElementPtr elem = helper::clone(m_template, instancer);

		Rml::Element* insertBefore = nullptr;

		auto next = _node->getNextNode(false);

		if (next)
		{
			const auto itNext = m_activeNodeElements.find(next);
			if (itNext != m_activeNodeElements.end())
				insertBefore = itNext->second;
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

	float ElemTree::getIndent(const float _base, const TreeNodePtr& _node)
	{
		return _base * static_cast<float>(_node->getDepth() - 1);
	}
}

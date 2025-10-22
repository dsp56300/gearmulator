#include "rmlElemTree.h"

#include "rmlElemTreeNode.h"
#include "rmlHelper.h"

#include "RmlUi/Core/ElementInstancer.h"

namespace juceRmlUi
{

	ElemTree::ElemTree(Rml::CoreInstance& _coreInstance, const Rml::String& _tag) : Element(_coreInstance, _tag), m_tree(*this)
	{
	}

	ElemTree::~ElemTree()
	{
		m_tree.getRoot()->clear();
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

	void ElemTree::OnPropertyChange(const Rml::PropertyIdSet& _changedProperties)
	{
		Element::OnPropertyChange(_changedProperties);
	}

	void ElemTree::onPropertyChanged(const std::string& _key)
	{
		if (_key == "indent-margin-left")
			updateElementsDepthProperty("indent-margin-left", Rml::PropertyId::MarginLeft);
		else if (_key == "indent-padding-left")
			updateElementsDepthProperty("indent-padding-left", Rml::PropertyId::PaddingLeft);

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
		auto itA = m_activeNodeElements.find(_child);

		if (itA != m_activeNodeElements.end())
		{
			Rml::Element* elem = itA->second;

			m_activeNodeElements.erase(itA);

			helper::removeFromParent(elem);
		}
		else
		{
			m_inactiveNodeElements.erase(_child);
		}
	}

	void ElemTree::setElementVisibility(const TreeNodePtr& _node, ElemTreeNode& _elem, bool _isVisible)
	{
		if (_isVisible)
		{
			auto it = m_inactiveNodeElements.find(_node);

			if (it != m_inactiveNodeElements.end())
			{
				Rml::ElementPtr elem = std::move(it->second);
				m_inactiveNodeElements.erase(it);
				Rml::Element* e = insertElement(_node, std::move(elem));
				m_activeNodeElements.insert({_node, e});
			}
		}
		else
		{
			auto it = m_activeNodeElements.find(_node);

			if (it != m_activeNodeElements.end())
			{
				auto elem = helper::removeFromParent(it->second);
				m_activeNodeElements.erase(it);
				m_inactiveNodeElements.insert({_node, std::move(elem)});
			}
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
			{
				const auto itInact = m_inactiveNodeElements.find(_node);

				if (itInact == m_inactiveNodeElements.end())
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
		Rml::ElementInstancer* instancer = m_instancer;

		if (m_instancerCallback)
		{
			instancer = m_instancerCallback(_node);
			if (!instancer)
				instancer = m_instancer;
		}

		Rml::ElementPtr elem = helper::clone(m_template, instancer);
		elem->RemoveProperty(Rml::PropertyId::Display);

		updateElementDepthProperty(_node, *elem, "indent-margin-left", Rml::PropertyId::MarginLeft);
		updateElementDepthProperty(_node, *elem, "indent-padding-left", Rml::PropertyId::PaddingLeft);

		if (const auto nodeElem = dynamic_cast<ElemTreeNode*>(elem.get()))
		{
			nodeElem->setTree(this);
			nodeElem->setNode(_node);
		}

		elem->SetPseudoClass("depth" + std::to_string(_node->getDepth()), true);

		if (!_node->getParent() || _node->getParent()->isOpened())
		{
			Rml::Element* e = insertElement(_node, std::move(elem));

			m_activeNodeElements.insert({_node, e});
		}
		else
		{
			m_inactiveNodeElements.insert({_node, std::move(elem)});
		}
	}

	void ElemTree::updateElementsDepthProperty(const std::string& _sourceProperty, const Rml::PropertyId& _targetProperty)
	{
		const auto source = GetProperty(_sourceProperty);

		if (!source)
			return;

		auto prop = *source;
		const auto base = prop.GetNumericValue(GetCoreInstance());

		if (base.number <= 0)
			return;

		for (auto& [node, elem] : m_activeNodeElements)
		{
			prop.value = getIndent(base.number, node);
			helper::changeProperty(elem, _targetProperty, prop);
		}

		// FIXME: don't do this here, we can postpone to when they become visible
		for (auto& [node, elem] : m_inactiveNodeElements)
		{
			prop.value = getIndent(base.number, node);
			helper::changeProperty(elem.get(), _targetProperty, prop);
		}
	}

	void ElemTree::updateElementDepthProperty(const TreeNodePtr& _node, Rml::Element& _elem, const std::string& _sourceProperty, const Rml::PropertyId& _targetProperty)
	{
		const auto source = GetProperty(_sourceProperty);
		if (!source)
			return;

		auto prop = *source;
		const auto base = prop.GetNumericValue(GetCoreInstance());
		if (base.number <= 0)
			return;
		prop.value = getIndent(base.number, _node);
		_elem.SetProperty(_targetProperty, prop);
	}

	float ElemTree::getIndent(const float _base, const TreeNodePtr& _node)
	{
		return _base * static_cast<float>(_node->getDepth() - 1);
	}

	Rml::Element* ElemTree::insertElement(const TreeNodePtr& _node, Rml::ElementPtr&& _elem)
	{
		auto nextNode = _node->getNextNode(false);

		// if there is no next node, we can just append
		if (!nextNode)
			return AppendChild(std::move(_elem));

		// if there is an active next node, we can insert before it
		auto itNext = m_activeNodeElements.find(nextNode);

		if (itNext != m_activeNodeElements.end())
			return InsertBefore(std::move(_elem), itNext->second);

		// The more complex version: If an element needs to be inserted but the next node is inactive, it can
		// only be a sibling or a child. All of these need to be inserted too. We repeat this until we find an active
		// node or run out of nodes.
		std::list<std::pair<Rml::ElementPtr, Rml::Element*>> toInsert;
		std::vector<std::pair<TreeNodePtr, Rml::Element*>> toBeMadeActive;

		while (true)
		{
			itNext = m_activeNodeElements.find(nextNode);

			if (itNext != m_activeNodeElements.end())
			{
				toInsert.emplace_front(std::move(_elem), itNext->second);
				break;
			}

			auto itNextInactive = m_inactiveNodeElements.find(nextNode);

			if (itNextInactive == m_inactiveNodeElements.end())
			{
				toInsert.emplace_front(std::move(_elem), nullptr);
				break;
			}

			nextNode = itNextInactive->first;
			auto nextElem = std::move(itNextInactive->second);

			toInsert.emplace_front(std::move(_elem), nextElem.get());

			toBeMadeActive.emplace_back(nextNode, nextElem.get());

			nextNode = nextNode->getNextNode(false);
			_elem = std::move(nextElem);
		}

		for (const auto& [node, elem] : toBeMadeActive)
		{
			m_inactiveNodeElements.erase(node);
			m_activeNodeElements.insert({node, elem});
		}

		Rml::Element* result = nullptr;

		for (auto& [elem, insertBefore] : toInsert)
		{
			if (insertBefore)
			{
				assert(insertBefore->GetParentNode());
				result = InsertBefore(std::move(elem), insertBefore);
			}
			else
			{
				result = AppendChild(std::move(elem));
			}
		}

		return result;
	}
}

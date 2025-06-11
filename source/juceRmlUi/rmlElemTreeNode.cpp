#include "rmlElemTreeNode.h"

#include "rmlEventListener.h"
#include "rmlHelper.h"

namespace juceRmlUi
{
	ElemTreeNode::ElemTreeNode(const Rml::String& _tag) : Element(_tag)
	{
		this->AddEventListener(Rml::EventId::Click, this);
		this->AddEventListener(Rml::EventId::Keydown, this);
	}

	void ElemTreeNode::setTree(ElemTree* _elemTree)
	{
		m_tree = _elemTree;
	}

	void ElemTreeNode::setNode(const TreeNodePtr& _node)
	{
		if (m_node == _node)
			return;

		if (!_node)
		{
			m_onChildAdded.reset();
			m_onChildRemoved.reset();
			m_onSelectedChanged.reset();
			m_onOpenedChanged.reset();

		}
		m_node = _node;

		m_onChildAdded.set(m_node->evChildAdded, [this](const TreeNodePtr&, const TreeNodePtr& _child)
		{
			onChildAdded(_child);
		});
		m_onChildRemoved.set(m_node->evChildRemoved, [this](const TreeNodePtr&, const TreeNodePtr& _child)
		{
			onChildAdded(_child);
		});
		m_onSelectedChanged.set(m_node->evSelectedChanged, [this](const TreeNodePtr&, const bool _selected)
		{
			onSelectedChanged(_selected);
		});
		m_onOpenedChanged.set(m_node->evOpenedChanged, [this](const TreeNodePtr&, const bool _opened)
		{
			onOpenedChanged(_opened);
		});
		m_onVisibilityChanged.set(m_node->evVisibilityChanged, [this](const TreeNodePtr&, const bool _isVisible)
		{
			onVisibilityChanged(_isVisible);
		});

		updatePropertiesFromNode();
	}

	void ElemTreeNode::ProcessEvent(Rml::Event& _event)
	{
		if (!m_node)
			return;

		switch (_event.GetId())
		{
		case Rml::EventId::Click:
			{
				const auto mousePos = helper::getMousePos(_event);

				// if we click left of the content, we toggle opened/closed, if we click on the content, we toggle selected
				if (!m_node->empty())
				{
					auto edgeLeftContent = this->GetBox().GetCumulativeEdge(Rml::BoxArea::Content, Rml::BoxEdge::Left);

					if (mousePos.x < edgeLeftContent)
					{
						m_node->setOpened(!m_node->isOpened());
						return;
					}
				}

				m_node->setSelected(!m_node->isSelected());
			}
			break;
		case Rml::EventId::Keydown:
			if (m_node->isSelected())
			{
				const auto keyIdentifier = helper::getKeyIdentifier(_event);
				if (m_node->handleNavigationKey(keyIdentifier))
					_event.StopPropagation();
			}
			break;
		default:
			break;
		}
	}

	void ElemTreeNode::onSelectedChanged(bool /*_selected*/)
	{
		updateSelectedProperties();
	}

	void ElemTreeNode::onOpenedChanged(bool /*_opened*/)
	{
		updateOpenClosedProperties();
	}

	void ElemTreeNode::onVisibilityChanged(bool /*_opened*/)
	{
		updateVisibilityProperties();
	}

	void ElemTreeNode::onChildAdded(const TreeNodePtr& /*_child*/)
	{
		updateOpenClosedProperties();
	}

	void ElemTreeNode::onChildRemoved(const TreeNodePtr& /*_child*/)
	{
		updateOpenClosedProperties();
	}

	void ElemTreeNode::updatePropertiesFromNode()
	{
		updateVisibilityProperties();
		updateOpenClosedProperties();
		updateSelectedProperties();
	}

	void ElemTreeNode::updateSelectedProperties()
	{
		SetPseudoClass("selected", m_node->isSelected());

		if (m_node->isSelected())
		{
			Focus();
			Rml::ScrollIntoViewOptions options;
			options.vertical = Rml::ScrollAlignment::Nearest;
			options.behavior = Rml::ScrollBehavior::Smooth;
			options.parentage = Rml::ScrollParentage::Closest;
			ScrollIntoView(options);
		}
	}

	void ElemTreeNode::updateOpenClosedProperties()
	{
		SetPseudoClass("opened", m_node->isOpened() && !m_node->empty());
		SetPseudoClass("closed", m_node->isClosed() && !m_node->empty());
		SetPseudoClass("empty", m_node->empty());
	}

	void ElemTreeNode::updateVisibilityProperties()
	{
		SetProperty(Rml::PropertyId::Display, Rml::Property(m_node->isVisible() ? Rml::Style::Display::Block : Rml::Style::Display::None));
	}
}

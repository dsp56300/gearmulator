#pragma once

#include "rmlElement.h"
#include "rmlTreeNode.h"

#include "RmlUi/Core/EventListener.h"

namespace juceRmlUi
{
	class ElemTree;

	class ElemTreeNode : public Element, Rml::EventListener
	{
	public:
		explicit ElemTreeNode(Rml::CoreInstance& _coreInstance, const Rml::String& _tag);
		~ElemTreeNode() override;

		virtual void setTree(ElemTree* _elemTree);
		ElemTree* getTree() const { return m_tree; }

		virtual void setNode(const TreeNodePtr& _node);

		auto& getNode() const { return m_node; }

		void ProcessEvent(Rml::Event& _event) override;

		virtual void onClick() {}
		virtual void onRightClick(const Rml::Event& _event) {}

	private:
		void onSelectedChanged(bool _selected);
		void onOpenedChanged(bool _opened);
		void onVisibilityChanged(bool _opened);
		void onChildAdded(const TreeNodePtr& _child);
		void onChildRemoved(const TreeNodePtr& _child);

		void updatePropertiesFromNode();
		void updateSelectedProperties();
		void updateOpenClosedProperties();
		void updateVisibilityProperties();

		ElemTree* m_tree = nullptr;
		TreeNodePtr m_node;

		baseLib::EventListener<TreeNodePtr, bool> m_onSelectedChanged;
		baseLib::EventListener<TreeNodePtr, bool> m_onOpenedChanged;
		baseLib::EventListener<TreeNodePtr, bool> m_onVisibilityChanged;
		baseLib::EventListener<TreeNodePtr, TreeNodePtr> m_onChildAdded;
		baseLib::EventListener<TreeNodePtr, TreeNodePtr> m_onChildRemoved;
	};
}

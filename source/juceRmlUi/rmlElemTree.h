#pragma once

#include "rmlElement.h"
#include "rmlTree.h"

namespace juceRmlUi
{
	class ElemTreeNode;

	class ElemTree : public Element
	{
	public:
		using InstancerCallback = std::function<Rml::ElementInstancer*(const TreeNodePtr&)>;

		explicit ElemTree(Rml::CoreInstance& _coreInstance, const Rml::String& _tag);
		~ElemTree() override;

		void OnChildAdd(Rml::Element* _child) override;

		void OnPropertyChange(const Rml::PropertyIdSet& _changedProperties) override;
		void onPropertyChanged(const std::string& _key) override;

		void setNodeInstancer(Rml::ElementInstancer* _instancer);
		void setNodeInstancerCallback(InstancerCallback _callback);

		void childAdded(const TreeNodePtr& _parent, const TreeNodePtr& _child);
		void childRemoved(const TreeNodePtr& _parent, const TreeNodePtr& _child);

		Tree& getTree() { return m_tree; }

		void setElementVisibility(const TreeNodePtr& _node, ElemTreeNode& _elem, bool _isVisible);

	private:
		void updateNodeElements();
		void updateNodeElements(const TreeNodePtr& _node);
		void createNodeElement(const TreeNodePtr& _node);

		void updateElementsDepthProperty(const std::string& _sourceProperty, const Rml::PropertyId& _targetProperty);
		void updateElementDepthProperty(const TreeNodePtr& _node, Rml::Element& _elem, const std::string& _sourceProperty, const Rml::PropertyId& _targetProperty);
		static float getIndent(float _base, const TreeNodePtr& _node);

		Rml::Element* insertElement(const TreeNodePtr& _node, Rml::ElementPtr&& _elem);

		Tree m_tree;
		std::map<TreeNodePtr, Rml::Element*> m_activeNodeElements;
		std::map<TreeNodePtr, Rml::ElementPtr> m_inactiveNodeElements;
		ElemTreeNode* m_template = nullptr;
		Rml::ElementInstancer* m_instancer = nullptr;
		InstancerCallback m_instancerCallback;
	};
}

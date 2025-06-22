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

		explicit ElemTree(const Rml::String& _tag);
		~ElemTree() override;

		void OnChildAdd(Rml::Element* _child) override;

		void OnPropertyChange(const Rml::PropertyIdSet& changed_properties) override;
		void onPropertyChanged(const std::string& _key) override;

		void setNodeInstancer(Rml::ElementInstancer* _instancer);
		void setNodeInstancerCallback(InstancerCallback _callback);

		void childAdded(const TreeNodePtr& _parent, const TreeNodePtr& _child);
		void childRemoved(const TreeNodePtr& _parent, const TreeNodePtr& _child);

		Tree& getTree() { return m_tree; }

	private:
		void updateNodeElements();
		void updateNodeElements(const TreeNodePtr& _node);
		void createNodeElement(const TreeNodePtr& _node);

		void updateElementsDepthProperty(const std::string& _sourceProperty, const std::string& _targetProperty);
		void updateElementDepthProperty(const TreeNodePtr& _node, Rml::Element& _elem, const std::string& _sourceProperty, const std::string& _targetProperty);
		static float getIndent(float _base, const TreeNodePtr& _node);

		Tree m_tree;
		std::map<TreeNodePtr, Rml::Element*> m_activeNodeElements;
		ElemTreeNode* m_template = nullptr;
		Rml::ElementInstancer* m_instancer = nullptr;
		InstancerCallback m_instancerCallback;
	};
}

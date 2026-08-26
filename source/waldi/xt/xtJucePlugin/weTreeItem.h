#pragma once

#include "juceRmlUi/rmlDragSource.h"
#include "juceRmlUi/rmlDragTarget.h"
#include "juceRmlUi/rmlElemTreeNode.h"

namespace juce
{
	class Graphics;
}

namespace juceRmlUi
{
	class ElemCanvas;
}

namespace xtJucePlugin
{
	class TreeItem : public juceRmlUi::ElemTreeNode, public juceRmlUi::DragSource, public juceRmlUi::DragTarget
	{
	public:
		explicit TreeItem(Rml::CoreInstance& _coreInstance, const std::string& _tag);

		void setText(const std::string& _text);

	protected:
		virtual void paintItem(juce::Graphics& _g, const int _width, const int _height) = 0;

		void repaintItem() const;

		void OnChildAdd(Rml::Element* _child) override;

		void hideCanvas() const;

	private:
		std::string m_text;
		Rml::Element* m_elemText = nullptr;
		juceRmlUi::ElemCanvas* m_elemCanvas = nullptr;
	};
}

#include "weTreeItem.h"

#include "juceRmlUi/rmlElemCanvas.h"
#include "juceUiLib/treeViewStyle.h"

#include "juce_graphics/juce_graphics.h"

#include "RmlUi/Core/Context.h"

namespace xtJucePlugin
{
	TreeItem::TreeItem(Rml::CoreInstance& _coreInstance, const std::string& _tag) : juceRmlUi::ElemTreeNode(_coreInstance, _tag)
	{
		DragSource::init(this);
		DragTarget::init(this);
	}

	void TreeItem::setText(const std::string& _text)
	{
		if(_text == m_text)
			return;

		m_text = _text;

		if (m_elemText)
			m_elemText->SetInnerRML(Rml::StringUtilities::EncodeRml(m_text));

		if (auto* context = GetContext())
			context->RequestNextUpdate(0);
	}

	void TreeItem::repaintItem() const
	{
		if (m_elemCanvas)
			m_elemCanvas->repaint();
	}

	void TreeItem::OnChildAdd(Rml::Element* _child)
	{
		ElemTreeNode::OnChildAdd(_child);

		if (auto* canvas = dynamic_cast<juceRmlUi::ElemCanvas*>(_child))
		{
			m_elemCanvas = canvas;
			m_elemCanvas->setRepaintGraphicsCallback([this](const juce::Image& _image, juce::Graphics& _graphics)
			{
				paintItem(_graphics, _image.getWidth(), _image.getHeight());
			});
			m_elemCanvas->setClearEveryFrame(true);
		}
		else if (_child->GetId() == "name")
		{
			m_elemText = _child;

			if (!m_text.empty())
				m_elemText->SetInnerRML(Rml::StringUtilities::EncodeRml(m_text));
		}
	}

	void TreeItem::hideCanvas() const
	{
		if (m_elemCanvas)
			m_elemCanvas->SetProperty(Rml::PropertyId::Display, Rml::Style::Display::None);
	}
}

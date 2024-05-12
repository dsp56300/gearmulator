#include "focusedParameterTooltip.h"

#include "pluginEditor.h"

#include "juce_gui_basics/juce_gui_basics.h"

namespace jucePluginEditorLib
{
	FocusedParameterTooltip::FocusedParameterTooltip(juce::Label *_label, const genericUI::Editor &_editor) :
		m_label(_label), m_editor(_editor), m_defaultWidth(_label ? _label->getWidth() : 0)
	{
		setVisible(false);

		if(isValid())
			m_label->setInterceptsMouseClicks(false,false);
	}

	void FocusedParameterTooltip::setVisible(bool _visible) const
	{
		if (isValid())
			m_label->setVisible(_visible);
	}

	void FocusedParameterTooltip::initialize(juce::Component* _component, const juce::String& _value) const
	{
		if (!isValid())
			return;

		if(dynamic_cast<juce::Slider*>(_component) && _component->isShowing())
		{
			int x = _component->getX();
			int y = _component->getY();

			// local to global
			auto parent = _component->getParentComponent();

			while(parent && parent != m_label->getParentComponent())
			{
				x += parent->getX();
				y += parent->getY();
				parent = parent->getParentComponent();
			}

			int labelWidth = m_defaultWidth;

			if(_value.length() > 3)
				labelWidth += (m_label->getHeight()>>1) * (_value.length() - 3);

			x += (_component->getWidth()>>1) - (labelWidth>>1);
			y += _component->getHeight() + (m_label->getHeight()>>1);

			if(m_label->getProperties().contains("offsetY"))
				y += static_cast<int>(m_label->getProperties()["offsetY"]);

			m_label->setTopLeftPosition(x,y);
			m_label->setSize(labelWidth, m_label->getHeight());

			const auto editorRect = m_editor.getBounds();
			auto labelRect = m_label->getBounds();

			m_label->setBounds(labelRect.constrainedWithin(editorRect));
			m_label->setText(_value, juce::dontSendNotification);
			m_label->setVisible(true);
			m_label->toFront(false);
		}
		else if(m_label)
		{
			m_label->setVisible(false);
		}
	}
}

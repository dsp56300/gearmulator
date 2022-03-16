#include "Tabs.h"

#include "VirusEditor.h"

namespace genericVirusUI
{
	Tabs::Tabs(VirusEditor& _editor): m_editor(_editor)
	{
		m_tabs.push_back(m_editor.findComponent("page_osc"));
		m_tabs.push_back(m_editor.findComponent("page_lfo"));
		m_tabs.push_back(m_editor.findComponent("page_fx"));
		m_tabs.push_back(m_editor.findComponent("page_arp"));
		m_tabs.push_back(m_editor.findComponent("page_presets"));

		m_tabButtons.push_back(m_editor.findComponentT<juce::Button>("TabOsc"));
		m_tabButtons.push_back(m_editor.findComponentT<juce::Button>("TabLfo"));
		m_tabButtons.push_back(m_editor.findComponentT<juce::Button>("TabEffects"));
		m_tabButtons.push_back(m_editor.findComponentT<juce::Button>("TabArp"));
		m_tabButtons.push_back(m_editor.findComponentT<juce::Button>("Presets"));

		if(m_tabs.size() != m_tabButtons.size())
			throw std::runtime_error("Number of tabs does not match number of tab buttons, not all requested objects have been found");

		for(size_t i=0; i<m_tabButtons.size(); ++i)
			m_tabButtons[i]->onClick = [this, i] { setPage(i); };

		setPage(0);
	}

	void Tabs::setPage(const size_t _page) const
	{
		for(size_t i=0; i<m_tabs.size(); ++i)
		{
			m_tabs[i]->setVisible(_page == i);
			m_tabButtons[i]->setToggleState(_page == i, juce::dontSendNotification);
		}
	}
}

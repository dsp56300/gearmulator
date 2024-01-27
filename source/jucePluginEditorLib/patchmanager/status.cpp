#include "status.h"

namespace jucePluginEditorLib::patchManager
{
	void Status::setScanning(bool _scanning)
	{
		if(m_isScanning == _scanning)
			return;

		m_isScanning = _scanning;
		updateText();
	}

	void Status::setListStatus(uint32_t _selected, uint32_t _total)
	{
		if(m_listSelected == _selected && m_listTotal == _total)
			return;

		m_listSelected = _selected;
		m_listTotal = _total;
		updateText();
	}

	void Status::updateText()
	{
		if(m_isScanning)
		{
			setText("Scanning...");
		}
		else if(m_listSelected > 0 || m_listTotal > 0)
		{
			std::string t = std::to_string(m_listTotal) + " Patches";
			if(m_listSelected > 0)
				t += ", " + std::to_string(m_listSelected) + " selected";
			setText(t);
		}
		else
			setText({});
	}

	void Status::setText(const std::string& _text)
	{
		juce::Label::setText(_text, juce::dontSendNotification);
	}
}

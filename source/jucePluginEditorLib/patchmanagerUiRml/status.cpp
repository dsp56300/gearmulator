#include "status.h"

#include "patchmanagerDataModel.h"

namespace jucePluginEditorLib::patchManagerRml
{
	void Status::setScanning(const bool _scanning)
	{
		if(m_isScanning == _scanning)
			return;

		m_isScanning = _scanning;
		updateText();
	}

	void Status::setListStatus(const uint32_t _selected, const uint32_t _total)
	{
		if(m_listSelected == _selected && m_listTotal == _total)
			return;

		m_listSelected = _selected;
		m_listTotal = _total;
		updateText();
	}

	void Status::updateText() const
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
		{
			setText({});
		}
	}

	void Status::setText(const std::string& _text) const
	{
		m_dataModel.setStatus(_text);
	}
}

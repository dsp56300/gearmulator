#pragma once

#include "juce_gui_basics/juce_gui_basics.h"

namespace jucePluginEditorLib::patchManager
{
	class Status : public juce::Label
	{
	public:
		void setScanning(bool _scanning);
		void setListStatus(uint32_t _selected, uint32_t _total);

	private:
		void updateText();
		void setText(const std::string& _text);

		bool m_isScanning = false;
		uint32_t m_listSelected = 0;
		uint32_t m_listTotal = 0;
	};
}

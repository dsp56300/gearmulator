#pragma once

#include <string>
#include <cstdint>

namespace jucePluginEditorLib::patchManagerRml
{
	class PatchManagerDataModel;
}

namespace jucePluginEditorLib::patchManagerRml
{
	class Status
	{
	public:
		Status(PatchManagerDataModel& _dataModel) : m_dataModel(_dataModel) {}

		void setScanning(bool _scanning);
		void setListStatus(uint32_t _selected, uint32_t _total);

	private:
		void updateText() const;
		void setText(const std::string& _text) const;

		PatchManagerDataModel& m_dataModel;

		bool m_isScanning = false;

		uint32_t m_listSelected = 0;
		uint32_t m_listTotal = 0;
	};
}

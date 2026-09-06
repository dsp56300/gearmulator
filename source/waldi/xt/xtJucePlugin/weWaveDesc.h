#pragma once

#include "weData.h"

#include "juceRmlUi/rmlDragData.h"

namespace juce
{
	class File;
}

namespace juceRmlUi
{
	class DragSource;
}

namespace pluginLib
{
	class Processor;
}

namespace xtJucePlugin
{
	class WaveEditor;

	enum class WaveDescSource : uint8_t
	{
		Invalid,
		ControlTableList,
		WaveList,
		TablesList
	};

	struct WaveDesc : juceRmlUi::DragData
	{
		WaveDesc(WaveEditor& _editor) : m_editor(_editor) {}

		static WaveDesc* fromDragSource(const juceRmlUi::DragSource* _sourceDetails);
		static WaveDesc* fromDragData(juceRmlUi::DragData* _data);

		bool canExportAsFiles() const override;
		bool getFilesForExport(std::vector<std::string>& _files, bool& _filesAreTemporary) override;

		bool writeToFile(const juce::File& _file) const;

		void fillData(const WaveEditorData& _data);

		std::string getExportFileName(const pluginLib::Processor& _processor) const;

		WaveEditor& m_editor;

		std::vector<xt::WaveId> waveIds;
		std::vector<xt::TableId> tableIds;

		xt::TableIndex tableIndex;

		std::vector<xt::WaveData> waveDatas;
		std::vector<xt::TableData> tableDatas;

		WaveDescSource source = WaveDescSource::Invalid;
	};
}

#pragma once

#include "weData.h"

#include "jucePluginEditorLib/dragAndDropObject.h"

#include "juce_gui_basics/juce_gui_basics.h"

namespace xtJucePlugin
{
	class WaveEditor;

	enum class WaveDescSource
	{
		Invalid,
		ControlTableList,
		WaveList,
		TablesList
	};

	struct WaveDesc : jucePluginEditorLib::DragAndDropObject
	{
		WaveDesc(WaveEditor& _editor) : m_editor(_editor) {}

		static WaveDesc* fromDragSource(const juce::DragAndDropTarget::SourceDetails& _sourceDetails);

		bool writeToFile(const juce::File& _file) const override;
		bool canDropExternally() const override;
		std::string getExportFileName(const pluginLib::Processor& _processor) const override;

		void fillData(const WaveEditorData& _data);

		WaveEditor& m_editor;

		xt::WaveId waveId;
		xt::TableId tableId;

		xt::TableIndex tableIndex;

		std::optional<xt::WaveData> waveData;
		std::optional<xt::TableData> tableData;

		WaveDescSource source = WaveDescSource::Invalid;
	};
}

#include "weWaveDesc.h"

#include "weWaveTreeItem.h"
#include "xtWaveEditor.h"

#include "synthLib/sysexToMidi.h"

namespace xtJucePlugin
{
	WaveDesc* WaveDesc::fromDragSource(const juce::DragAndDropTarget::SourceDetails& _sourceDetails)
	{
		auto* desc = dynamic_cast<WaveDesc*>(_sourceDetails.description.getObject());
		return desc;
	}

	bool WaveDesc::writeToFile(const juce::File& _file) const
	{
		std::vector<xt::SysEx> sysex;

		if(tableData)
			sysex.emplace_back(xt::State::createTableData(*tableData, tableId.rawId(), false));
		if(waveData)
			sysex.emplace_back(xt::State::createWaveData(*waveData, waveId.rawId(), false));

		if(sysex.empty())
			return false;

		return synthLib::SysexToMidi::write(_file.getFullPathName().toStdString().c_str(), sysex);
	}

	bool WaveDesc::canDropExternally() const
	{
		return waveData || tableData;
	}

	std::string WaveDesc::getExportFileName(const pluginLib::Processor& _processor) const
	{
		std::stringstream name;
		name << _processor.getProperties().name << " - ";

		if(tableData)
			name << "Table " << m_editor.getTableName(tableId);
		else if(waveData)
			name << "Wave " << WaveTreeItem::getWaveName(waveId);
		else
			return DragAndDropObject::getExportFileName(_processor);

		name << ".mid";

		return name.str();
	}

	void WaveDesc::fillData(const WaveEditorData& _data)
	{
		waveData = _data.getWave(waveId);
		tableData = _data.getTable(tableId);
	}
}

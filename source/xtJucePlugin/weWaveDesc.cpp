#include "weWaveDesc.h"

#include "weWaveTreeItem.h"
#include "xtWaveEditor.h"

#include "baseLib/filesystem.h"

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

		sysex.reserve(tableIds.size() + waveIds.size());

		for (size_t i = 0; i<tableIds.size(); ++i)
			sysex.emplace_back(xt::State::createTableData(tableDatas[i], tableIds[i].rawId(), false));

		for (size_t i = 0; i < waveIds.size(); ++i)
			sysex.emplace_back(xt::State::createWaveData(waveDatas[i], waveIds[i].rawId(), false));

		if(sysex.empty())
			return false;

		if (_file.getFullPathName().endsWithIgnoreCase(".syx"))
		{
			std::vector<uint8_t> data;
			for (const auto& s : sysex)
				data.insert(data.end(), s.begin(), s.end());
			return baseLib::filesystem::writeFile(_file.getFullPathName().toStdString(), data);
		}
		return synthLib::SysexToMidi::write(_file.getFullPathName().toStdString().c_str(), sysex);
	}

	bool WaveDesc::canDropExternally() const
	{
		return !waveDatas.empty() || !tableDatas.empty();
	}

	std::string WaveDesc::getExportFileName(const pluginLib::Processor& _processor) const
	{
		std::stringstream name;
		name << _processor.getProperties().name << " - ";

		if (tableIds.size() > 1)
			name << "Table " << m_editor.getTableName(tableIds.front()) << " - " << m_editor.getTableName(tableIds.back());
		else if (waveIds.size() > 1)
			name << "Wave " << WaveTreeItem::getWaveName(waveIds.front()) << " - " << WaveTreeItem::getWaveName(waveIds.back());
		else if(!tableDatas.empty())
			name << "Table " << m_editor.getTableName(tableIds.front());
		else if(!waveDatas.empty())
			name << "Wave " << WaveTreeItem::getWaveName(waveIds.front());
		else
			return DragAndDropObject::getExportFileName(_processor);

		if (juce::ModifierKeys::getCurrentModifiers().isShiftDown())
			name << ".syx";
		else
			name << ".mid";

		return name.str();
	}

	void WaveDesc::fillData(const WaveEditorData& _data)
	{
		for (int32_t i=0; i<static_cast<int32_t>(waveIds.size()); ++i)
		{
			if (auto wave = _data.getWave(waveIds[i]))
				waveDatas.push_back(*wave);
			else
				waveIds.erase(waveIds.begin() + i);
		}
		for (int32_t i = 0; i < static_cast<int32_t>(tableIds.size()); ++i)
		{
			if (auto table = _data.getTable(tableIds[i]))
				tableDatas.push_back(*table);
			else
				tableIds.erase(tableIds.begin() + i);
		}
	}
}

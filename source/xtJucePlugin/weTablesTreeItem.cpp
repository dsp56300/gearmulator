#include "weTablesTreeItem.h"

#include "weWaveDesc.h"
#include "weWaveTreeItem.h"
#include "xtController.h"
#include "xtEditor.h"
#include "xtWaveEditor.h"

namespace xtJucePlugin
{
	TablesTreeItem::TablesTreeItem(WaveEditor& _editor, const xt::TableId _tableIndex) : m_editor(_editor), m_index(_tableIndex)
	{
		setPaintRootItemInBold(false);
		setDrawsInLeftMargin(true);

		const auto name = _editor.getTableName(_tableIndex);

		setText(name);

		m_onTableChanged.set(_editor.getData().onTableChanged, [this](const xt::TableId& _index)
		{
			onTableChanged(_index);
		});
	}

	void TablesTreeItem::itemSelectionChanged(const bool _isNowSelected)
	{
		TreeItem::itemSelectionChanged(_isNowSelected);
		if(_isNowSelected)
			m_editor.setSelectedTable(m_index);
	}

	juce::var TablesTreeItem::getDragSourceDescription()
	{
		auto* waveDesc = new WaveDesc(m_editor);

		waveDesc->tableId = m_index;
		waveDesc->source = WaveDescSource::TablesList;

		waveDesc->fillData(m_editor.getData());

		return waveDesc;
	}

	bool TablesTreeItem::isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& dragSourceDetails)
	{
		if(xt::wave::isReadOnly(m_index))
			return false;
		const auto* waveDesc = WaveDesc::fromDragSource(dragSourceDetails);
		if(!waveDesc || waveDesc->source != WaveDescSource::TablesList)
			return false;
		return waveDesc->tableId != m_index;
	}

	void TablesTreeItem::itemDropped(const juce::DragAndDropTarget::SourceDetails& dragSourceDetails, int insertIndex)
	{
		TreeItem::itemDropped(dragSourceDetails, insertIndex);

		const auto* waveDesc = WaveDesc::fromDragSource(dragSourceDetails);

		if(!waveDesc || waveDesc->source != WaveDescSource::TablesList || waveDesc->tableId == m_index)
			return;

		auto& data = m_editor.getData();
		if(data.copyTable(m_index, waveDesc->tableId))
		{
			setSelected(true, true, juce::dontSendNotification);
			m_editor.setSelectedTable(m_index);
			data.sendTableToDevice(m_index);
		}
	}

	bool TablesTreeItem::isInterestedInFileDrag(const juce::StringArray& files)
	{
		if(xt::wave::isReadOnly(getTableId()))
			return false;

		if(files.size() == 1 && files[0].endsWithIgnoreCase(".mid") || files[1].endsWithIgnoreCase(".syx"))
			return true;

		return TreeItem::isInterestedInFileDrag(files);
	}

	void TablesTreeItem::filesDropped(const juce::StringArray& files, int insertIndex)
	{
		if(xt::wave::isReadOnly(getTableId()))
			return;

		const auto errorTitle = m_editor.getEditor().getProcessor().getProperties().name + " - Error";

		const auto sysex = WaveTreeItem::getSysexFromFiles(files);

		if(sysex.empty())
		{
			juce::NativeMessageBox::showMessageBox(juce::AlertWindow::WarningIcon, errorTitle, "No Sysex data found in file");
			return;
		}

		std::vector<xt::TableData> tables;

		for (const auto& s : sysex)
		{
			xt::TableData table;
			if (xt::State::parseTableData(table, s))
				tables.push_back(table);
		}

		if(tables.size() == 1)
		{
			m_editor.getData().setTable(m_index, tables.front());
			return;
		}

		juce::NativeMessageBox::showMessageBox(juce::AlertWindow::WarningIcon, errorTitle, tables.empty() ? "No Control Table found in files" : "Multiple control tables found in file");
	}

	void TablesTreeItem::itemClicked(const juce::MouseEvent& _mouseEvent)
	{
		if(!_mouseEvent.mods.isPopupMenu())
		{
			TreeItem::itemClicked(_mouseEvent);
			return;
		}

		juce::PopupMenu menu;

		juce::PopupMenu copyToTableSubMenu;

		for(auto i = xt::wave::g_firstRamTableIndex; i < xt::wave::g_tableCount; ++i)
		{
			if(i > xt::wave::g_firstRamTableIndex && (i&7) == 0)
				copyToTableSubMenu.addColumnBreak();

			const auto id = xt::TableId(i);
			copyToTableSubMenu.addItem(m_editor.getTableName(id), [this, id]
			{
				m_editor.getData().copyTable(id, m_index);
			});
		}

		menu.addSubMenu("Copy to", copyToTableSubMenu);

		menu.showMenuAsync({});
	}

	void TablesTreeItem::onTableChanged(xt::TableId _index)
	{
		if(_index != m_index)
			return;
		onTableChanged();
	}

	void TablesTreeItem::onTableChanged()
	{
	}
}

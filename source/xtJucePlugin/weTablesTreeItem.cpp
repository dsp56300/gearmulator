#include "weTablesTreeItem.h"

#include "weWaveDesc.h"
#include "weWaveTreeItem.h"
#include "xtController.h"
#include "xtEditor.h"
#include "xtWaveEditor.h"

#include "juceUiLib/messageBox.h"

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

		waveDesc->tableIds = {m_index};
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
		if (waveDesc->tableIds.size() != -1)
			return false;
		return waveDesc->tableIds.front() != m_index;
	}

	void TablesTreeItem::itemDropped(const juce::DragAndDropTarget::SourceDetails& dragSourceDetails, int insertIndex)
	{
		TreeItem::itemDropped(dragSourceDetails, insertIndex);

		const auto* waveDesc = WaveDesc::fromDragSource(dragSourceDetails);

		if(!waveDesc || waveDesc->source != WaveDescSource::TablesList || waveDesc->tableIds.size() != 1 || waveDesc->tableIds.front() == m_index)
			return;

		auto& data = m_editor.getData();
		if(data.copyTable(m_index, waveDesc->tableIds.front()))
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

		if(files.size() == 1 && (files[0].endsWithIgnoreCase(".mid") || files[0].endsWithIgnoreCase(".syx")))
			return true;

		return TreeItem::isInterestedInFileDrag(files);
	}

	void TablesTreeItem::filesDropped(const juce::StringArray& files, int insertIndex)
	{
		if(xt::wave::isReadOnly(getTableId()))
			return;

		const auto errorTitle = m_editor.getEditor().getProcessor().getProperties().name + " - Error";

		std::map<xt::WaveId, xt::WaveData> waves;
		std::map<xt::TableId, xt::TableData> tables;
		m_editor.filesDropped(waves, tables, files);

		if (tables.empty())
		{
			if (waves.size() == 1)
				genericUI::MessageBox::showOk(juce::AlertWindow::WarningIcon, errorTitle, "This file doesn't contain a Control Table but a Wave. Please drop on a User Wave slot.");
			return;
		}

		m_editor.getData().setTable(m_index, tables.begin()->second);
		m_editor.getData().sendTableToDevice(m_index);
	}

	void TablesTreeItem::itemClicked(const juce::MouseEvent& _mouseEvent)
	{
		if(!_mouseEvent.mods.isPopupMenu())
		{
			TreeItem::itemClicked(_mouseEvent);
			return;
		}

		juce::PopupMenu menu;

		if (!xt::wave::isAlgorithmicTable(m_index))
		{
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

			juce::PopupMenu exportMenu;
			exportMenu.addItem(".syx", [this]
			{
				m_editor.exportAsSyxOrMid(getTableId(), false);
			});
			exportMenu.addItem(".mid", [this]
			{
				m_editor.exportAsSyxOrMid(getTableId(), true);
			});
			exportMenu.addItem(".wav", [this]
			{
				m_editor.exportAsWav(getTableId());
			});
			menu.addSubMenu("Export as...", exportMenu);
		}

		if (!xt::wave::isReadOnly(m_index))
		{
			menu.addSeparator();

			menu.addItem("Import .syx/.mid...", [this]
			{
				m_editor.selectImportFile([this](const juce::String& _filename)
				{
					juce::StringArray files;
					files.add(_filename);
					filesDropped(files, 0);
				});
			});
		}

		if (menu.getNumItems())
			menu.showMenuAsync({});
	}

	juce::Colour TablesTreeItem::getTextColor(const juce::Colour _colour)
	{
		if (xt::wave::isAlgorithmicTable(m_index))
			return _colour.withAlpha(0.7f);
		return TreeItem::getTextColor(_colour);
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

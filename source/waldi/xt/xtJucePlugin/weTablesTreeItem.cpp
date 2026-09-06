#include "weTablesTreeItem.h"

#include "weWaveDesc.h"
#include "weWaveTreeItem.h"
#include "xtController.h"
#include "xtEditor.h"
#include "xtWaveEditor.h"
#include "baseLib/filesystem.h"
#include "jucePluginEditorLib/pluginProcessor.h"

#include "juceUiLib/messageBox.h"

namespace xtJucePlugin
{
	TablesTreeItem::TablesTreeItem(Rml::CoreInstance& _coreInstance, const std::string& _tag, WaveEditor& _editor)
	: TreeItem(_coreInstance, _tag)
	, m_editor(_editor)
	{
		m_onTableChanged.set(_editor.getData().onTableChanged, [this](const xt::TableId& _index)
		{
			onTableChanged(_index);
		});
	}

	void TablesTreeItem::setNode(const juceRmlUi::TreeNodePtr& _node)
	{
		TreeItem::setNode(_node);

		SetPseudoClass("x-table-algorithmic", xt::wave::isAlgorithmicTable(getTableId()));
		SetPseudoClass("x-table-readonly", xt::wave::isReadOnly(getTableId()));

		const auto name = m_editor.getTableName(getTableId());

		setText(name);
	}

	xt::TableId TablesTreeItem::getTableId() const
	{
		auto* node = dynamic_cast<TablesTreeNode*>(getNode().get());
		if (node)
			return node->getTableId();
		return xt::TableId::invalid();
	}

	void TablesTreeItem::onSelectedChanged(bool _selected)
	{
		TreeItem::onSelectedChanged(_selected);
		if (_selected)
			m_editor.setSelectedTable(getTableId());
	}

	std::unique_ptr<juceRmlUi::DragData> TablesTreeItem::createDragData()
	{
		auto waveDesc = std::make_unique<WaveDesc>(m_editor);

		waveDesc->tableIds = {getTableId()};
		waveDesc->source = WaveDescSource::TablesList;

		waveDesc->fillData(m_editor.getData());

		return waveDesc;
	}

	bool TablesTreeItem::canDrop(const Rml::Event& _event, const DragSource* _source)
	{
		if(xt::wave::isReadOnly(getTableId()))
			return TreeItem::canDrop(_event, _source);
		const auto* waveDesc = WaveDesc::fromDragSource(_source);
		if(!waveDesc || waveDesc->source != WaveDescSource::TablesList)
			return TreeItem::canDrop(_event, _source);
		if (waveDesc->tableIds.size() != 1)
			return TreeItem::canDrop(_event, _source);
		return waveDesc->tableIds.front() != getTableId();
	}

	void TablesTreeItem::drop(const Rml::Event& _event, const DragSource* _source, const juceRmlUi::DragData* _data)
	{
		TreeItem::drop(_event, _source, _data);

		const auto* waveDesc = WaveDesc::fromDragSource(_source);

		if(!waveDesc || waveDesc->source != WaveDescSource::TablesList || waveDesc->tableIds.size() != 1 || waveDesc->tableIds.front() == getTableId())
			return;

		auto& data = m_editor.getData();

		if(data.copyTable(getTableId(), waveDesc->tableIds.front()))
		{
			getNode()->setSelected(true);
			m_editor.setSelectedTable(getTableId());
			data.sendTableToDevice(getTableId());
		}
	}

	bool TablesTreeItem::canDropFiles(const Rml::Event& _event, const std::vector<std::string>& _files)
	{
		if(xt::wave::isReadOnly(getTableId()))
			return false;

		if(_files.size() == 1 && (baseLib::filesystem::hasExtension(_files.front(), ".mid") || baseLib::filesystem::hasExtension(_files.front(), ".syx")))
			return true;

		return TreeItem::canDropFiles(_event, _files);
	}

	void TablesTreeItem::dropFiles(const Rml::Event& _event, const juceRmlUi::FileDragData* _data, const std::vector<std::string>& _files)
	{
		dropFiles(_files);
	}

	void TablesTreeItem::dropFiles(const std::vector<std::string>& _files)
	{
		if(xt::wave::isReadOnly(getTableId()))
			return;

		const auto errorTitle = m_editor.getEditor().getProcessor().getProperties().name + " - Error";

		std::map<xt::WaveId, xt::WaveData> waves;
		std::map<xt::TableId, xt::TableData> tables;
		m_editor.filesDropped(waves, tables, _files);

		if (tables.empty())
		{
			if (waves.size() == 1)
				genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning, errorTitle, "This file doesn't contain a Control Table but a Wave. Please drop on a User Wave slot.");
			return;
		}

		m_editor.getData().setTable(getTableId(), tables.begin()->second);
		m_editor.getData().sendTableToDevice(getTableId());
	}

	void TablesTreeItem::openContextMenu(const Rml::Event& _event)
	{
		juceRmlUi::Menu menu;

		if (!xt::wave::isAlgorithmicTable(getTableId()))
		{
			juceRmlUi::Menu copyToTableSubMenu;

			for(auto i = xt::wave::g_firstRamTableIndex; i < xt::wave::g_tableCount; ++i)
			{
				const auto id = xt::TableId(i);
				copyToTableSubMenu.addEntry(m_editor.getTableName(id), [this, id]
				{
					m_editor.getData().copyTable(id, getTableId());
				});
			}

			menu.addSubMenu("Copy to", std::move(copyToTableSubMenu));

			juceRmlUi::Menu exportMenu;
			exportMenu.addEntry(".syx", [this]
			{
				m_editor.exportAsSyxOrMid(getTableId(), false);
			});
			exportMenu.addEntry(".mid", [this]
			{
				m_editor.exportAsSyxOrMid(getTableId(), true);
			});
			exportMenu.addEntry(".wav", [this]
			{
				m_editor.exportAsWav(getTableId());
			});
			menu.addSubMenu("Export as...", std::move(exportMenu));
		}

		if (!xt::wave::isReadOnly(getTableId()))
		{
			menu.addSeparator();

			menu.addEntry("Import .syx/.mid...", [this]
			{
				m_editor.selectImportFile([this](const juce::String& _filename)
				{
					const std::vector<std::string> files{_filename.toStdString()};
					dropFiles(files);
				});
			});
		}

		if (!menu.empty())
			menu.runModal(_event);
	}

	void TablesTreeItem::paintItem(juce::Graphics& _g, const int _width, const int _height)
	{
		_g.fillAll(juce::Colours::magenta);
	}

	void TablesTreeItem::onTableChanged(const xt::TableId _index)
	{
		if(_index != getTableId())
			return;
		onTableChanged();
	}

	void TablesTreeItem::onTableChanged()
	{
	}
}

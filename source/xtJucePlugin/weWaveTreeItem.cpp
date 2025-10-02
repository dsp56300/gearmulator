#include "weWaveTreeItem.h"

#include "weWaveCategoryTreeItem.h"
#include "weWaveDesc.h"
#include "xtEditor.h"
#include "xtWaveEditor.h"
#include "baseLib/filesystem.h"
#include "jucePluginEditorLib/pluginProcessor.h"

#include "juceUiLib/messageBox.h"

#include "synthLib/midiToSysex.h"

namespace xtJucePlugin
{
	WaveTreeItem::WaveTreeItem(Rml::CoreInstance& _coreInstance, const std::string& _tag, WaveEditor& _editor)
		: TreeItem(_coreInstance, _tag)
		, m_editor(_editor)
	{
		m_onWaveChanged.set(m_editor.getData().onWaveChanged, [this](const xt::WaveId& _waveIndex)
		{
			onWaveChanged(_waveIndex);
		});
	}

	xt::WaveId WaveTreeItem::getWaveId() const
	{
		if (const auto& node = getNode())
		{
			if (const auto* waveNode = dynamic_cast<WaveTreeNode*>(node.get()))
				return waveNode->getWaveId();
		}
		return xt::WaveId::invalid();
	}

	void WaveTreeItem::setNode(const juceRmlUi::TreeNodePtr& _node)
	{
		TreeItem::setNode(_node);

		setText(getWaveName(getWaveId()));
	}

	void WaveTreeItem::paintWave(const xt::WaveData& _data, juce::Graphics& _g, const int _x, const int _y, const int _width, const int _height, const juce::Colour& _colour)
	{
		_g.setColour(_colour);

		const float scaleX = static_cast<float>(_width)  / static_cast<float>(_data.size());
		const float scaleY = static_cast<float>(_height) / static_cast<float>(256);

		for(uint32_t x=1; x<_data.size(); ++x)
		{
			const auto x0 = static_cast<float>(x - 1) * scaleX + static_cast<float>(_x);
			const auto x1 = static_cast<float>(x    ) * scaleX + static_cast<float>(_x);

			const auto y0 = static_cast<float>(255 - (_data[x - 1] + 128)) * scaleY + static_cast<float>(_y);
			const auto y1 = static_cast<float>(255 - (_data[x    ] + 128)) * scaleY + static_cast<float>(_y);

			_g.drawLine(x0, y0, x1, y1);
		}
	}

	std::string WaveTreeItem::getWaveName(const xt::WaveId _waveIndex)
	{
		if(!xt::wave::isValidWaveIndex(_waveIndex.rawId()))
			return {};
		const auto category = getCategory(_waveIndex);
		return WaveCategoryTreeItem::getCategoryName(category) + ' ' + std::to_string(_waveIndex.rawId());
	}

	WaveCategory WaveTreeItem::getCategory(const xt::WaveId _waveIndex)
	{
		if(_waveIndex.rawId() < xt::wave::g_romWaveCount)
			return WaveCategory::Rom;
		if(_waveIndex.rawId() >= xt::wave::g_firstRamWaveIndex && _waveIndex.rawId() < xt::wave::g_firstRamWaveIndex + xt::wave::g_ramWaveCount)
			return WaveCategory::User;
		return WaveCategory::Invalid;
	}

	void WaveTreeItem::onSelectedChanged(bool _selected)
	{
		TreeItem::onSelectedChanged(_selected);

		if(_selected)
			m_editor.setSelectedWave(getWaveId());
	}

	std::unique_ptr<juceRmlUi::DragData> WaveTreeItem::createDragData()
	{
		auto desc = std::make_unique<WaveDesc>(m_editor);

		desc->waveIds = {getWaveId()};
		desc->source = WaveDescSource::WaveList;

		desc->fillData(m_editor.getData());

		return desc;
	}

	bool WaveTreeItem::canDrop(const Rml::Event& _event, const DragSource* _source)
	{
		if(xt::wave::isReadOnly(getWaveId()))
			return TreeItem::canDrop(_event, _source);
		const auto* waveDesc = WaveDesc::fromDragSource(_source);
		if(!waveDesc)
			return TreeItem::canDrop(_event, _source);
		if(waveDesc->waveIds.size() != 1)
			return TreeItem::canDrop(_event, _source);
		return true;
		
	}

	void WaveTreeItem::drop(const Rml::Event& _event, const DragSource* _source, const juceRmlUi::DragData* _data)
	{
		TreeItem::drop(_event, _source, _data);

		if(xt::wave::isReadOnly(getWaveId()))
			return;
		const auto* waveDesc = WaveDesc::fromDragSource(_source);
		if(!waveDesc)
			return;
		auto& data = m_editor.getData();

		if(data.copyWave(getWaveId(), waveDesc->waveIds.front()))
		{
			getNode()->setSelected(true, false);
			data.sendWaveToDevice(getWaveId());
		}
	}

	bool WaveTreeItem::canDropFiles(const Rml::Event& _event, const std::vector<std::string>& _files)
	{
		if(xt::wave::isReadOnly(getWaveId()))
			return TreeItem::canDropFiles(_event, _files);

		if (_files.size() != 1)
			return TreeItem::canDropFiles(_event, _files);

		const auto& f = _files.front();

		const auto ext = baseLib::filesystem::getExtension(f);

		if(baseLib::filesystem::hasExtension(f, ".mid") || baseLib::filesystem::hasExtension(f, ".syx") || baseLib::filesystem::hasExtension(f, ".wav"))
			return true;

		return TreeItem::canDropFiles(_event, _files);
	}

	void WaveTreeItem::dropFiles(const Rml::Event& _event, const juceRmlUi::FileDragData* _data, const std::vector<std::string>& _files)
	{
		dropFiles(_files);
	}

	void WaveTreeItem::dropFiles(const std::vector<std::string>& _files) const
	{
		if(xt::wave::isReadOnly(getWaveId()))
			return;

		if (_files.empty())
			return;

		if (baseLib::filesystem::hasExtension(_files.front(), ".wav"))
		{
			if (auto wave = m_editor.importWaveFile(_files.front()))
			{
				m_editor.getData().setWave(getWaveId(), *wave);
				m_editor.getData().sendWaveToDevice(getWaveId());
			}
			return;
		}

		const auto errorTitle = m_editor.getEditor().getProcessor().getProperties().name + " - Error";

		std::map<xt::WaveId, xt::WaveData> waves;
		std::map<xt::TableId, xt::TableData> tables;
		m_editor.filesDropped(waves, tables, _files);

		if (waves.empty())
		{
			if (tables.size() == 1)
				genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning, errorTitle, "This file doesn't contain a Wave but a Control Table, please drop on a User Table slot.");

			return;
		}

		m_editor.getData().setWave(getWaveId(), waves.begin()->second);
		m_editor.getData().sendWaveToDevice(getWaveId());
	}

	std::vector<std::vector<uint8_t>> WaveTreeItem::getSysexFromFiles(const std::vector<std::string>& _files)
	{
		std::vector<std::vector<uint8_t>> sysex;

		for(const auto& file : _files)
		{
			std::vector<std::vector<uint8_t>> s;
			synthLib::MidiToSysex::extractSysexFromFile(s, file);
			sysex.insert(sysex.end(), s.begin(), s.end());
		}

		return sysex;
	}

	void WaveTreeItem::openContextMenu(const Rml::Event& _event)
	{
		juceRmlUi::Menu menu;

		const auto selectedTableId = m_editor.getSelectedTable();

		if(selectedTableId.isValid())
		{
			auto subMenuCT = m_editor.createCopyToSelectedTableMenu(getWaveId());
			menu.addSubMenu("Copy to current Control Table...", std::move(subMenuCT));
		}

		auto subMenuUW = WaveEditor::createRamWavesPopupMenu([this](const xt::WaveId _dest)
		{
			if (m_editor.getData().copyWave(_dest, getWaveId()))
			{
				m_editor.getData().sendWaveToDevice(_dest);
				m_editor.setSelectedWave(_dest);
			}
		});
		menu.addSubMenu("Copy to User Wave...", std::move(subMenuUW));
		menu.addSeparator();

		if (auto wave = m_editor.getData().getWave(getWaveId()))
		{
			auto w = *wave;

			juceRmlUi::Menu exportMenu;

			if (xt::wave::isReadOnly(getWaveId()))
			{
				// we cannot export a .mid or .syx that is a rom location as the hardware cannot import it
				auto subMenuSyx = WaveEditor::createRamWavesPopupMenu([this, w](const xt::WaveId _id)
				{
					m_editor.exportAsSyx(_id, w);
				});
				exportMenu.addSubMenu(".syx", std::move(subMenuSyx));
				auto subMenuMid = WaveEditor::createRamWavesPopupMenu([this, w](const xt::WaveId _id)
				{
					m_editor.exportAsMid(_id, w);
				});
				exportMenu.addSubMenu(".mid", std::move(subMenuMid));
			}
			else
			{
				exportMenu.addEntry(".syx", [this, w]
				{
					m_editor.exportAsSyx(getWaveId(), w);
				});
				exportMenu.addEntry(".mid", [this, w]
				{
					m_editor.exportAsMid(getWaveId(), w);
				});
			}
			exportMenu.addEntry(".wav", [this, w]
			{
				m_editor.exportAsWav(w);
			});
			menu.addSubMenu("Export as...", std::move(exportMenu));
		}

		if (!xt::wave::isReadOnly(getWaveId()))
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

		menu.runModal(_event);
	}

	void WaveTreeItem::onWaveChanged(const xt::WaveId _index) const
	{
		if(_index != getWaveId())
			return;
		onWaveChanged();
	}

	void WaveTreeItem::onWaveChanged() const
	{
		repaintItem();
	}

	void WaveTreeItem::paintItem(juce::Graphics& g, const int _width, const int _height)
	{
		if(const auto wave = m_editor.getData().getWave(getWaveId()))
			paintWave(*wave, g, 0, 0, _width, _height, juce::Colour(0xffffffff));
	}
}

#include "xtWaveEditor.h"

#include "weWaveTree.h"
#include "weTablesTree.h"
#include "weControlTree.h"
#include "weGraphFreq.h"
#include "weGraphPhase.h"
#include "weGraphTime.h"
#include "weWaveTreeItem.h"
#include "xtController.h"

#include "xtEditor.h"

#include "jucePluginEditorLib/pluginProcessor.h"

#include "juceUiLib/messageBox.h"

#include "xtLib/xtState.h"

namespace xtJucePlugin
{
	WaveEditor::WaveEditor(Editor& _editor, const juce::File& _cacheDir) : ComponentMovementWatcher(this), m_editor(_editor), m_data(_editor.getXtController(), _cacheDir.getFullPathName().toStdString())
	{
		addComponentListener(this);

		m_data.onWaveChanged.addListener([this](const xt::WaveId& _waveIndex)
		{
			if(_waveIndex != m_selectedWave)
				return;

			setSelectedWave(_waveIndex, true);
		});

		m_graphData.onIntegerChanged.addListener([this](const xt::WaveData& _data)
		{
			onWaveDataChanged(_data);
		});
	}

	WaveEditor::~WaveEditor()
	{
		destroy();
		removeComponentListener(this);
	}

	void WaveEditor::initialize()
	{
		auto* waveListParent = m_editor.findComponent("wecWaveList");
		auto* tablesListParent = m_editor.findComponent("wecWavetableList");
		auto* controlListParent = m_editor.findComponent("wecWaveControlTable");
		auto* waveFreqParent = m_editor.findComponent("wecWaveFreq");
		auto* wavePhaseParent = m_editor.findComponent("wecWavePhase");
		auto* waveTimeParent = m_editor.findComponent("wecWaveTime");

		m_waveTree.reset(new WaveTree(*this));
		m_tablesTree.reset(new TablesTree(*this));
		m_controlTree.reset(new ControlTree(*this));

		m_graphFreq.reset(new GraphFreq(*this));
		m_graphPhase.reset(new GraphPhase(*this));
		m_graphTime.reset(new GraphTime(*this));

		waveListParent->addAndMakeVisible(m_waveTree.get());
		tablesListParent->addAndMakeVisible(m_tablesTree.get());
		controlListParent->addAndMakeVisible(m_controlTree.get());

		waveFreqParent->addAndMakeVisible(m_graphFreq.get());
		wavePhaseParent->addAndMakeVisible(m_graphPhase.get());
		waveTimeParent->addAndMakeVisible(m_graphTime.get());

		constexpr auto colourId = juce::TreeView::ColourIds::backgroundColourId;
		const auto colour = m_waveTree->findColour(colourId);
		m_graphFreq->setColour(colourId, colour);
		m_graphPhase->setColour(colourId, colour);
		m_graphTime->setColour(colourId, colour);

		m_tablesTree->setSelectedEntryFromCurrentPreset();
	}

	void WaveEditor::destroy()
	{
		m_waveTree.reset();
		m_tablesTree.reset();
		m_controlTree.reset();
		m_graphFreq.reset();
		m_graphPhase.reset();
		m_graphTime.reset();
	}

	void WaveEditor::checkFirstTimeVisible()
	{
		if(isShowing() && !m_wasVisible)
		{
			m_wasVisible = true;
			onFirstTimeVisible();
		}
	}

	void WaveEditor::onFirstTimeVisible()
	{
		m_data.requestData();
	}

	void WaveEditor::toggleWavePreview(const bool _enabled)
	{
		if(_enabled)
			toggleWavetablePreview(false);
	}

	void WaveEditor::toggleWavetablePreview(const bool _enabled)
	{
		if(_enabled)
			toggleWavePreview(false);
	}

	void WaveEditor::onWaveDataChanged(const xt::WaveData&) const
	{
	}

	bool WaveEditor::saveWaveTo(const xt::WaveId _target)
	{
		if(xt::wave::isReadOnly(_target))
			return false;

		if(!m_data.setWave(_target, m_graphData.getSource()))
			return false;

		m_data.sendWaveToDevice(_target);

		if(_target != m_selectedWave)
			setSelectedWave(_target);

		return true;
	}

	void WaveEditor::onReceiveWave(const pluginLib::MidiPacket::Data& _data, const std::vector<uint8_t>& _msg)
	{
		m_data.onReceiveWave(_msg);
	}

	void WaveEditor::onReceiveTable(const pluginLib::MidiPacket::Data& _data, const std::vector<uint8_t>& _msg)
	{
		m_data.onReceiveTable(_msg);
	}

	void WaveEditor::setSelectedTable(xt::TableId _index)
	{
		if(m_selectedTable == _index)
			return;

		m_selectedTable = _index;
		m_controlTree->setTable(_index);
		m_tablesTree->setSelectedTable(_index);
	}

	void WaveEditor::setSelectedWave(const xt::WaveId _waveIndex, bool _forceRefresh/* = false*/)
	{
		if(m_selectedWave == _waveIndex && !_forceRefresh)
			return;

		m_selectedWave = _waveIndex;

		m_waveTree->setSelectedWave(m_selectedWave);

		if(const auto wave = m_data.getWave(_waveIndex))
		{
			m_graphData.set(*wave);
			onWaveDataChanged(*wave);
		}
	}

	std::string WaveEditor::getTableName(const xt::TableId _id) const
	{
		const auto& wavetableNames = getEditor().getXtController().getParameterDescriptions().getValueList("waveType");
		return wavetableNames->valueToText(_id.rawId());
	}

	juce::PopupMenu WaveEditor::createCopyToSelectedTableMenu(xt::WaveId _id)
	{
		juce::PopupMenu controlTableSlotsMenu;
		for(uint16_t i=0; i<xt::wave::g_wavesPerTable; ++i)
		{
			const auto tableIndex = xt::TableIndex(i);

			if(i && (i & 15) == 0)
				controlTableSlotsMenu.addColumnBreak();

			controlTableSlotsMenu.addItem("Slot " + std::to_string(i), !xt::wave::isReadOnly(tableIndex), false, [this, tableIndex, _id]
			{
				getData().setTableWave(getSelectedTable(), tableIndex, _id);
			});
		}
		return controlTableSlotsMenu;
	}

	juce::PopupMenu WaveEditor::createRamWavesPopupMenu(const std::function<void(xt::WaveId)>& _callback)
	{
		juce::PopupMenu subMenu;

		uint16_t count = 0;
		for (uint16_t i = xt::wave::g_firstRamWaveIndex; i < xt::wave::g_firstRamWaveIndex + xt::wave::g_ramWaveCount; ++i)
		{
			const auto id = xt::WaveId(i);
			subMenu.addItem(WaveTreeItem::getWaveName(id), true, false, [id, _callback]
				{
					_callback(id);
				});

				++count;
				if ((count % 25) == 0)
					subMenu.addColumnBreak();
		}

		return subMenu;
	}

	void WaveEditor::filesDropped(std::map<xt::WaveId, xt::WaveData>& _waves, std::map<xt::TableId, xt::TableData>& _tables, const juce::StringArray& _files)
	{
		_waves.clear();
		_tables.clear();

		const auto title = getEditor().getProcessor().getProperties().name + " - ";

		const auto sysex = WaveTreeItem::getSysexFromFiles(_files);

		if(sysex.empty())
		{
			genericUI::MessageBox::showOk(juce::AlertWindow::WarningIcon, title + "Error", "No Sysex data found in file");
			return;
		}

		for (const auto& s : sysex)
		{
			xt::TableData table;
			xt::WaveData wave;
			if (xt::State::parseTableData(table, s))
			{
				const auto tableId = xt::State::getTableId(s);
				_tables.emplace(tableId, table);
			}
			else if (xt::State::parseWaveData(wave, s))
			{
				const auto waveId = xt::State::getWaveId(s);
				_waves.emplace(waveId, wave);
			}
		}

		if (_tables.size() + _waves.size() > 1)
		{
			std::stringstream ss;
			ss << "The imported file contains\n\n";
			if (!_tables.empty())
			{
				ss << "Control Tables:\n";
				size_t c = 0;
				for (const auto& it : _tables)
				{
					ss << getTableName(it.first);
					if (++c < _tables.size())
						ss << ", ";
				}
				ss << "\n\n";
			}

			if (!_waves.empty())
			{
				ss << "Waves:\n";
				size_t c = 0;
				for (const auto& it : _waves)
				{
					ss << WaveTreeItem::getWaveName(it.first);
					if (++c < _waves.size())
						ss << ", ";
				}
				ss << "\n\n";
			}
			ss << "Do you want to import all of them? This will replace existing data.";
			genericUI::MessageBox::showYesNo(juce::AlertWindow::QuestionIcon, title + "Question", ss.str(), [this, t = std::move(_tables), w = std::move(_waves)](genericUI::MessageBox::Result _result)
			{
				if (_result != genericUI::MessageBox::Result::Yes)
					return;

				for (const auto& it : w)
				{
					m_data.setWave(it.first, it.second);
					m_data.sendWaveToDevice(it.first);
				}

				for (const auto& it : t)
				{
					m_data.setTable(it.first, it.second);
					m_data.sendTableToDevice(it.first);
				}
			});
		}
	}

	void WaveEditor::openGraphPopupMenu(const Graph&, const juce::MouseEvent&)
	{
		juce::PopupMenu menu;

		if (!xt::wave::isReadOnly(m_selectedWave))
		{
			menu.addItem("Save (overwrite " + WaveTreeItem::getWaveName(m_selectedWave) + ')', true, false, [this]
			{
				saveWaveTo(m_selectedWave);
			});
		}

		// open menu and let user select one of the wave slots
		const auto subMenu = createRamWavesPopupMenu([this](const xt::WaveId _id)
		{
			saveWaveTo(_id);
		});

		menu.addSubMenu("Save as...", subMenu);

		menu.showMenuAsync({});
	}
}

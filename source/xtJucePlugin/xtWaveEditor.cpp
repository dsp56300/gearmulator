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

#include "baseLib/filesystem.h"

#include "dsp56kEmu/fastmath.h"

#include "jucePluginEditorLib/pluginProcessor.h"

#include "juceUiLib/messageBox.h"

#include "synthLib/sysexToMidi.h"
#include "synthLib/wavReader.h"
#include "synthLib/wavWriter.h"

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

		constexpr auto totalCount = (xt::wave::g_ramWaveCount - xt::wave::g_firstRamWaveIndex);

		constexpr auto divide = 25;
		static_assert(totalCount / divide * divide == totalCount);

		for (uint16_t i = xt::wave::g_firstRamWaveIndex; i < xt::wave::g_firstRamWaveIndex + xt::wave::g_ramWaveCount; i += divide)
		{
			juce::PopupMenu subSubMenu;

			const auto idMin = xt::WaveId(i);
			const auto idMax = xt::WaveId(i+divide-1);

			for (uint16_t j=i; j<i+divide; ++j)
			{
				const auto id = xt::WaveId(j);
				subSubMenu.addItem(WaveTreeItem::getWaveName(id), true, false, [id, _callback]
				{
					_callback(id);
				});
			}

			subMenu.addSubMenu(WaveTreeItem::getWaveName(idMin) + " - " + WaveTreeItem::getWaveName(idMax), subSubMenu);
		}

		return subMenu;
	}

	void WaveEditor::filesDropped(std::map<xt::WaveId, xt::WaveData>& _waves, std::map<xt::TableId, xt::TableData>& _tables, const juce::StringArray& _files)
	{
		_waves.clear();
		_tables.clear();

		const auto title = getEditor().getProcessor().getProperties().name + " - ";

		auto sysex = WaveTreeItem::getSysexFromFiles(_files);

		if(sysex.empty())
		{
			genericUI::MessageBox::showOk(juce::AlertWindow::WarningIcon, title + "Error", "No Sysex data found in file");
			return;
		}

		if (sysex.size() == 1)
		{
			auto s = sysex.front();

			if (s.size() == xt::Mw1::g_allWavesAndTablesDumpLength && 
				s.front() == 0xf0 &&
				s[1] == wLib::IdWaldorf &&
				s[2] == xt::IdMw1 &&
				s[4] == xt::Mw1::g_idmAllWavesAndTables)
			{
				// contains 12 tables and 61 waves
				static constexpr uint32_t tableSize = 64 * 4;
				static constexpr uint32_t waveSize = 64 * 2;
				static_assert(xt::Mw1::g_allWavesAndTablesDumpLength == 5 + 2 + tableSize * 12 + waveSize * 61);

				int32_t off = 5;
				for (size_t t=0; t<12; ++t, off += tableSize)
				{
					std::vector<uint8_t> data = { 0xf0, wLib::IdWaldorf, xt::IdMw1, wLib::IdDeviceOmni, xt::Mw1::g_idmTable, static_cast<uint8_t>(xt::Mw1::g_firstRamTableIndex + t) };
					data.insert(data.end(), s.begin() + off, s.begin() + off + tableSize);
					data.push_back(0x00);
					data.push_back(0xf7);
					sysex.emplace_back(std::move(data));
				}
				for (size_t w=0; w<61; ++w, off += waveSize)
				{
					auto waveIndex = static_cast<uint16_t>(xt::Mw1::g_firstRamWaveIndex + w);

					std::vector<uint8_t> data = { 0xf0, wLib::IdWaldorf, xt::IdMw1, wLib::IdDeviceOmni, xt::Mw1::g_idmWave,
						static_cast<uint8_t>(waveIndex >> 12),
						static_cast<uint8_t>((waveIndex >> 8) & 0xf),
						static_cast<uint8_t>((waveIndex >> 4) & 0xf),
						static_cast<uint8_t>(waveIndex & 0xf) };
					data.insert(data.end(), s.begin() + off, s.begin() + off + waveSize);
					data.push_back(0x00);
					data.push_back(0xf7);
					sysex.emplace_back(std::move(data));
				}
				sysex.erase(sysex.begin());
			}
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
		menu.addItem("Init Square", [this]
		{
			xt::WaveData data;
			for (size_t  i = 0; i < data.size(); ++i)
				data[i] = i < data.size() / 2 ? 127 : -128;
			m_graphData.set(data);
		});
		menu.addItem("Init Triangle", [this]
		{
			xt::WaveData data;
			for (size_t  i = 0; i < data.size(); ++i)
				data[i] = static_cast<char>((i < data.size() / 2 ? i * 4 : 255 - i * 4) - 128);
			m_graphData.set(data);
		});
		menu.addItem("Init Saw", [this]
		{
			xt::WaveData data;
			for (size_t  i = 0; i < data.size(); ++i)
				data[i] = static_cast<char>(((i+1) << 1) - 129);
			m_graphData.set(data);
		});
		menu.addItem("Init Sine", [this]
		{
			xt::WaveData data;
			for (size_t i = 0; i < data.size(); ++i)
				data[i] = static_cast<char>(sin(3.1415926535f * 2.0f * static_cast<float>(i) / data.size()) * 127.0f);
			m_graphData.set(data);
		});

		menu.addItem("Clear", [this]
		{
			xt::WaveData data;
			data.fill(0);
			m_graphData.set(data);
		});

		menu.addSeparator();

		menu.addItem("Invert", [this]
		{
			xt::WaveData data = m_graphData.getSource();
			for (auto& i : data)
			{
				int d = -i;
				d = dsp56k::clamp(d, -128, 127);
				i = static_cast<char>(d);
			}
			m_graphData.set(data);
		});

		menu.addItem(juce::String::fromUTF8("Phase +180\xC2\xB0"), [this]
		{
			xt::WaveData data = m_graphData.getSource();

			for (size_t i = 0; i < data.size()>>1; ++i)
				std::swap(data[i] ,data[i+(data.size()>>1)]);
			m_graphData.set(data);
		});

		menu.addSeparator();

		menu.addItem("Export as .wav", [this]
		{
			exportAsWav(m_graphData.getSource());
		});

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

		menu.addSubMenu("Save to User Wave Slot...", subMenu);

		menu.showMenuAsync({});
	}

	void WaveEditor::exportAsSyx(const xt::WaveId& _id, const xt::WaveData& _data)
	{
		selectExportFileName("Save Wave as .syx", ".syx", [this, _id, _data](const std::string& _filename)
		{
			exportAsSyxOrMid(_filename, _id, _data, false);
		});
	}

	void WaveEditor::exportAsMid(const xt::WaveId& _id, const xt::WaveData& _data)
	{
		selectExportFileName("Save Wave as .mid", ".mid", [this, _id, _data](const std::string& _filename)
		{
			exportAsSyxOrMid(_filename, _id, _data, true);
		});
	}

	void WaveEditor::exportAsSyxOrMid(const std::string& _filename, const xt::WaveId& _id, const xt::WaveData& _data, bool _midi) const
	{
		auto sysex = xt::State::createWaveData(_data, _id.rawId(), false);

		bool success;
		if (_midi)
			success = synthLib::SysexToMidi::write(_filename.c_str(), {sysex});
		else
			success = baseLib::filesystem::writeFile(_filename, sysex);

		if (!success)
		{
			const auto productName = getEditor().getProcessor().getProperties().name;
			genericUI::MessageBox::showOk(juce::AlertWindow::WarningIcon, productName + " - Error", "Failed to create file\n" + _filename + ".\n\nMake sure that the file is not write protected or opened in another application");
		}
	}

	void WaveEditor::exportAsSyxOrMid(const std::vector<xt::WaveId>& _ids, bool _midi)
	{
		selectExportFileName(_midi ? "Save Waves as .mid" : "Save Waves as .syx", _midi ? ".mid" : ".syx", [this, _ids, _midi](const std::string& _filename)
		{
			std::vector<std::vector<uint8_t>> sysex;

			sysex.reserve(_ids.size());

			for (const auto& id : _ids)
			{
				auto wave = m_data.getWave(id);
				if (!wave)
				{
					const auto productName = getEditor().getProcessor().getProperties().name;
					genericUI::MessageBox::showOk(juce::AlertWindow::WarningIcon, productName + " - Error", "Failed to export wave " + WaveTreeItem::getWaveName(id) + ".\n\nThe wave is not available.");
					continue;
				}

				sysex.push_back(xt::State::createWaveData(*wave, id.rawId(), false));
			}

			bool success;

			if (_midi)
			{
				success = synthLib::SysexToMidi::write(_filename.c_str(), sysex);
			}
			else
			{
				std::vector<uint8_t> data;
				for (const auto& s : sysex)
					data.insert(data.end(), s.begin(), s.end());
				success = baseLib::filesystem::writeFile(_filename, data);
			}

			if (!success)
			{
				const auto productName = getEditor().getProcessor().getProperties().name;
				genericUI::MessageBox::showOk(juce::AlertWindow::WarningIcon, productName + " - Error", "Failed to create file\n" + _filename + ".\n\nMake sure that the file is not write protected or opened in another application");
			}
		});
	}

	void WaveEditor::exportAsWav(const xt::WaveData& _data)
	{
		selectExportFileName("Save Wave as .wav", ".wav", [this, _data](const std::string& _filename)
		{
			exportAsWav(_filename, _data);
		});
	}

	void WaveEditor::exportAsWav(const std::string& _filename, const xt::WaveData& _data) const
	{
		synthLib::WavWriter w;

		// 8 bit waves are unsigned
		std::array<uint8_t, std::tuple_size_v<xt::WaveData>> data;
		for (size_t i=0; i<_data.size(); ++i)
			data[i] = static_cast<uint8_t>(_data[i] + 128);

		if (!w.write(_filename, 8, false, 1, 32000, data.data(), sizeof(_data)))
		{
			const auto productName = getEditor().getProcessor().getProperties().name;
			genericUI::MessageBox::showOk(juce::AlertWindow::WarningIcon, productName + " - Error", "Failed to create file\n" + _filename + ".\n\nMake sure that the file is not write protected or opened in another application");
		}
	}

	void WaveEditor::selectImportFile(const std::function<void(const juce::String&)>& _callback)
	{
		const auto& config = m_editor.getProcessor().getConfig();

		constexpr const char* configKey = "xt_import_path";

		auto path = config.getValue(configKey, {});

		m_fileChooser = std::make_unique<juce::FileChooser>(
			"Select .syx/.mid to import",
			path,
			"*.syx,*.mid,*.midi", true);

		constexpr auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::FileChooserFlags::canSelectFiles;

		const std::function onFileChosen = [this, _callback](const juce::FileChooser& _chooser)
		{
			if (_chooser.getResults().isEmpty())
				return;

			const juce::File result = _chooser.getResult();

			m_editor.getProcessor().getConfig().setValue(configKey, result.getParentDirectory().getFullPathName());

			_callback(result.getFullPathName().toStdString());
		};

		m_fileChooser->launchAsync(flags, onFileChosen);
	}

	void WaveEditor::selectExportFileName(const std::string& _title, const std::string& _extension, const std::function<void(const std::string&)>& _callback)
	{
		const auto& config = m_editor.getProcessor().getConfig();

		constexpr const char* configKey = "xt_export_path";

		const auto path = config.getValue(configKey, {});

		m_fileChooser = std::make_unique<juce::FileChooser>(_title, path, '*' + _extension, true);

		constexpr auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::FileChooserFlags::canSelectFiles;

		auto onFileChosen = [this, _callback](const juce::FileChooser& _chooser)
		{
			if (_chooser.getResults().isEmpty())
				return;

			const auto result = _chooser.getResult();
			getEditor().getProcessor().getConfig().setValue(configKey, result.getParentDirectory().getFullPathName());

			if (!result.existsAsFile())
			{
				_callback(result.getFullPathName().toStdString());
			}
			else
			{
				genericUI::MessageBox::showYesNo(juce::MessageBoxIconType::WarningIcon, "File exists", "Do you want to overwrite the existing file?",
					[this, _callback, result](const genericUI::MessageBox::Result _result)
					{
						if (_result == genericUI::MessageBox::Result::Yes)
						{
							_callback(result.getFullPathName().toStdString());
						}
					});
			}
		};
		m_fileChooser->launchAsync(flags, onFileChosen);
	}

	std::optional<xt::WaveData> WaveEditor::importWaveFile(const std::string& _filename) const
	{
		const auto productName = getEditor().getProcessor().getProperties().name;

		std::vector<uint8_t> fileData;

		if (!baseLib::filesystem::readFile(fileData, _filename))
		{
			genericUI::MessageBox::showOk(juce::AlertWindow::WarningIcon, productName + " - Error", "Failed to open file\n" + _filename + ".\n\nMake sure that the file exists and " + productName + " has read permissions.");
			return {};
		}

		constexpr const char* formatDesc = "\n\nMake sure it is an 8 bit mono PCM file with a size of 64 (half-cycle wave) or 128 (full-cycle wave)";

		synthLib::Data wavData;
		if (!synthLib::WavReader::load(wavData, nullptr, fileData.data(), fileData.size()))
		{
			genericUI::MessageBox::showOk(juce::AlertWindow::WarningIcon, productName + " - Error", "Failed to parse data from file\n" + _filename + "." + formatDesc);
			return {};
		}

		std::vector<int8_t> data;

		if (wavData.isFloat || wavData.bitsPerSample != 8 || wavData.channels != 1)
		{
			genericUI::MessageBox::showOk(juce::AlertWindow::WarningIcon, productName + " - Error", "Unsupported wav format in file\n" + _filename + "." + formatDesc);
			return {};
		}

		data.resize(wavData.dataByteSize);
		for (size_t i=0; i<wavData.dataByteSize; ++i)
		{
			data[i] = static_cast<int8_t>(static_cast<const uint8_t*>(wavData.data)[i] - 128);
		}

		if (data.size() == 64)
		{
			xt::WaveData d;
			for (size_t i = 0; i < 64; ++i)
			{
				d[i] = data[i];
				d[127 - i] = static_cast<int8_t>(dsp56k::clamp(-data[i], -128, 127));
			}
			return d;
		}

		if (data.size() == 128)
		{
			xt::WaveData d;
			for (size_t i = 0; i < 128; ++i)
				d[i] = data[i];

			bool valid = true;

			for (size_t i=0; i<64; ++i)
			{
				if (d[i] != -d[127 - i])
				{
					valid = false;
					break;
				}
			}

			if (valid)
				return d;

			genericUI::MessageBox::showOk(juce::AlertWindow::WarningIcon, productName + " - Error", "Unsupported file\n" + _filename + ".\n\n" +
				"An imported file with a size of 128 samples needs to be a full-cycle wave, i.e. the second half of the file needs to be the first half reversed and inverted.\n" + 
				"Correct the file or import a file of length 64 only (half-cycle wave). In the latter case, building a full cycle wave is done during import."
				+ formatDesc);
			return {};
		}

		genericUI::MessageBox::showOk(juce::AlertWindow::WarningIcon, productName + " - Error", "Unsupported size of file\n" + _filename + "." + formatDesc);
		return {};
	}
}

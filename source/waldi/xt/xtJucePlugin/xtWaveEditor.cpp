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

#include "dsp56kBase/fastmath.h"

#include "jucePluginEditorLib/pluginProcessor.h"

#include "juceRmlUi/juceRmlComponent.h"

#include "juceUiLib/messageBox.h"

#include "synthLib/sysexToMidi.h"
#include "synthLib/wavReader.h"
#include "synthLib/wavWriter.h"

#include "xtLib/xtState.h"

namespace xtJucePlugin
{
	WaveEditor::WaveEditor(Editor& _editor, Rml::Element* _parent, const juce::File& _cacheDir) : m_editor(_editor), m_parent(_parent), m_data(_editor.getXtController(), _cacheDir.getFullPathName().toStdString())
	{
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

		initialize();

		auto* comp = juceRmlUi::RmlComponent::fromElement(m_parent);

		m_onUpdate.set(comp->evPostUpdate, [this](juceRmlUi::RmlComponent* const&)
		{
			checkFirstTimeVisible();
		});
	}

	WaveEditor::~WaveEditor()
	{
		destroy();
	}

	void WaveEditor::initialize()
	{
		auto& coreInstance = m_parent->GetCoreInstance();

		Rml::ElementPtr waveTree(new WaveTree(coreInstance, "tree", *this));
		Rml::ElementPtr tablesTree(new TablesTree(coreInstance, "tree", *this));
		Rml::ElementPtr controlTree(new ControlTree(coreInstance, "tree", *this));

		auto makeFullSize = [](Rml::Element* e)
		{
			e->SetProperty(Rml::PropertyId::Position, Rml::Style::Position::Absolute);
			e->SetProperty(Rml::PropertyId::Left, Rml::Property(0, Rml::Unit::PX));
			e->SetProperty(Rml::PropertyId::Top, Rml::Property(0, Rml::Unit::PX));
			e->SetProperty(Rml::PropertyId::Width, Rml::Property(100, Rml::Unit::PERCENT));
			e->SetProperty(Rml::PropertyId::Height, Rml::Property(100, Rml::Unit::PERCENT));
		};

		makeFullSize(waveTree.get());
		makeFullSize(tablesTree.get());
		makeFullSize(controlTree.get());

		auto* waveListParent = m_editor.findChild("wecWaveList");
		auto* tablesListParent = m_editor.findChild("wecWavetableList");
		auto* controlListParent = m_editor.findChild("wecWaveControlTable");

		m_waveTree = dynamic_cast<WaveTree*>(waveListParent->AppendChild(std::move(waveTree)));
		m_tablesTree = dynamic_cast<TablesTree*>(tablesListParent->AppendChild(std::move(tablesTree)));
		m_controlTree = dynamic_cast<ControlTree*>(controlListParent->AppendChild(std::move(controlTree)));

		auto* waveFreqParent = m_editor.findChild("wecWaveFreq");
		auto* wavePhaseParent = m_editor.findChild("wecWavePhase");
		auto* waveTimeParent = m_editor.findChild("wecWaveTime");

		m_graphFreq.reset(new GraphFreq(*this, waveFreqParent));
		m_graphPhase.reset(new GraphPhase(*this, wavePhaseParent));
		m_graphTime.reset(new GraphTime(*this, waveTimeParent));

		m_tablesTree->setSelectedEntryFromCurrentPreset();
	}

	void WaveEditor::destroy()
	{
		if (m_waveTree)
		{
			delete juceRmlUi::helper::removeFromParent(m_waveTree).release();
			delete juceRmlUi::helper::removeFromParent(m_tablesTree).release();
			delete juceRmlUi::helper::removeFromParent(m_controlTree).release();
		}
		m_graphFreq.reset();
		m_graphPhase.reset();
		m_graphTime.reset();
	}

	void WaveEditor::checkFirstTimeVisible()
	{
		if(m_parent->IsVisible(true) && !m_wasVisible)
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

	void WaveEditor::onReceiveWave(const pluginLib::MidiPacket::Data& _data, const synthLib::SysexBuffer& _msg)
	{
		m_data.onReceiveWave(_msg);
	}

	void WaveEditor::onReceiveTable(const pluginLib::MidiPacket::Data& _data, const synthLib::SysexBuffer& _msg)
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

	juceRmlUi::Menu WaveEditor::createCopyToSelectedTableMenu(xt::WaveId _id)
	{
		juceRmlUi::Menu controlTableSlotsMenu;
		for(uint16_t i=0; i<xt::wave::g_wavesPerTable; ++i)
		{
			const auto tableIndex = xt::TableIndex(i);

			controlTableSlotsMenu.addEntry("Slot " + std::to_string(i), !xt::wave::isReadOnly(tableIndex), false, [this, tableIndex, _id]
			{
				getData().setTableWave(getSelectedTable(), tableIndex, _id);
			});
		}

		controlTableSlotsMenu.setItemsPerColumn(16);
		return controlTableSlotsMenu;
	}

	juceRmlUi::Menu WaveEditor::createRamWavesPopupMenu(const std::function<void(xt::WaveId)>& _callback)
	{
		juceRmlUi::Menu subMenu;

		constexpr auto totalCount = (xt::wave::g_ramWaveCount - xt::wave::g_firstRamWaveIndex);

		constexpr auto divide = 25;
		static_assert(totalCount / divide * divide == totalCount);

		for (uint16_t i = xt::wave::g_firstRamWaveIndex; i < xt::wave::g_firstRamWaveIndex + xt::wave::g_ramWaveCount; i += divide)
		{
			juceRmlUi::Menu subSubMenu;

			const auto idMin = xt::WaveId(i);
			const auto idMax = xt::WaveId(i+divide-1);

			for (uint16_t j=i; j<i+divide; ++j)
			{
				const auto id = xt::WaveId(j);
				subSubMenu.addEntry(WaveTreeItem::getWaveName(id), true, false, [id, _callback]
				{
					_callback(id);
				});
			}

			subMenu.addSubMenu(WaveTreeItem::getWaveName(idMin) + " - " + WaveTreeItem::getWaveName(idMax), std::move(subSubMenu));
		}

		return subMenu;
	}

	void WaveEditor::filesDropped(std::map<xt::WaveId, xt::WaveData>& _waves, std::map<xt::TableId, xt::TableData>& _tables, const std::vector<std::string>& _files)
	{
		_waves.clear();
		_tables.clear();

		const auto title = getEditor().getProcessor().getProperties().name + " - ";

		auto sysex = WaveTreeItem::getSysexFromFiles(_files);

		if(sysex.empty())
		{
			genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning, title + "Error", "No Sysex data found in file");
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
					synthLib::SysexBuffer data = { 0xf0, wLib::IdWaldorf, xt::IdMw1, wLib::IdDeviceOmni, xt::Mw1::g_idmTable, static_cast<uint8_t>(xt::Mw1::g_firstRamTableIndex + t) };
					data.insert(data.end(), s.begin() + off, s.begin() + off + tableSize);
					data.push_back(0x00);
					data.push_back(0xf7);
					sysex.emplace_back(std::move(data));
				}
				for (size_t w=0; w<61; ++w, off += waveSize)
				{
					auto waveIndex = static_cast<uint16_t>(xt::Mw1::g_firstRamWaveIndex + w);

					synthLib::SysexBuffer data = { 0xf0, wLib::IdWaldorf, xt::IdMw1, wLib::IdDeviceOmni, xt::Mw1::g_idmWave,
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

		if (_tables.size() == 1 && _waves.size() > 1)
		{
			auto& table = _tables.begin()->second;

			if (xt::State::isSpeech(table))
			{
				// if we import speech, retarget all waves to be put into out special rom slots as speech requires continuous memory

				const auto oldStart = table[2].rawId();
				constexpr auto newStart = xt::wave::g_firstRwRomWaveIndex;

				auto waves = xt::State::getWavesForTable(_tables.begin()->second);

				for (auto wave : waves)
				{
					auto it = _waves.find(wave);
					if (it == _waves.end())
						continue;

					const auto offset = it->first.rawId() - oldStart;
					const auto newId = xt::WaveId(static_cast<uint16_t>(newStart + offset));

					if (_waves.find(newId) != _waves.end())
						continue;

					auto data = it->second;
					_waves.erase(it);
					_waves.emplace(newId, data);
				}

				// table needs to point to new location
				table[2] = xt::WaveId(newStart);
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
			genericUI::MessageBox::showYesNo(genericUI::MessageBox::Icon::Question, title + "Question", ss.str(), [this, t = std::move(_tables), w = std::move(_waves)](genericUI::MessageBox::Result _result)
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

	void WaveEditor::openGraphPopupMenu(const Graph&, Rml::Event& _event)
	{
		_event.StopPropagation();

		juceRmlUi::Menu menu;
		menu.addEntry("Init Square", [this]
		{
			xt::WaveData data;
			for (size_t  i = 0; i < data.size(); ++i)
				data[i] = i < data.size() / 2 ? 127 : -128;
			m_graphData.set(data);
		});
		menu.addEntry("Init Triangle", [this]
		{
			xt::WaveData data;
			for (size_t  i = 0; i < data.size(); ++i)
				data[i] = static_cast<char>((i < data.size() / 2 ? i * 4 : 255 - i * 4) - 128);
			m_graphData.set(data);
		});
		menu.addEntry("Init Saw", [this]
		{
			xt::WaveData data;
			for (size_t  i = 0; i < data.size(); ++i)
				data[i] = static_cast<char>(((i+1) << 1) - 129);
			m_graphData.set(data);
		});
		menu.addEntry("Init Sine", [this]
		{
			xt::WaveData data;
			for (size_t i = 0; i < data.size(); ++i)
				data[i] = static_cast<char>(sin(3.1415926535f * 2.0f * static_cast<float>(i) / data.size()) * 127.0f);
			m_graphData.set(data);
		});

		menu.addEntry("Clear", [this]
		{
			xt::WaveData data;
			data.fill(0);
			m_graphData.set(data);
		});

		menu.addSeparator();

		menu.addEntry("Invert", [this]
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

		menu.addEntry(juce::String::fromUTF8("Phase +180\xC2\xB0").toStdString(), [this]
		{
			xt::WaveData data = m_graphData.getSource();

			for (size_t i = 0; i < data.size()>>1; ++i)
				std::swap(data[i] ,data[i+(data.size()>>1)]);
			m_graphData.set(data);
		});

		menu.addSeparator();

		menu.addEntry("Export as .wav", [this]
		{
			exportAsWav(m_graphData.getSource());
		});

		if (!xt::wave::isReadOnly(m_selectedWave))
		{
			menu.addEntry("Save (overwrite " + WaveTreeItem::getWaveName(m_selectedWave) + ')', true, false, [this]
			{
				saveWaveTo(m_selectedWave);
			});
		}

		// open menu and let user select one of the wave slots
		auto subMenu = createRamWavesPopupMenu([this](const xt::WaveId _id)
		{
			saveWaveTo(_id);
		});

		menu.addSubMenu("Save to User Wave Slot...", std::move(subMenu));

		menu.runModal(_event);
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
		exportToFile(_filename, sysex, _midi);
	}

	void WaveEditor::exportAsSyxOrMid(const std::vector<xt::WaveId>& _ids, bool _midi)
	{
		selectExportFileName(_midi ? "Save Waves as .mid" : "Save Waves as .syx", _midi ? ".mid" : ".syx", [this, _ids, _midi](const std::string& _filename)
		{
			synthLib::SysexBufferList sysex;

			sysex.reserve(_ids.size());

			for (const auto& id : _ids)
			{
				auto wave = m_data.getWave(id);
				if (!wave)
				{
					const auto productName = getEditor().getProcessor().getProperties().name;
					genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning, productName + " - Error", "Failed to export wave " + WaveTreeItem::getWaveName(id) + ".\n\nThe wave is not available.");
					continue;
				}

				sysex.push_back(xt::State::createWaveData(*wave, id.rawId(), false));
			}

			exportToFile(_filename, sysex, _midi);
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
		return exportAsWav(_filename, std::vector(_data.begin(), _data.end()));
	}

	void WaveEditor::exportAsSyxOrMid(const xt::TableId& _table, bool _midi)
	{
		auto table = m_data.getTable(_table);
		if (!table)
			return;

		selectExportFileName(_midi ? "Save Table as .mid" : "Save Table as .syx", _midi ? ".mid" : ".syx", [this, _table, table, _midi](const std::string& _filename)
		{
			const auto sysex = xt::State::createTableData(*table, _table.rawId(), false);
			exportToFile(_filename, sysex, _midi);
		});
	}

	void WaveEditor::exportAsWav(const xt::TableId& _table)
	{
		selectExportFileName("Save Table as .wav", ".wav", [this, _table](const std::string& _filename)
		{
			auto t = m_data.getTable(_table);
			if (!t)
				return;
			const auto& table = *t;

			std::vector<int8_t> data;

			bool first = true;

			for (uint16_t a=0; a<table.size(); ++a)
			{
				const auto waveIdA = table[a];

				auto waveA = m_data.getWave(waveIdA);

				if (!waveA)
					continue;

				for (uint16_t b=a+1; b<table.size(); ++b)
				{
					const auto waveIdB = table[b];

					auto waveB = m_data.getWave(waveIdB);

					if (!waveB)
						continue;

					if (first)
					{
						data.insert(data.end(), waveA->begin(), waveA->end());
						first = false;
					}

					for (uint16_t c=a+1; c<b; ++c)
					{
						auto d = xt::State::createinterpolatedTable(*waveA, *waveB, a, b, c);
						data.insert(data.end(), d.begin(), d.end());
					}

					data.insert(data.end(), waveB->begin(), waveB->end());

					a = b - 1;
					break;
				}
			}

			exportAsWav(_filename, data);
		});
	}

	void WaveEditor::exportAsWav(const std::string& _filename, const std::vector<int8_t>& _data) const
	{
		synthLib::WavWriter w;

		// 8 bit waves are unsigned
		std::vector<uint8_t> data;
		data.reserve(_data.size());
		for (size_t i=0; i<_data.size(); ++i)
			data.push_back(static_cast<uint8_t>(_data[i] + 128));

		if (!w.write(_filename, 8, false, 1, 32000, data.data(), _data.size()))
		{
			const auto productName = getEditor().getProcessor().getProperties().name;
			genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning, productName + " - Error", "Failed to create file\n" + _filename + ".\n\nMake sure that the file is not write protected or opened in another application");
		}
	}

	void WaveEditor::exportToFile(const std::string& _filename, const synthLib::SysexBufferList& _sysex, const bool _midi) const
	{
		bool success;

		if (_midi)
		{
			success = synthLib::SysexToMidi::write(_filename.c_str(), _sysex);
		}
		else
		{
			if (_sysex.size() > 1)
			{
				synthLib::SysexBuffer sysex;
				for (const auto& s : _sysex)
					sysex.insert(sysex.end(), s.begin(), s.end());
				success = baseLib::filesystem::writeFile(_filename, sysex);
			}
			else
			{
				success = baseLib::filesystem::writeFile(_filename, _sysex.front());
			}
		}

		if (success)
			return;

		const auto productName = getEditor().getProcessor().getProperties().name;
		genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning, productName + " - Error", "Failed to create file\n" + _filename + ".\n\nMake sure that the file is not write protected or opened in another application");
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

		const std::function onFileChosen = [this, _callback, configKey](const juce::FileChooser& _chooser)
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

		auto onFileChosen = [this, _callback, configKey](const juce::FileChooser& _chooser)
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
				genericUI::MessageBox::showYesNo(genericUI::MessageBox::Icon::Warning, "File exists", "Do you want to overwrite the existing file?",
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
			genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning, productName + " - Error", "Failed to open file\n" + _filename + ".\n\nMake sure that the file exists and " + productName + " has read permissions.");
			return {};
		}

		constexpr const char* formatDesc = "\n\nMake sure it is an 8 bit mono PCM file with a size of 64 (half-cycle wave) or 128 (full-cycle wave)";

		synthLib::Data wavData;
		if (!synthLib::WavReader::load(wavData, nullptr, fileData.data(), fileData.size()))
		{
			genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning, productName + " - Error", "Failed to parse data from file\n" + _filename + "." + formatDesc);
			return {};
		}

		std::vector<int8_t> data;

		if (wavData.isFloat || wavData.bitsPerSample != 8 || wavData.channels != 1)
		{
			genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning, productName + " - Error", "Unsupported wav format in file\n" + _filename + "." + formatDesc);
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

			genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning, productName + " - Error", "Unsupported file\n" + _filename + ".\n\n" +
				"An imported file with a size of 128 samples needs to be a full-cycle wave, i.e. the second half of the file needs to be the first half reversed and inverted.\n" + 
				"Correct the file or import a file of length 64 only (half-cycle wave). In the latter case, building a full cycle wave is done during import."
				+ formatDesc);
			return {};
		}

		genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning, productName + " - Error", "Unsupported size of file\n" + _filename + "." + formatDesc);
		return {};
	}
}

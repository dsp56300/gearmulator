#include "PatchBrowser.h"

#include "VirusEditor.h"
#include "../PluginProcessor.h"

#include "../../virusLib/microcontrollerTypes.h"
#include "../../virusLib/microcontroller.h"

#include "../VirusController.h"
#include "juce_cryptography/hashing/juce_MD5.h"

#include "../../synthLib/midiToSysex.h"

using namespace juce;

const juce::Array<juce::String> ModelList = {"A","B","C","TI"};

namespace genericVirusUI
{
	virusLib::PresetVersion guessVersion(const uint8_t v)
	{
		return virusLib::Microcontroller::getPresetVersion(v);
	}

	static PatchBrowser* s_lastPatchBrowser = nullptr;

	PatchBrowser::PatchBrowser(const VirusEditor& _editor) : m_editor(_editor), m_controller(_editor.getController()),
		m_fileFilter("*.syx;*.mid;*.midi", "*", "Virus Patch Dumps"),
		m_bankList(FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, File::getSpecialLocation(File::SpecialLocationType::currentApplicationFile), &m_fileFilter, nullptr),
		m_search("Search Box"),
		m_patchList("Patch Browser"),
		m_properties(_editor.getProcessor().getConfig())
	{
		const auto bankDir = m_properties.getValue("virus_bank_dir", "");

		if (bankDir.isNotEmpty() && File(bankDir).isDirectory())
		{
			m_bankList.setRoot(bankDir);

			s_lastPatchBrowser = this;
			auto callbackPathBrowser = this;

			juce::Timer::callAfterDelay(1000, [&, bankDir, callbackPathBrowser]()
			{
				if(s_lastPatchBrowser != callbackPathBrowser)
					return;

				const auto lastFile = m_properties.getValue("virus_selected_file", "");
				const auto child = File(bankDir).getChildFile(lastFile);
				if(child.existsAsFile())
				{
					m_bankList.setFileName(child.getFileName());
					m_sendOnSelect = false;
					onFileSelected(child);
					m_sendOnSelect = true;
				}
			});
		}

		m_patchList.getHeader().addColumn("#", Columns::INDEX, 32);
		m_patchList.getHeader().addColumn("Name", Columns::NAME, 130);
		m_patchList.getHeader().addColumn("Category1", Columns::CAT1, 84);
		m_patchList.getHeader().addColumn("Category2", Columns::CAT2, 84);
		m_patchList.getHeader().addColumn("Arp", Columns::ARP, 32);
		m_patchList.getHeader().addColumn("Uni", Columns::UNI, 32);
		m_patchList.getHeader().addColumn("ST+-", Columns::ST, 32);
		m_patchList.getHeader().addColumn("Ver", Columns::VER, 32);

		fitInParent(m_bankList, "ContainerFileSelector");
		fitInParent(m_patchList, "ContainerPatchList");

		m_search.setColour(TextEditor::textColourId, Colours::white);
		m_search.onTextChange = [this]
		{
			refreshPatchList();
		};
		m_search.setTextToShowWhenEmpty("Search...", Colours::grey);

		fitInParent(m_search, "ContainerPatchListSearchBox");

		m_bankList.addListener(this);
		m_patchList.setModel(this);

		m_romBankSelect = _editor.findComponentT<juce::ComboBox>("RomBankSelect", false);

		if(m_romBankSelect)
		{
			int id=1;

			m_romBankSelect->addItem("-", 1);

			for(uint32_t i=0; i<m_controller.getBankCount(); ++i)
			{
				m_romBankSelect->addItem(m_controller.getBankName(i), ++id);
			}

			m_romBankSelect->onChange = [this]
			{
				const auto index = m_romBankSelect->getSelectedItemIndex();
				if(index > 0)
					loadRomBank(index - 1);
			};
		}
	}

	PatchBrowser::~PatchBrowser()
	{
		if(s_lastPatchBrowser == this)
			s_lastPatchBrowser = nullptr;
	}

	void PatchBrowser::fitInParent(juce::Component& _component, const std::string& _parentName) const
	{
		auto* parent = m_editor.findComponent(_parentName);

		_component.setTransform(juce::AffineTransform::scale(2.0f));

		const auto& bounds = parent->getBounds();
		const auto w = bounds.getWidth() >> 1;
		const auto h = bounds.getHeight() >> 1;

		_component.setBounds(0,0, w,h);

		parent->addAndMakeVisible(_component);
	}

	void PatchBrowser::selectionChanged() {}

	uint32_t PatchBrowser::load(const Virus::Controller& _controller, std::vector<Patch>& _result, std::set<std::string>* _dedupeChecksums, const std::vector<std::vector<uint8_t>>& _packets)
	{
		uint32_t count = 0;
		for (const auto& packet : _packets)
		{
			if (load(_controller, _result, _dedupeChecksums, packet))
				++count;
		}
		return count;
	}

	bool PatchBrowser::load(const Virus::Controller& _controller, std::vector<Patch>& _result, std::set<std::string>* _dedupeChecksums, const std::vector<uint8_t>& _data)
	{
		if (_data.size() < 267)
			return false;

		Patch patch;
		patch.sysex = _data;
		if(!initializePatch(_controller, patch))
			return false;

		patch.progNumber = static_cast<int>(_result.size());

		if (!_dedupeChecksums)
		{
			_result.push_back(patch);
		}
		else
		{
			const auto md5 = std::string(MD5(&_data.front() + 9 + 17, 256 - 17 - 3).toHexString().toRawUTF8());

			if (_dedupeChecksums->find(md5) == _dedupeChecksums->end())
			{
				_dedupeChecksums->insert(md5);
				_result.push_back(patch);
			}
		}

		return true;
	}

	uint32_t PatchBrowser::loadBankFile(const Virus::Controller& _controller, std::vector<Patch>& _result, std::set<std::string>* _dedupeChecksums, const File& file)
	{
		const auto ext = file.getFileExtension().toLowerCase();
		const auto path = file.getParentDirectory().getFullPathName();

		if (ext == ".syx")
		{
			MemoryBlock data;

			if (!file.loadFileAsData(data))
				return 0;

			std::vector<uint8_t> d;
			d.resize(data.getSize());
			memcpy(&d[0], data.getData(), data.getSize());

			std::vector<std::vector<uint8_t>> packets;
			synthLib::MidiToSysex::splitMultipleSysex(packets, d);

			return load(_controller, _result, _dedupeChecksums, packets);
		}

		if (ext == ".mid" || ext == ".midi")
		{
			std::vector<uint8_t> data;

			synthLib::MidiToSysex::readFile(data, file.getFullPathName().getCharPointer());

			if (data.empty())
				return 0;

			std::vector<std::vector<uint8_t>> packets;
			synthLib::MidiToSysex::splitMultipleSysex(packets, data);

			return load(_controller, _result, _dedupeChecksums, packets);
		}

		return 0;
	}

	bool PatchBrowser::selectPrevPreset()
	{
		return selectPrevNextPreset(-1);
	}

	bool PatchBrowser::selectNextPreset()
	{
		return selectPrevNextPreset(1);
	}

	void PatchBrowser::fileClicked(const File& file, const MouseEvent& e)
	{
		const auto ext = file.getFileExtension().toLowerCase();
		const auto path = file.getParentDirectory().getFullPathName();
		if (file.isDirectory() && e.mods.isPopupMenu())
		{
			auto p = PopupMenu();
			p.addItem("Add directory contents to patch list", [this, file]()
				{
					m_patches.clear();
					m_checksums.clear();
					std::set<std::string> dedupeChecksums;

					std::vector<Patch> patches;

					for (const auto& f : RangedDirectoryIterator(file, false, "*.syx;*.mid;*.midi", File::findFiles))
						loadBankFile(m_controller, patches, &dedupeChecksums, f.getFile());

					fillPatchList(patches);
				});
			p.showMenuAsync(PopupMenu::Options());

			return;
		}
		m_properties.setValue("virus_bank_dir", path);
		onFileSelected(file);
	}

	int PatchBrowser::getNumRows() { return m_filteredPatches.size(); }

	void PatchBrowser::paintRowBackground(Graphics& g, int rowNumber, int width, int height, bool rowIsSelected)
	{
		const auto alternateColour = m_patchList.getLookAndFeel()
			.findColour(ListBox::backgroundColourId)
			.interpolatedWith(m_patchList.getLookAndFeel().findColour(ListBox::textColourId), 0.03f);
		if (rowIsSelected)
			g.fillAll(Colours::lightblue);
		else if (rowNumber & 1)
			g.fillAll(alternateColour);
	}

	void PatchBrowser::paintCell(Graphics& g, int rowNumber, int columnId, int width, int height, bool rowIsSelected) {
		g.setColour(rowIsSelected ? Colours::darkblue
			: m_patchList.getLookAndFeel().findColour(ListBox::textColourId)); // [5]

		if (rowNumber >= getNumRows())
			return;	// Juce what are you up to?

		const auto rowElement = m_filteredPatches[rowNumber];
		//auto text = rowElement.name;
		String text = "";
		if (columnId == Columns::INDEX)
			text = String(rowElement.progNumber);
		else if (columnId == Columns::NAME)
			text = rowElement.name;
		else if (columnId == Columns::CAT1)
			text = rowElement.category1;
		else if (columnId == Columns::CAT2)
			text = rowElement.category2;
		else if (columnId == Columns::ARP)
			text = rowElement.arpMode != 0 ? "Y" : " ";
		else if (columnId == Columns::UNI)
			text = rowElement.unison == 0 ? " " : String(rowElement.unison + 1);
		else if (columnId == Columns::ST)
			text = rowElement.transpose != 64 ? String(rowElement.transpose - 64) : " ";
		else if (columnId == Columns::VER)
		{
			switch (rowElement.model)
			{
			case virusLib::A:	text = "A";	break;
			case virusLib::B:	text = "B";	break;
			case virusLib::C:	text = "C";	break;
			case virusLib::D:	text = "TI";	break;
			case virusLib::D2:  text = "TI2";	break;
			default:			text = "?";	break;
			}
		}
		g.drawText(text, 2, 0, width - 4, height, Justification::centredLeft, true); // [6]
		g.setColour(m_patchList.getLookAndFeel().findColour(ListBox::backgroundColourId));
		g.fillRect(width - 1, 0, 1, height); // [7]
	}

	void PatchBrowser::selectedRowsChanged(int lastRowSelected)
	{
		if(!m_sendOnSelect)
			return;

		const auto idx = m_patchList.getSelectedRow();

		if (idx == -1)
			return;

		// re-pack single, force to edit buffer
		const auto program = m_controller.isMultiMode() ? m_controller.getCurrentPart() : static_cast<uint8_t>(virusLib::ProgramType::SINGLE);

		const auto& patch = m_filteredPatches[idx];

		const auto msg = m_controller.modifySingleDump(patch.sysex, virusLib::BankNumber::EditBuffer, program, true, true);

		if(msg.empty())
			return;

		m_controller.sendSysEx(msg);
		m_controller.requestSingle(0x0, program);

		m_controller.setCurrentPartPresetSource(m_controller.getCurrentPart(), Virus::Controller::PresetSource::Browser);

		m_properties.setValue("virus_selected_patch", patch.name);
	}

	void PatchBrowser::cellDoubleClicked(int rowNumber, int columnId, const MouseEvent&)
	{
		if (rowNumber == m_patchList.getSelectedRow())
			selectedRowsChanged(0);
	}

	class PatchBrowser::PatchBrowserSorter
	{
	public:
		PatchBrowserSorter(const int attributeToSortBy, const bool forwards)
			: m_attributeToSort(attributeToSortBy),
			m_direction(forwards ? 1 : -1)
		{}

		int compareElements(const Patch& first, const Patch& second) const
		{
			if (m_attributeToSort == Columns::INDEX)
				return m_direction * (first.progNumber - second.progNumber);
			if (m_attributeToSort == Columns::NAME)
				return m_direction * first.name.compareIgnoreCase(second.name);
			if (m_attributeToSort == Columns::CAT1)
				return m_direction * first.category1.compare(second.category1);
			if (m_attributeToSort == Columns::CAT2)
				return m_direction * first.category2.compare(second.category2);
			if (m_attributeToSort == Columns::ARP)
				return m_direction * (first.arpMode - second.arpMode);
			if (m_attributeToSort == Columns::UNI)
				return m_direction * (first.unison - second.unison);
			if (m_attributeToSort == Columns::VER)
				return m_direction * (first.model - second.model);
			if (m_attributeToSort == Columns::ST)
				return m_direction * (first.transpose - second.transpose);
			return m_direction * (first.progNumber - second.progNumber);
		}

	private:
		const int m_attributeToSort;
		const int m_direction;
	};

	void PatchBrowser::sortOrderChanged(int newSortColumnId, bool isForwards)
	{
		if (newSortColumnId != 0)
		{
			PatchBrowserSorter sorter(newSortColumnId, isForwards);
			m_filteredPatches.sort(sorter);
			m_patchList.updateContent();
		}
	}

	void PatchBrowser::loadRomBank(const uint32_t _bankIndex)
	{
		const auto& singles = m_controller.getSinglePresets();

		if(_bankIndex >= singles.size())
			return;

		const auto& bank = singles[_bankIndex];

		const auto searchValue = m_search.getText();

		std::vector<Patch> patches;

		for(size_t s=0; s<bank.size(); ++s)
		{
			Patch patch;

			patch.sysex = bank[s].data;

			if(!initializePatch(m_controller, patch))
				continue;

			patch.progNumber = static_cast<int>(s);

			patches.push_back(patch);
		}

		fillPatchList(patches);
	}

	void PatchBrowser::onFileSelected(const juce::File& file)
	{
		const auto ext = file.getFileExtension().toLowerCase();
		if (file.existsAsFile() && ext == ".syx" || ext == ".midi" || ext == ".mid")
		{
			m_properties.setValue("virus_selected_file", file.getFileName());

			std::vector<Patch> patches;
			loadBankFile(m_controller, patches, nullptr, file);

			fillPatchList(patches);
		}

		if(m_romBankSelect)
			m_romBankSelect->setSelectedItemIndex(0);
	}

	void PatchBrowser::fillPatchList(const std::vector<Patch>& _patches)
	{
		m_patches.clear();

		for (const auto& patch : _patches)
			m_patches.add(patch);

		refreshPatchList();
	}

	void PatchBrowser::refreshPatchList()
	{
		const auto searchValue = m_search.getText();
		const auto selectedPatchName = m_properties.getValue("virus_selected_patch", "");

		m_filteredPatches.clear();
		int i=0;
		int selectIndex = -1;

		for (const auto& patch : m_patches)
		{
			if (searchValue.isEmpty() || patch.name.containsIgnoreCase(searchValue))
			{
				m_filteredPatches.add(patch);

				if(patch.name == selectedPatchName)
					selectIndex = i;

				++i;
			}
		}
		m_patchList.updateContent();
		m_patchList.deselectAllRows();
		m_patchList.repaint();

		if(selectIndex != -1)
			m_patchList.selectRow(selectIndex);
	}

	bool PatchBrowser::selectPrevNextPreset(int _dir)
	{
		const auto part = m_controller.getCurrentPart();

		if(m_controller.getCurrentPartPresetSource(part) == Virus::Controller::PresetSource::Rom)
			return false;

		if(m_filteredPatches.isEmpty())
			return false;

		const auto idx = m_patchList.getSelectedRow();
		if(idx < 0)
			return false;

		const auto name = m_controller.getCurrentPartPresetName(part);

		if(m_filteredPatches[idx].name != name)
			return false;

		const auto newIdx = idx + _dir;

		if(newIdx < 0 || newIdx >= m_filteredPatches.size())
			return false;

		m_patchList.selectRow(newIdx);
		return true;
	}

	bool PatchBrowser::initializePatch(const Virus::Controller& _controller, Patch& _patch)
	{
		const auto& c = _controller;

		pluginLib::MidiPacket::Data data;
		pluginLib::MidiPacket::ParamValues parameterValues;

		if(!c.parseSingle(data, parameterValues, _patch.sysex))
			return false;

		const auto idxVersion = c.getParameterIndexByName("Version");
		const auto idxCategory1 = c.getParameterIndexByName("Category1");
		const auto idxCategory2 = c.getParameterIndexByName("Category2");
		const auto idxUnison = c.getParameterIndexByName("Unison Mode");
		const auto idxTranspose = c.getParameterIndexByName("Transpose");
		const auto idxArpMode = c.getParameterIndexByName("Arp Mode");

		_patch.name = c.getSinglePresetName(parameterValues);
		_patch.model = guessVersion(parameterValues.find(std::make_pair(pluginLib::MidiPacket::AnyPart, idxVersion))->second);
		_patch.unison = parameterValues.find(std::make_pair(pluginLib::MidiPacket::AnyPart, idxUnison))->second;
		_patch.transpose = parameterValues.find(std::make_pair(pluginLib::MidiPacket::AnyPart, idxTranspose))->second;
		_patch.arpMode = parameterValues.find(std::make_pair(pluginLib::MidiPacket::AnyPart, idxArpMode))->second;

		const auto category1 = parameterValues.find(std::make_pair(pluginLib::MidiPacket::AnyPart, idxCategory1))->second;
		const auto category2 = parameterValues.find(std::make_pair(pluginLib::MidiPacket::AnyPart, idxCategory2))->second;

		const auto* paramCategory1 = c.getParameter(idxCategory1, 0);
		const auto* paramCategory2 = c.getParameter(idxCategory2, 0);
		
		_patch.category1 = paramCategory1->getDescription().valueList.valueToText(category1);
		_patch.category2 = paramCategory2->getDescription().valueList.valueToText(category2);

		return true;
	}

}

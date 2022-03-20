#include "PatchBrowser.h"

#include "VirusEditor.h"

#include "../../virusLib/microcontrollerTypes.h"

#include "../VirusController.h"
#include "juce_cryptography/hashing/juce_MD5.h"

#include "../../synthLib/midiToSysex.h"

using namespace juce;

const juce::Array<juce::String> ModelList = {"A","B","C","TI"};

const Array<String> g_categories = { "", "Lead",	 "Bass",	  "Pad",	   "Decay",	   "Pluck",
							 "Acid", "Classic", "Arpeggiator", "Effects",	"Drums",	"Percussion",
							 "Input", "Vocoder", "Favourite 1", "Favourite 2", "Favourite 3" };

namespace genericVirusUI
{
	virusLib::VirusModel guessVersion(const uint8_t* _data)
	{
		const auto v = _data[0];

		if (v < 5)
			return virusLib::A;
		if (v == 6)
			return virusLib::B;
		if (v == 7)
			return virusLib::C;
		return virusLib::TI;
	}

	static juce::String parseAsciiText(const std::vector<uint8_t>& msg, const int start)
	{
	    char text[Virus::Controller::kNameLength + 1];
	    text[Virus::Controller::kNameLength] = 0; // termination
	    for (int pos = 0; pos < Virus::Controller::kNameLength; ++pos)
	        text[pos] = static_cast<char>(msg[start + pos]);
	    return {text};
	}

	static void initializePatch(Patch& _patch)
	{
		_patch.name = parseAsciiText(_patch.data, 128 + 112);
		_patch.category1 = _patch.data[251];
		_patch.category2 = _patch.data[252];
		_patch.unison = _patch.data[97];
		_patch.transpose = _patch.data[93];
		_patch.model = guessVersion(&_patch.data[0]);
	}

	PatchBrowser::PatchBrowser(const VirusEditor& _editor) : m_editor(_editor), m_controller(_editor.getController()),
		m_fileFilter("*.syx;*.mid;*.midi", "*", "Virus Patch Dumps"),
		m_bankList(FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, File::getSpecialLocation(File::SpecialLocationType::currentApplicationFile), &m_fileFilter, nullptr),
		m_search("Search Box"),
		m_patchList("Patch Browser"),
		m_properties(m_controller.getConfig())
	{
		const auto bankDir = m_properties->getValue("virus_bank_dir", "");

		if (bankDir.isNotEmpty() && File(bankDir).isDirectory())
			m_bankList.setRoot(bankDir);

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
			m_filteredPatches.clear();
			for (const auto& patch : m_patches)
			{
				const auto searchValue = m_search.getText();
				if (searchValue.isEmpty() || patch.name.containsIgnoreCase(searchValue))
					m_filteredPatches.add(patch);
			}
			m_patchList.updateContent();
			m_patchList.deselectAllRows();
			m_patchList.repaint();
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

			for(uint32_t i=0; i<m_editor.getController().getBankCount(); ++i)
			{
				std::stringstream ss;
				ss << "Bank " << static_cast<char>('A' + i);
				m_romBankSelect->addItem(ss.str(), ++id);
			}

			m_romBankSelect->onChange = [this]
			{
				const auto index = m_romBankSelect->getSelectedItemIndex();
				if(index > 0)
					loadRomBank(index - 1);
			};
		}
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

	uint32_t PatchBrowser::load(std::vector<Patch>& _result, std::set<std::string>* _dedupeChecksums, const std::vector<std::vector<uint8_t>>& _packets)
	{
		uint32_t count = 0;
		for (const auto& packet : _packets)
		{
			if (load(_result, _dedupeChecksums, packet))
				++count;
		}
		return count;
	}

	bool PatchBrowser::load(std::vector<Patch>& _result, std::set<std::string>* _dedupeChecksums, const std::vector<uint8_t>& _data)
	{
		if (_data.size() < 267)
			return false;

		auto* it = &_data.front();

		if (*it == (uint8_t)0xf0
			&& *(it + 1) == (uint8_t)0x00
			&& *(it + 2) == (uint8_t)0x20
			&& *(it + 3) == (uint8_t)0x33
			&& *(it + 4) == (uint8_t)0x01
			&& *(it + 6) == (uint8_t)virusLib::DUMP_SINGLE)
		{
			Patch patch;
			patch.progNumber = static_cast<int>(_result.size());
			patch.sysex = _data;
			patch.data.insert(patch.data.begin(), _data.begin() + 9, _data.end());
			initializePatch(patch);

			if (!_dedupeChecksums)
			{
				_result.push_back(patch);
			}
			else
			{
				const auto md5 = std::string(MD5(it + 9 + 17, 256 - 17 - 3).toHexString().toRawUTF8());

				if (_dedupeChecksums->find(md5) == _dedupeChecksums->end())
				{
					_dedupeChecksums->insert(md5);
					_result.push_back(patch);
				}
			}

			return true;
		}
		return false;
	}

	uint32_t PatchBrowser::loadBankFile(std::vector<Patch>& _result, std::set<std::string>* _dedupeChecksums, const File& file)
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
			splitMultipleSysex(packets, d);

			return load(_result, _dedupeChecksums, packets);
		}

		if (ext == ".mid" || ext == ".midi")
		{
			std::vector<uint8_t> data;

			synthLib::MidiToSysex::readFile(data, file.getFullPathName().getCharPointer());

			if (data.empty())
				return 0;

			std::vector<std::vector<uint8_t>> packets;
			splitMultipleSysex(packets, data);

			return load(_result, _dedupeChecksums, packets);
		}

		return 0;
	}

	void PatchBrowser::fileClicked(const File& file, const MouseEvent& e)
	{
		const auto ext = file.getFileExtension().toLowerCase();
		const auto path = file.getParentDirectory().getFullPathName();
		if (file.isDirectory() && e.mods.isRightButtonDown())
		{
			auto p = PopupMenu();
			p.addItem("Add directory contents to patch list", [this, file]()
				{
					m_patches.clear();
					m_checksums.clear();
					std::set<std::string> dedupeChecksums;

					std::vector<Patch> patches;

					for (const auto& f : RangedDirectoryIterator(file, false, "*.syx;*.mid;*.midi", File::findFiles))
						loadBankFile(patches, &dedupeChecksums, f.getFile());

					m_filteredPatches.clear();

					for (const auto& patch : patches)
					{
						const auto searchValue = m_search.getText();

						m_patches.add(patch);

						if (searchValue.isEmpty() || patch.name.containsIgnoreCase(searchValue))
							m_filteredPatches.add(patch);
					}
					m_patchList.updateContent();
					m_patchList.deselectAllRows();
					m_patchList.repaint();
				});
			p.showMenuAsync(PopupMenu::Options());

			return;
		}
		m_properties->setValue("virus_bank_dir", path);

		if (file.existsAsFile() && ext == ".syx" || ext == ".midi" || ext == ".mid")
		{
			m_patches.clear();
			std::vector<Patch> patches;
			loadBankFile(patches, nullptr, file);
			m_filteredPatches.clear();
			for (const auto& patch : patches)
			{
				const auto searchValue = m_search.getText();
				m_patches.add(patch);
				if (searchValue.isEmpty() || patch.name.containsIgnoreCase(searchValue))
					m_filteredPatches.add(patch);
			}
			m_patchList.updateContent();
			m_patchList.deselectAllRows();
			m_patchList.repaint();
		}

		if(m_romBankSelect)
			m_romBankSelect->setSelectedItemIndex(0);
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
			text = g_categories[rowElement.category1];
		else if (columnId == Columns::CAT2)
			text = g_categories[rowElement.category2];
		else if (columnId == Columns::ARP)
			text = rowElement.data[129] != 0 ? "Y" : " ";
		else if (columnId == Columns::UNI)
			text = rowElement.unison == 0 ? " " : String(rowElement.unison + 1);
		else if (columnId == Columns::ST)
			text = rowElement.transpose != 64 ? String(rowElement.transpose - 64) : " ";
		else if (columnId == Columns::VER) {
			if (rowElement.model < ModelList.size())
				text = ModelList[rowElement.model];
		}
		g.drawText(text, 2, 0, width - 4, height, Justification::centredLeft, true); // [6]
		g.setColour(m_patchList.getLookAndFeel().findColour(ListBox::backgroundColourId));
		g.fillRect(width - 1, 0, 1, height); // [7]
	}

	void PatchBrowser::selectedRowsChanged(int lastRowSelected)
	{
		const auto idx = m_patchList.getSelectedRow();

		if (idx == -1)
			return;

		// force to edit buffer
		const auto part = m_controller.isMultiMode() ? m_controller.getCurrentPart() : static_cast<uint8_t>(virusLib::ProgramType::SINGLE);

		auto sysex = m_filteredPatches[idx].sysex;
		sysex[7] = toMidiByte(virusLib::BankNumber::EditBuffer);
		sysex[8] = part;

		m_controller.sendSysEx(sysex);

		m_controller.sendSysEx(m_controller.constructMessage({ virusLib::REQUEST_SINGLE, 0x0, part }));
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
				return m_direction * (first.category1 - second.category1);
			if (m_attributeToSort == Columns::CAT2)
				return m_direction * (first.category2 - second.category2);
			if (m_attributeToSort == Columns::ARP)
				return m_direction * (first.data[129] - second.data[129]);
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

	void PatchBrowser::loadRomBank(uint32_t _bankIndex)
	{
		const auto& singles = m_editor.getController().getSinglePresets();

		if(_bankIndex >= singles.size())
			return;

		const auto& bank = singles[_bankIndex];

		const auto searchValue = m_search.getText();

		m_patches.clear();
		m_filteredPatches.clear();

		for(size_t s=0; s<bank.size(); ++s)
		{
			Patch patch;

			patch.progNumber = static_cast<int>(s);
			patch.data.insert(patch.data.begin(), bank[s].data.begin(), bank[s].data.end());

			initializePatch(patch);

			// build sysex message

			patch.sysex.push_back(0xf0);
			patch.sysex.push_back(0x00);	// Manufacturer
			patch.sysex.push_back(0x20);
			patch.sysex.push_back(0x33);
			patch.sysex.push_back(0x01);	// Product Id = Virus
			patch.sysex.push_back(m_editor.getController().getDeviceId());
			patch.sysex.push_back(virusLib::DUMP_SINGLE);
			patch.sysex.push_back(static_cast<uint8_t>(_bankIndex));			// bank
			patch.sysex.push_back(static_cast<uint8_t>(patch.progNumber));
			patch.sysex.insert(patch.sysex.end(), patch.data.begin(), patch.data.end());

			// checksum
			uint8_t cs = 0;

			for(size_t i=5; i<patch.sysex.size(); ++i)
				cs += patch.sysex[i];

			patch.sysex.push_back(cs & 0x7f);
			patch.sysex.push_back(0xf7);

			m_patches.add(patch);

			if (searchValue.isEmpty() || patch.name.containsIgnoreCase(searchValue))
				m_filteredPatches.add(patch);
		}

		m_patchList.updateContent();
		m_patchList.deselectAllRows();
		m_patchList.repaint();
	}

	void PatchBrowser::splitMultipleSysex(std::vector<std::vector<uint8_t>>& _dst, const std::vector<uint8_t>& _src)
	{
		for (size_t i = 0; i < _src.size(); ++i)
		{
			if (_src[i] != 0xf0)
				continue;

			for (size_t j = i + 1; j < _src.size(); ++j)
			{
				if (_src[j] != 0xf7)
					continue;

				std::vector<uint8_t> entry;
				entry.insert(entry.begin(), _src.begin() + i, _src.begin() + j + 1);

				_dst.emplace_back(entry);

				i = j;
				break;
			}
		}
	}
}

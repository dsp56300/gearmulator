#include "../VirusParameterBinding.h"
#include "Virus_PatchBrowser.h"
#include "VirusEditor.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_cryptography/juce_cryptography.h>

#include "../../synthLib/midiToSysex.h"

using namespace juce;
using namespace virusLib;

const Array<String> g_categories = {"", "Lead",	 "Bass",	  "Pad",	   "Decay",	   "Pluck",
                             "Acid", "Classic", "Arpeggiator", "Effects",	"Drums",	"Percussion",
                             "Input", "Vocoder", "Favourite 1", "Favourite 2", "Favourite 3"};

PatchBrowser::PatchBrowser(VirusParameterBinding & _parameterBinding, Virus::Controller& _controller) :
    m_parameterBinding(_parameterBinding),
    m_controller(_controller),
    m_fileFilter("*.syx;*.mid;*.midi", "*", "virus patch dumps"),
	m_bankList(FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, File::getSpecialLocation(File::SpecialLocationType::currentApplicationFile), &m_fileFilter, nullptr),
    m_search("Search Box"),
    m_patchList("Patch browser"),
    m_properties(m_controller.getConfig())
{
	const auto bankDir = m_properties->getValue("virus_bank_dir", "");
    if (bankDir != "" && File(bankDir).isDirectory())
    {
        m_bankList.setRoot(bankDir);
    }

    setBounds(22, 30, 1000, 600);
    m_bankList.setBounds(16, 28, 480, 540);

    m_patchList.setBounds(m_bankList.getBounds().translated(m_bankList.getWidth(), 0));

    m_patchList.getHeader().addColumn("#", Columns::INDEX, 32);
    m_patchList.getHeader().addColumn("Name", Columns::NAME, 130);
    m_patchList.getHeader().addColumn("Category1", Columns::CAT1, 84);
    m_patchList.getHeader().addColumn("Category2", Columns::CAT2, 84);
    m_patchList.getHeader().addColumn("Arp", Columns::ARP, 32);
    m_patchList.getHeader().addColumn("Uni", Columns::UNI, 32);
    m_patchList.getHeader().addColumn("ST+-", Columns::ST, 32);
    m_patchList.getHeader().addColumn("Ver", Columns::VER, 32);
    addAndMakeVisible(m_bankList);
    addAndMakeVisible(m_patchList);

    m_search.setSize(m_patchList.getWidth(), 20);
    m_search.setColour(TextEditor::textColourId, Colours::white);
    m_search.setTopLeftPosition(m_patchList.getBounds().getBottomLeft().translated(0, 8));
    m_search.onTextChange = [this]
	{
        m_filteredPatches.clear();
        for(const auto& patch : m_patches)
		{
            const auto searchValue = m_search.getText();
            if (searchValue.isEmpty()) 
			{
                m_filteredPatches.add(patch);
            }
            else if(patch.name.containsIgnoreCase(searchValue))
			{
                m_filteredPatches.add(patch);
            }
        }
        m_patchList.updateContent();
        m_patchList.deselectAllRows();
        m_patchList.repaint();
    };
    m_search.setTextToShowWhenEmpty("search...", Colours::grey);
    addAndMakeVisible(m_search);
    m_bankList.addListener(this);
    m_patchList.setModel(this);
}

void PatchBrowser::selectionChanged() {}

PresetVersion guessVersion(const uint8_t* _data)
{
	if(_data)
		return static_cast<PresetVersion>(_data[0]);
	return PresetVersion::A;
}

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
		patch.name = parseAsciiText(patch.data, 128 + 112);
		patch.category1 = patch.data[251];
		patch.category2 = patch.data[252];
		patch.unison = patch.data[97];
		patch.transpose = patch.data[93];
		patch.model = guessVersion(&patch.data[0]);

		if(!_dedupeChecksums)
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

	    if (!synthLib::MidiToSysex::readFile(data, file.getFullPathName().getCharPointer()))
		    return 0;

	    std::vector<std::vector<uint8_t>> packets;
	    splitMultipleSysex(packets, data);

	    return load(_result, _dedupeChecksums, packets);
    }
    return 0;
}

void PatchBrowser::fileClicked(const File &file, const MouseEvent &e)
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

            for(const auto& patch : patches)
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

    if(file.existsAsFile() && ext == ".syx" || ext == ".midi" || ext == ".mid")
	{
        m_patches.clear();
		std::vector<Patch> patches;
        loadBankFile(patches, nullptr, file);
        m_filteredPatches.clear();
        for(const auto& patch : patches)
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

}

void PatchBrowser::fileDoubleClicked(const File &file) {}

void PatchBrowser::browserRootChanged(const File &newRoot) {}

int PatchBrowser::getNumRows() { return m_filteredPatches.size(); }

void PatchBrowser::paintRowBackground(Graphics &g, int rowNumber, int width, int height, bool rowIsSelected) {
	const auto alternateColour = getLookAndFeel()
	                             .findColour(ListBox::backgroundColourId)
	                             .interpolatedWith(getLookAndFeel().findColour(ListBox::textColourId), 0.03f);
    if (rowIsSelected)
        g.fillAll(Colours::lightblue);
    else if (rowNumber & 1)
        g.fillAll(alternateColour);
}

void PatchBrowser::paintCell(Graphics &g, int rowNumber, int columnId, int width, int height, bool rowIsSelected) {
    g.setColour(rowIsSelected ? Colours::darkblue
                                : getLookAndFeel().findColour(ListBox::textColourId)); // [5]

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
    else if(columnId == Columns::UNI)
        text = rowElement.unison == 0 ? " " : String(rowElement.unison+1);
    else if(columnId == Columns::ST)
        text = rowElement.transpose != 64 ? String(rowElement.transpose - 64) : " ";
    else if (columnId == Columns::VER)
	{
		switch (rowElement.model)
		{
		case A:	text = "A"; break;
		case B: text = "B"; break;
		case C: text = "C"; break;
		case D: text = "TI"; break;
		case D2: text = "TI2"; break;
		default:
			if (rowElement.model < B)
				text = "A";
			else if (rowElement.model >= D)
				text = "TI";
			else
				text = "";
			break;
		}
    }
    g.drawText(text, 2, 0, width - 4, height, Justification::centredLeft, true); // [6]
    g.setColour(getLookAndFeel().findColour(ListBox::backgroundColourId));
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

void PatchBrowser::cellDoubleClicked(int rowNumber, int columnId, const MouseEvent &)
{
    if(rowNumber == m_patchList.getSelectedRow())
        selectedRowsChanged(0);
}

class PatchBrowser::PatchBrowserSorter
{
public:
    PatchBrowserSorter (const int attributeToSortBy, const bool forwards)
        : attributeToSort (attributeToSortBy),
            direction (forwards ? 1 : -1)
    {}

    int compareElements (const Patch& first, const Patch& second) const
    {
        if(attributeToSort == Columns::INDEX)
            return direction * (first.progNumber - second.progNumber);
        if (attributeToSort == Columns::NAME)
            return direction * first.name.compareIgnoreCase(second.name);
        if (attributeToSort == Columns::CAT1)
            return direction * (first.category1 - second.category1);
        if (attributeToSort == Columns::CAT2)
            return direction * (first.category2 - second.category2);
        if (attributeToSort == Columns::ARP)
            return direction * (first.data[129]- second.data[129]);
        if (attributeToSort == Columns::UNI)
            return direction * (first.unison - second.unison);
        if (attributeToSort == Columns::VER)
            return direction * (first.model - second.model);
        if (attributeToSort == Columns::ST)
            return direction * (first.transpose - second.transpose);
        return direction * (first.progNumber - second.progNumber);
    }

private:
    const int attributeToSort;
	const int direction;
};

void PatchBrowser::sortOrderChanged(int newSortColumnId, bool isForwards)
{
    if (newSortColumnId != 0)
    {
        PatchBrowserSorter sorter (newSortColumnId, isForwards);
        m_filteredPatches.sort(sorter);
        m_patchList.updateContent();
    }
}

void PatchBrowser::splitMultipleSysex(std::vector<std::vector<uint8_t>>& _dst, const std::vector<uint8_t>& _src)
{
	for(size_t i=0; i<_src.size(); ++i)
	{
		if(_src[i] != 0xf0)
			continue;

		for(size_t j=i+1; j < _src.size(); ++j)
		{
			if(_src[j] != 0xf7)
				continue;

			std::vector<uint8_t> entry;
			entry.insert(entry.begin(), _src.begin() + i, _src.begin() + j + 1);

			_dst.emplace_back(entry);

			i = j;
			break;
		}
	}
}

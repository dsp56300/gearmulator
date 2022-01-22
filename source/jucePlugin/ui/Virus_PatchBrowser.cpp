#include "../VirusParameterBinding.h"
#include "BinaryData.h"
#include "Ui_Utils.h"
#include "Virus_PatchBrowser.h"
#include "VirusEditor.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_cryptography/juce_cryptography.h>
using namespace juce;
using namespace virusLib;
constexpr auto comboBoxWidth = 98;
const juce::Array<juce::String> categories = {"", "Lead",	 "Bass",	  "Pad",	   "Decay",	   "Pluck",
                             "Acid", "Classic", "Arpeggiator", "Effects",	"Drums",	"Percussion",
                             "Input", "Vocoder", "Favourite 1", "Favourite 2", "Favourite 3"};

PatchBrowser::PatchBrowser(VirusParameterBinding & _parameterBinding, Virus::Controller& _controller) :
    m_parameterBinding(_parameterBinding),
    m_controller(_controller),
    m_patchList("Patch browser"),
    m_fileFilter("*.syx;*.mid;*.midi", "*", "virus patch dumps"),
    m_bankList(FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, File::getSpecialLocation(File::SpecialLocationType::currentApplicationFile), &m_fileFilter, NULL),
    m_search("Search Box")
{
    m_properties = m_controller.getConfig();
    
    auto bankDir = m_properties->getValue("virus_bank_dir", "");
    if (bankDir != "" && juce::File(bankDir).isDirectory())
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
    m_search.setColour(TextEditor::textColourId, juce::Colours::white);
    m_search.setTopLeftPosition(m_patchList.getBounds().getBottomLeft().translated(0, 8));
    m_search.onTextChange = [this] {
        m_filteredPatches.clear();
        for(auto patch : m_patches) {
            const auto searchValue = m_search.getText();
            if (searchValue.isEmpty()) {
                m_filteredPatches.add(patch);
            }
            else if(patch.name.containsIgnoreCase(searchValue)) {
                m_filteredPatches.add(patch);
            }
        }
        m_patchList.updateContent();
        m_patchList.deselectAllRows();
        m_patchList.repaint();
    };
    m_search.setTextToShowWhenEmpty("search...", juce::Colours::grey);
    addAndMakeVisible(m_search);
    m_bankList.addListener(this);
    m_patchList.setModel(this);
}

void PatchBrowser::selectionChanged() {}

VirusModel guessVersion(uint8_t *data) {
    if (data[51] > 3) {
        // check extra filter modes
        return VirusModel::C;
    }
    if(data[179] == 0x40 && data[180] == 0x40) // soft knobs don't exist on B so they have fixed value
        return VirusModel::B;
    /*if (data[232] != 0x03 || data[235] != 0x6c || data[238] != 0x01) { // extra mod slots
        return VirusModel::C;
    }*/
    /*if(data[173] != 0x00 || data[174] != 0x00) // EQ
        return VirusModel::C;*/
    /*if (data[220] != 0x40 || data[221] != 0x54 || data[222] != 0x20 || data[223] != 0x40 || data[224] != 0x40) {
        // eq controls
        return VirusModel::C;
    }*/
    return VirusModel::C;

}
int PatchBrowser::loadBankFile(const juce::File& file, const int _startIndex = 0, const bool dedupe = false) {
    auto ext = file.getFileExtension().toLowerCase();
    auto path = file.getParentDirectory().getFullPathName();
    int loadedCount = 0;
    int index = _startIndex;
    if (ext == ".syx")
    {
        juce::MemoryBlock data;
        if (!file.loadFileAsData(data)) {
            return 0;
        }
        for (auto it = data.begin(); it != data.end(); it += 267)
        {
            if ((uint8_t)*it == (uint8_t)0xf0
                    && (uint8_t)*(it+1) == (uint8_t)0x00
                    && (uint8_t)*(it+2) == (uint8_t)0x20
                    && (uint8_t)*(it+3) == (uint8_t)0x33
                    && (uint8_t)*(it+4) == (uint8_t)0x01
                    && (uint8_t)*(it+6) == (uint8_t)virusLib::DUMP_SINGLE)
            {
                Patch patch;
                patch.progNumber = index;
                data.copyTo(patch.data, 267*index + 9, 256);
                patch.name = parseAsciiText(patch.data, 128 + 112);
                patch.category1 = patch.data[251];
                patch.category2 = patch.data[252];
                patch.unison = patch.data[97];
                patch.transpose = patch.data[93];
                if ((uint8_t)*(it + 266) != (uint8_t)0xf7 && (uint8_t)*(it + 266) != (uint8_t)0xf8) {
                        patch.model = VirusModel::TI;
                }
                else {
                    patch.model = guessVersion(patch.data);
                }
                auto md5 = juce::MD5(it+9 + 17, 256-17-3).toHexString();
                if(!dedupe || !m_checksums.contains(md5)) {
                    m_checksums.set(md5, true);
                    m_patches.add(patch);
                    index++;
                }
            }
        }
    }
    else if (ext == ".mid" || ext == ".midi")
    {
        juce::MemoryBlock data;
        if (!file.loadFileAsData(data))
        {
            return 0;
        }

        const uint8_t *ptr = (uint8_t *)data.getData();
        const auto end = ptr + data.getSize();

        for (auto it = ptr; it < end; it += 1)
        {
            if ((uint8_t)*it == (uint8_t)0xf0 && (it + 267) < end) // we don't check for sysex eof so we can load TI banks
            {
                if ((uint8_t) *(it+1) == (uint8_t)0x00
                    && (uint8_t)*(it+2) == 0x20
                    && (uint8_t)*(it+3) == 0x33
                    && (uint8_t)*(it+4) == 0x01
                    && (uint8_t)*(it+6) == virusLib::DUMP_SINGLE)
                {
                    auto syx = Virus::SysEx(it, it + 267);
                    syx[7] = 0x01; // force to bank a
                    syx[266] = 0xf7;

                    Patch patch;
                    patch.progNumber = index;
                    std::copy(syx.begin() + 9, syx.end() - 2, patch.data);
                    patch.name = parseAsciiText(patch.data, 128 + 112);
                    patch.category1 = patch.data[251];
                    patch.category2 = patch.data[252];
                    patch.unison = patch.data[97];
                    patch.transpose = patch.data[93];
                    if ((uint8_t)*(it + 266) != (uint8_t)0xf7 && (uint8_t)*(it + 266) != (uint8_t)0xf8) {
                        patch.model = VirusModel::TI;
                    }
                    else {
                        patch.model = guessVersion(patch.data);
                    }
                    auto md5 = juce::MD5(it+9 + 17, 256-17-3).toHexString();
                    if(!dedupe || !m_checksums.contains(md5)) {
                        m_checksums.set(md5, true);
                        m_patches.add(patch);
                        index++;
                    }

                    it += 266;
                }
                else if((uint8_t)*(it+3) == 0x00 // some midi files have two bytes after the 0xf0
                    && (uint8_t)*(it+4) == 0x20
                    && (uint8_t)*(it+5) == 0x33
                    && (uint8_t)*(it+6) == 0x01
                    && (uint8_t)*(it+8) == virusLib::DUMP_SINGLE)
                {
                    auto syx = Virus::SysEx();
                    syx.push_back(0xf0);
                    for (auto i = it + 3; i < it + 3 + 266; i++)
                    {
                        syx.push_back((uint8_t)*i);
                    }
                    syx[7] = 0x01; // force to bank a
                    syx[266] = 0xf7;
                    
                    Patch patch;
                    std::memcpy(patch.data, syx.data()+9, 256);
                    patch.progNumber = index;
                    patch.name = parseAsciiText(patch.data, 128 + 112);
                    patch.category1 = patch.data[251];
                    patch.category2 = patch.data[252];
                    patch.unison = patch.data[97];
                    patch.transpose = patch.data[93];
                    if ((uint8_t)*(it + 2 + 266) != (uint8_t)0xf7 && (uint8_t)*(it + 2 + 266) != (uint8_t)0xf8) {
                        patch.model = VirusModel::TI;
                    }
                    else {
                        patch.model = guessVersion(patch.data);
                    }
                    auto md5 = juce::MD5(it+2+9 + 17, 256-17-3).toHexString();
                    if(!dedupe || !m_checksums.contains(md5)) {
                        m_checksums.set(md5, true);
                        m_patches.add(patch);
                        index++;
                    }
                    loadedCount++;

                    it += 266;
                }
            }
        }
    }
    return index;
}

void PatchBrowser::fileClicked(const juce::File &file, const juce::MouseEvent &e)
{
    auto ext = file.getFileExtension().toLowerCase();
    auto path = file.getParentDirectory().getFullPathName();
    if (file.isDirectory() && e.mods.isRightButtonDown()) {
        auto p = juce::PopupMenu();
        p.addItem("Add directory contents to patch list", [this, file]() {
            m_patches.clear();
            m_checksums.clear();
            int lastIndex = 0;
            for (auto f : juce::RangedDirectoryIterator(file, false, "*.syx;*.mid;*.midi", juce::File::findFiles)) {
                lastIndex = loadBankFile(f.getFile(), lastIndex, true);
            }
            m_filteredPatches.clear();
            for(auto patch : m_patches) {
                const auto searchValue = m_search.getText();
                if (searchValue.isEmpty()) {
                    m_filteredPatches.add(patch);
                }
                else if(patch.name.containsIgnoreCase(searchValue)) {
                    m_filteredPatches.add(patch);
                }
            }
            m_patchList.updateContent();
            m_patchList.deselectAllRows();
            m_patchList.repaint();    
        });
        p.showMenu(juce::PopupMenu::Options());
        
        return;
    }
    m_properties->setValue("virus_bank_dir", path);
    if(file.existsAsFile() && ext == ".syx" || ext == ".midi" || ext == ".mid") {
        m_patches.clear();
        loadBankFile(file);
        m_filteredPatches.clear();
        for(auto patch : m_patches) {
            const auto searchValue = m_search.getText();
            if (searchValue.isEmpty()) {
                m_filteredPatches.add(patch);
            }
            else if(patch.name.containsIgnoreCase(searchValue)) {
                m_filteredPatches.add(patch);
            }
        }
        m_patchList.updateContent();
        m_patchList.deselectAllRows();
        m_patchList.repaint();
    }

}

void PatchBrowser::fileDoubleClicked(const juce::File &file) {}

void PatchBrowser::browserRootChanged(const File &newRoot) {}

int PatchBrowser::getNumRows() { return m_filteredPatches.size(); }

void PatchBrowser::paintRowBackground(Graphics &g, int rowNumber, int width, int height, bool rowIsSelected) {
    auto alternateColour = getLookAndFeel()
                               .findColour(juce::ListBox::backgroundColourId)
                               .interpolatedWith(getLookAndFeel().findColour(juce::ListBox::textColourId), 0.03f);
    if (rowIsSelected)
        g.fillAll(juce::Colours::lightblue);
    else if (rowNumber % 2)
        g.fillAll(alternateColour);
}

void PatchBrowser::paintCell(Graphics &g, int rowNumber, int columnId, int width, int height, bool rowIsSelected) {
    g.setColour(rowIsSelected ? juce::Colours::darkblue
                                : getLookAndFeel().findColour(juce::ListBox::textColourId)); // [5]
    
    auto rowElement = m_filteredPatches[rowNumber];
    //auto text = rowElement.name;
    juce::String text = "";
    if (columnId == Columns::INDEX)
        text = juce::String(rowElement.progNumber);
    else if (columnId == Columns::NAME)
        text = rowElement.name;
    else if (columnId == Columns::CAT1)
        text = categories[rowElement.category1];
    else if (columnId == Columns::CAT2)
        text = categories[rowElement.category2];
    else if (columnId == Columns::ARP)
        text = rowElement.data[129] != 0 ? "Y" : " ";
    else if(columnId == Columns::UNI)
        text = rowElement.unison == 0 ? " " : juce::String(rowElement.unison+1);
    else if(columnId == Columns::ST)
        text = rowElement.transpose != 64 ? juce::String(rowElement.transpose - 64) : " ";
    else if (columnId == Columns::VER) {
        if(rowElement.model < ModelList.size())
            text = ModelList[rowElement.model];
    }
    g.drawText(text, 2, 0, width - 4, height, juce::Justification::centredLeft, true); // [6]
    g.setColour(getLookAndFeel().findColour(juce::ListBox::backgroundColourId));
    g.fillRect(width - 1, 0, 1, height); // [7]
}

void PatchBrowser::selectedRowsChanged(int lastRowSelected) {
    auto idx = m_patchList.getSelectedRow();
    if (idx == -1) {
        return;
    }
    uint8_t syxHeader[9] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x00, 0x10, 0x00, 0x00};
    syxHeader[8] = m_controller.isMultiMode() ? m_controller.getCurrentPart() : virusLib::ProgramType::SINGLE; // set edit buffer
    const uint8_t syxEof = 0xF7;
    uint8_t cs = syxHeader[5] + syxHeader[6] + syxHeader[7] + syxHeader[8];
    uint8_t data[256];
    for (int i = 0; i < 256; i++)
    {
        data[i] = m_filteredPatches[idx].data[i];
        cs += data[i];
    }
    cs = cs & 0x7f;
    Virus::SysEx syx;
    for (auto i : syxHeader)
    {
        syx.push_back(i);
    }
    for (auto i : data)
    {
        syx.push_back(i);
    }
    syx.push_back(cs);
    syx.push_back(syxEof);
    m_controller.sendSysEx(syx); // send to edit buffer
    m_controller.parseMessage(syx); // update ui
    getParentComponent()->postCommandMessage(VirusEditor::Commands::UpdateParts);
}

void PatchBrowser::cellDoubleClicked(int rowNumber, int columnId, const juce::MouseEvent &)
{
    if(rowNumber == m_patchList.getSelectedRow()) {
        selectedRowsChanged(0);
    }
}

class PatchBrowser::PatchBrowserSorter
{
public:
    PatchBrowserSorter (int attributeToSortBy, bool forwards)
        : attributeToSort (attributeToSortBy),
            direction (forwards ? 1 : -1)
    {}

    int compareElements (Patch first, Patch second) const
    {
        if(attributeToSort == Columns::INDEX) {
            return direction * (first.progNumber - second.progNumber);
        }
        else if (attributeToSort == Columns::NAME) {
            return direction * first.name.compareIgnoreCase(second.name);
        }
        else if (attributeToSort == Columns::CAT1) {
            return direction * (first.category1 - second.category1);
        }
        else if (attributeToSort == Columns::CAT2) {
            return direction * (first.category2 - second.category2);
        }
        else if (attributeToSort == Columns::ARP) {
            return direction * (first.data[129]- second.data[129]);
        }
        else if (attributeToSort == Columns::UNI) {
            return direction * (first.unison - second.unison);
        }
        else if (attributeToSort == Columns::VER) {
            return direction * (first.model - second.model);
        }
        else if (attributeToSort == Columns::ST) {
            return direction * (first.transpose - second.transpose);
        }
        return direction * (first.progNumber - second.progNumber);
    }

private:
    int attributeToSort;
    int direction;
};

void PatchBrowser::sortOrderChanged(int newSortColumnId, bool isForwards)
{
    if (newSortColumnId != 0)
    {
        PatchBrowser::PatchBrowserSorter sorter (newSortColumnId, isForwards);
        m_filteredPatches.sort(sorter);
        m_patchList.updateContent();
    }
}

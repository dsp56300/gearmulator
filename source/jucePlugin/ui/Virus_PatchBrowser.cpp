#include "../VirusParameterBinding.h"
#include "BinaryData.h"
#include "Ui_Utils.h"
#include "Virus_PatchBrowser.h"
#include <juce_gui_extra/juce_gui_extra.h>
using namespace juce;
constexpr auto comboBoxWidth = 98;
const juce::Array<juce::String> categories = {"", "Lead",	 "Bass",	  "Pad",	   "Decay",	   "Pluck",
                             "Acid", "Classic", "Arpeggiator", "Effects",	"Drums",	"Percussion",
                             "Input", "Vocoder", "Favourite 1", "Favourite 2", "Favourite 3"};

PatchBrowser::PatchBrowser(VirusParameterBinding & _parameterBinding, Virus::Controller& _controller) :
    m_parameterBinding(_parameterBinding),
    m_controller(_controller),
    m_patchList("Patch browser"),
    m_fileFilter("*.syx;*.mid;*.midi", "*", "virus patch dumps"),
    m_bankList(FileBrowserComponent::canSelectFiles, File::getSpecialLocation(File::SpecialLocationType::currentApplicationFile), &m_fileFilter, NULL)
{
    juce::PropertiesFile::Options opts;
    opts.applicationName = "DSP56300 Emulator";
    opts.filenameSuffix = ".settings";
    opts.folderName = "DSP56300 Emulator";
    opts.osxLibrarySubFolder = "Application Support/DSP56300 Emulator";
    m_properties = new juce::PropertiesFile(opts);
    
    auto bankDir = m_properties->getValue("virus_bank_dir", "");
    if (bankDir != "" && juce::File(bankDir).isDirectory())
    {
        m_bankList.setRoot(bankDir);
    }

    setBounds(22, 30, 1000, 570);
    m_bankList.setBounds(16, 28, 480, 540);

    m_patchList.setBounds(m_bankList.getBounds().translated(m_bankList.getWidth(), 0));

    m_patchList.getHeader().addColumn("#", 0, 32);
    m_patchList.getHeader().addColumn("Name", 1, 150);
    m_patchList.getHeader().addColumn("Category1", 2, 100);
    m_patchList.getHeader().addColumn("Category2", 3, 100);
    m_patchList.getHeader().addColumn("Arp", 4, 32);
    addAndMakeVisible(m_bankList);
    addAndMakeVisible(m_patchList);

    m_bankList.addListener(this);
    m_patchList.setModel(this);
}

void PatchBrowser::selectionChanged() {}

void PatchBrowser::fileClicked(const juce::File &file, const juce::MouseEvent &e)
{
    auto ext = file.getFileExtension().toLowerCase();
    auto path = file.getParentDirectory().getFullPathName();
    m_properties->setValue("virus_bank_dir", path);
    if (ext == ".syx")
    {
        m_patches.clear();

        juce::MemoryBlock data;
        file.loadFileAsData(data);
        uint8_t index = 0;
        for (auto it = data.begin(); it != data.end(); it += 267)
        {
            if ((it + 267) <= data.end())
            {
                Patch patch;
                patch.progNumber = index;
                data.copyTo(patch.data, 267*index + 9, 256);
                patch.name = parseAsciiText(patch.data, 128 + 112);
                patch.category1 = patch.data[251];
                patch.category2 = patch.data[252];
                m_patches.add(patch);
                index++;
            }
        }
        m_patchList.updateContent();
        m_patchList.deselectAllRows();
        m_patchList.repaint(); // force repaint since row number doesn't often change
    }
    else if (ext == ".mid" || ext == ".midi")
    {
        m_patches.clear();
        juce::MemoryBlock data;
        if (!file.loadFileAsData(data))
        {
            return;
        }
        uint8_t index = 0;

        const uint8_t *ptr = (uint8_t *)data.getData();
        const auto end = ptr + data.getSize();

        for (auto it = ptr; it < end; it += 1)
        {
            if ((uint8_t)*it == (uint8_t)0xf0 && (it + 267) < end)
            {
                if ((uint8_t) * (it + 1) == (uint8_t)0x00)
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
                    m_patches.add(patch);
                    index++;
                    it += 266;
                }
                else // some midi files have two bytes after the 0xf0
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
                    m_patches.add(patch);
                    index++;

                    it += 266;
                }

            }
        }
        m_patchList.updateContent();
        m_patchList.repaint();
    }
}

void PatchBrowser::fileDoubleClicked(const juce::File &file) {}

void PatchBrowser::browserRootChanged(const File &newRoot) {}

int PatchBrowser::getNumRows() { return m_patches.size(); }

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
    
    auto rowElement = m_patches[rowNumber];
    //auto text = rowElement.name;
    juce::String text = "";
    if (columnId == 0)
        text = juce::String(rowElement.progNumber);
    else if (columnId == 1)
        text = rowElement.name;
    else if (columnId == 2)
        text = categories[rowElement.category1];
    else if (columnId == 3)
        text = categories[rowElement.category2];
    else if (columnId == 4)
        text = rowElement.data[129] != 0 ? "Y" : " ";
    g.drawText(text, 2, 0, width - 4, height, juce::Justification::centredLeft, true); // [6]
    g.setColour(getLookAndFeel().findColour(juce::ListBox::backgroundColourId));
    g.fillRect(width - 1, 0, 1, height); // [7]
}

void PatchBrowser::selectedRowsChanged(int lastRowSelected) { 
    auto idx = m_patchList.getSelectedRow();
    uint8_t syxHeader[9] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x00, 0x10, 0x00, 0x00};
    syxHeader[8] = m_controller.isMultiMode() ? m_controller.getCurrentPart() : virusLib::ProgramType::SINGLE; // set edit buffer
    const uint8_t syxEof = 0xF7;
    uint8_t cs = syxHeader[5] + syxHeader[6] + syxHeader[7] + syxHeader[8];
    uint8_t data[256];
    for (int i = 0; i < 256; i++)
    {
        data[i] = m_patches[idx].data[i];
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
}

void PatchBrowser::cellDoubleClicked(int rowNumber, int columnId, const juce::MouseEvent &)
{
    if(rowNumber == m_patchList.getSelectedRow()) {
        selectedRowsChanged(0);
    }
}

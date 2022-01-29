#include "../VirusParameterBinding.h"
#include "BinaryData.h"
#include "Ui_Utils.h"
#include "Virus_Panel5_PatchBrowser.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_cryptography/juce_cryptography.h>
#include "VirusEditor.h"

using namespace juce;
using namespace virusLib;

float fBrowserScaleFactor = 2.0f;

const juce::Array<juce::String> categories = {"", "Lead",	 "Bass",	  "Pad",	   "Decay",	   "Pluck",
                             "Acid", "Classic", "Arpeggiator", "Effects",	"Drums",	"Percussion",
                             "Input", "Vocoder", "Favourite 1", "Favourite 2", "Favourite 3"};

PatchBrowser::PatchBrowser(VirusParameterBinding & _parameterBinding, AudioPluginAudioProcessor &_processorRef) :
    m_parameterBinding(_parameterBinding), m_properties(_processorRef.getController().getConfig()),
    m_controller(_processorRef.getController()),
    m_patchList("Patch browser"),
    m_fileFilter("*.syx;*.mid;*.midi", "*", "virus patch dumps"),
    m_bankList(FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, File::getSpecialLocation(File::SpecialLocationType::currentApplicationFile), &m_fileFilter, NULL),
    m_search("Search Box")
{
	setupBackground(*this, m_background, BinaryData::panel_5_png, BinaryData::panel_5_pngSize);    
    
    m_bankList.setLookAndFeel(&m_landf);

    //PatchBrowser
    m_bankList.setBounds(0, 185/fBrowserScaleFactor , 1030/fBrowserScaleFactor , 810/fBrowserScaleFactor );
    auto bankDir = m_properties->getValue("virus_bank_dir", "");
    if (bankDir != "" && juce::File(bankDir).isDirectory())
    {
        m_bankList.setRoot(bankDir);
    }

    setBounds(0, 0, kPanelWidth, kPanelHeight);
    
    //PatchList
    m_patchList.setBounds(1049/fBrowserScaleFactor , 50/fBrowserScaleFactor , 1010/fBrowserScaleFactor , 870/fBrowserScaleFactor );

    m_patchList.getHeader().addColumn("#", ColumnsPatch::INDEX, 32);
    m_patchList.getHeader().addColumn("Name", ColumnsPatch::NAME, 130);
    m_patchList.getHeader().addColumn("Category1", ColumnsPatch::CAT1, 84);
    m_patchList.getHeader().addColumn("Category2", ColumnsPatch::CAT2, 84);
    m_patchList.getHeader().addColumn("Arp", ColumnsPatch::ARP, 32);
    m_patchList.getHeader().addColumn("Uni", ColumnsPatch::UNI, 32);
    m_patchList.getHeader().addColumn("ST+-", ColumnsPatch::ST, 32);
    m_patchList.getHeader().addColumn("Ver", ColumnsPatch::VER, 32);

    addAndMakeVisible(m_patchList);

    m_bankList.setTransform(AffineTransform::scale(fBrowserScaleFactor));
    m_patchList.setTransform(AffineTransform::scale(fBrowserScaleFactor));

    m_bankList.addListener(this);
    m_patchList.setModel(this);

    //Search
    m_search.setTransform(AffineTransform::scale(fBrowserScaleFactor));
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
                if (patch.name!="")                                   
                    m_filteredPatches.add(patch);
            }
        }
        m_patchList.updateContent();
        m_patchList.deselectAllRows();
        m_patchList.repaint();
    };
    m_search.setTextToShowWhenEmpty("search...", juce::Colours::grey);
    addAndMakeVisible(m_search);

    //Show Options Buttons/Cmb
    addAndMakeVisible(m_SavePreset);
    addAndMakeVisible(m_ROMBankSelect);
    addAndMakeVisible(m_bankList);

    m_ROMBankSelect.setBounds(510 - 961 / 2, 78 - 51  / 2, 961, 51);
    for (int i=1; i<=m_controller.getBankCount();i++)
	{
        m_ROMBankSelect.addItem("BANK: " + getCurrentPartBankStr((virusLib::BankNumber)i),i+1);
    }

    m_ROMBankSelect.onChange = [this]() 
    { 
        m_LastBankRomNoUsed = m_ROMBankSelect.getSelectedItemIndex()+1;
        LoadBankNr(m_LastBankRomNoUsed); 
        m_bIsFileMode = false;
        m_search.setText("", true);
        getParentComponent()->postCommandMessage(VirusEditor::Commands::SelectFirstPatch);
        getParentComponent()->postCommandMessage(VirusEditor::Commands::UpdateParts);
        SaveSettings();
    };

	m_SavePreset.setBounds(2197 - m_SavePreset.kWidth / 2, 72 - m_SavePreset.kHeight / 2, m_SavePreset.kWidth, m_SavePreset.kHeight);  
    m_SavePreset.onClick = [this]() { savePreset(); };
}

PatchBrowser::~PatchBrowser()
{
	setLookAndFeel(nullptr);
    m_bankList.setLookAndFeel(nullptr);
}

void PatchBrowser::IntiPatches() 
{
    //Read Last Patch used from config file
    m_bIsFileMode = m_properties->getBoolValue("is_file_used", false);
    m_LastBankRomNoUsed = m_properties->getIntValue("last_bank_rom_no_used", 1);
    m_LastFileUsed = m_properties->getValue("last_file_used", "");

    if(!m_bIsFileMode)
	{
        m_controller.setCurrentPartPreset(0,(virusLib::BankNumber)(m_LastBankRomNoUsed),0);
        m_ROMBankSelect.setSelectedItemIndex(m_LastBankRomNoUsed-1, dontSendNotification);    
        LoadBankNr(m_LastBankRomNoUsed); 
        m_search.setText("", true);
        
    }
	else
    {
        m_bankList.setFileName(m_LastFileUsed);
        m_bankList.selectionChanged();
        const juce::File &file(m_LastFileUsed);
        LoadPatchesFromFile(file);
    }
}

void PatchBrowser::SaveSettings() 
{
    m_properties->setValue("last_file_used", m_LastFileUsed);
    m_properties->setValue("is_file_used", m_bIsFileMode);
    m_properties->setValue("last_bank_rom_no_used", m_LastBankRomNoUsed);
    m_properties->save();
}


juce::String PatchBrowser::GetLastPatchSelected()
{
    return m_LastPatchSelected;
}

juce::TableListBox* PatchBrowser::GetTablePatchList()
{
    return &m_patchList;
}

juce::String PatchBrowser::GetSelectBankNum()
{
    return getCurrentPartBankStr((virusLib::BankNumber)m_LastBankRomNoUsed);
}




bool PatchBrowser::GetIsFileMode() 
{
    return m_bIsFileMode;
}

void PatchBrowser::selectionChanged() {}

void PatchBrowser::LoadBankNr(int iBankNo) 
{
    juce::StringArray patches = m_controller.getSinglePresetNames((virusLib::BankNumber)(iBankNo));
    m_patches.clear();

    for (int i=0 ; i<128 ; i++)
	{       
        Patch patch;
        patch.progNumber = i;
        //data.copyTo(patch.data, 267*index + 9, 256);
        patch.name = patches.strings[i];
        patch.category1 = 0;
        patch.category2 = 0;
        patch.unison = 0;
        patch.transpose = 0;
        patch.model = VirusModel::A;
        m_patches.add(patch);
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

    //Show popup only on directory
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
        LoadPatchesFromFile(file); 
        m_LastFileUsed = file.getFullPathName();
    }
    m_bIsFileMode = true;
    m_search.setText("", true);
    m_ROMBankSelect.setText("",true);
    SaveSettings();
    getParentComponent()->postCommandMessage(VirusEditor::Commands::SelectFirstPatch);
    getParentComponent()->postCommandMessage(VirusEditor::Commands::UpdateParts);
}

void PatchBrowser::LoadPatchesFromFile(const juce::File &file)
{
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


void PatchBrowser::fileDoubleClicked(const juce::File &file) {}

void PatchBrowser::browserRootChanged(const File &newRoot) {}

int PatchBrowser::getNumRows() 
{ 
    return m_filteredPatches.size(); 
}

void PatchBrowser::paintRowBackground(Graphics &g, int rowNumber, int width, int height, bool rowIsSelected) 
{
    auto alternateColour = getLookAndFeel()
                               .findColour(juce::ListBox::backgroundColourId)
                               .interpolatedWith(getLookAndFeel().findColour(juce::ListBox::textColourId), 0.03f);
    if (rowIsSelected)
        g.fillAll(juce::Colours::lightblue);
    else if (rowNumber % 2)
        g.fillAll(alternateColour);
}

void PatchBrowser::paintCell(Graphics &g, int rowNumber, int columnId, int width, int height, bool rowIsSelected) 
{
    //Banks from file
    g.setColour(rowIsSelected ? juce::Colours::darkblue
                            : getLookAndFeel().findColour(juce::ListBox::textColourId)); // [5]
    
    auto rowElement = m_filteredPatches[rowNumber];
    //auto text = rowElement.name;
    juce::String text = "";

    if (m_bIsFileMode)
    {
        if (columnId == ColumnsPatch::INDEX)
            text = juce::String(rowElement.progNumber+1);
        else if (columnId == ColumnsPatch::NAME)
            text = rowElement.name;
        else if (columnId == ColumnsPatch::CAT1)
            text = categories[rowElement.category1];
        else if (columnId == ColumnsPatch::CAT2)
            text = categories[rowElement.category2];
        else if (columnId == ColumnsPatch::ARP)
            text = rowElement.data[129] != 0 ? "Y" : " ";
        else if(columnId == ColumnsPatch::UNI)
            text = rowElement.unison == 0 ? " " : juce::String(rowElement.unison+1);
        else if(columnId == ColumnsPatch::ST)
            text = rowElement.transpose != 64 ? juce::String(rowElement.transpose - 64) : " ";
        else if (columnId == ColumnsPatch::VER) {
            if(rowElement.model < ModelList.size())
                text = ModelList[rowElement.model];
        }
    }
	else
	{
        if (columnId == ColumnsPatch::INDEX)
            text = juce::String(rowElement.progNumber+1);
        else if (columnId == ColumnsPatch::NAME)
            text = rowElement.name;
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
    
    juce::Component *c;
    c = m_patchList.getCellComponent(1,idx);

    if (m_bIsFileMode)
    {
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

        m_LastPatchSelected = parseAsciiText(data, 128 + 112);
        getParentComponent()->postCommandMessage(VirusEditor::Commands::UpdateParts);
    }
    else 
    {
        m_controller.setCurrentPartPreset(m_controller.getCurrentPart(), (virusLib::BankNumber) m_LastBankRomNoUsed, m_filteredPatches[idx].progNumber);
        getParentComponent()->postCommandMessage(VirusEditor::Commands::UpdateParts);
    }
}

void PatchBrowser::cellDoubleClicked(int rowNumber, int columnId, const juce::MouseEvent &)
{
   /* if(m_modeIndex==1)
    {
         selectedRowsChanged(0);
    }
    else if (m_modeIndex==0)
    {
        m_controller.setCurrentPartPreset(m_controller.getCurrentPart(),m_controller.getCurrentPartBank(m_controller.getCurrentPart()),lastRowSelected);
    }*/
}



void PatchBrowser::sortOrderChanged(int newSortColumnId, bool isForwards)
{
    if (newSortColumnId != 0)
    {
        PatchBrowser::PatchBrowserSorter sorter (newSortColumnId, isForwards);
        m_patches.sort(sorter);
        m_patchList.updateContent();
    }
}


void PatchBrowser::loadBankFileToRom(const juce::File &file)
{   
    bool sentData = false;
    auto sExt = file.getFileExtension().toLowerCase();
    m_previousPath = file.getParentDirectory().getFullPathName();;
    juce::MemoryBlock data;

    if (sExt == ".syx")
    {
        if (!file.loadFileAsData(data))
        {
            return;
        }

        for (auto it = data.begin(); it != data.end(); it += 267)
        {
            if ((it + 267) <= data.end())
            {
                m_controller.sendSysEx(Virus::SysEx(it, it + 267));
                sentData = true;
            }
        }
    }
    else if (sExt == ".mid" || sExt == ".midi")
    {
        if (!file.loadFileAsData(data))
        {
            return;
        }

        const uint8_t *ptr = (uint8_t *)data.getData();
        const auto end = ptr + data.getSize();

        for (auto it = ptr; it < end; it += 1)
        {
            if ((uint8_t)*it == (uint8_t)0xf0 && (it + 267) < end)
            {
                if ((uint8_t)*(it+1) == 0x00
                    && (uint8_t)*(it+2) == 0x20
                    && (uint8_t)*(it+3) == 0x33
                    && (uint8_t)*(it+4) == 0x01
                    && (uint8_t)*(it+6) == virusLib::DUMP_SINGLE)
                {
                    auto syx = Virus::SysEx(it, it + 267);
                    syx[7] = 0x01; // force to bank a
                    syx[266] = 0xf7;

                    m_controller.sendSysEx(syx);

                    it += 266;
                }
                else if((uint8_t)*(it+3) == 0x00
                    && (uint8_t)*(it+4) == 0x20
                    && (uint8_t)*(it+5) == 0x33
                    && (uint8_t)*(it+6) == 0x01
                    && (uint8_t)*(it+8) == virusLib::DUMP_SINGLE)// some midi files have two bytes after the 0xf0
                {
                    auto syx = Virus::SysEx();
                    syx.push_back(0xf0);
                    for (auto i = it + 3; i < it + 3 + 266; i++)
                    {
                        syx.push_back((uint8_t)*i);
                    }
                    syx[7] = 0x01; // force to bank a
                    syx[266] = 0xf7;
                    m_controller.sendSysEx(syx);
                    it += 266;
                }

                sentData = true;
            }
        }
    }

    if (sentData)
    {
        m_controller.onStateLoaded();
    }
}


void PatchBrowser::loadFile()
{
    juce::MemoryBlock data;
    juce::FileChooser chooser(
        "Choose syx/midi banks to import",
        m_previousPath.isEmpty()
            ? juce::File::getSpecialLocation(juce::File::currentApplicationFile).getParentDirectory()
            : m_previousPath,
        "*.syx,*.mid,*.midi", true);

    if (!chooser.browseForFileToOpen())
        return;
    
    loadBankFileToRom(chooser.getResult());
}

void PatchBrowser::savePreset() {
    juce::FileChooser chooser(
        "Save preset as syx",
        m_previousPath.isEmpty()
            ? juce::File::getSpecialLocation(juce::File::currentApplicationFile).getParentDirectory()
            : m_previousPath,
        "*.syx", true);

    if (!chooser.browseForFileToSave(true))
        return;

    bool sentData = false;
    const auto result = chooser.getResult();
    m_previousPath = result.getParentDirectory().getFullPathName();
    const auto ext = result.getFileExtension().toLowerCase();

    const uint8_t syxHeader[9] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x00, 0x10, 0x01, 0x00};
    const uint8_t syxEof[1] = {0xF7};
    uint8_t cs = syxHeader[5] + syxHeader[6] + syxHeader[7] + syxHeader[8];
    uint8_t data[256];
    for (int i = 0; i < 256; i++)
    {
        auto param = m_controller.getParamValue(m_controller.getCurrentPart(), i < 128 ? 0 : 1, i % 128);

        data[i] = param ? (int)param->getValue() : 0;
        cs += data[i];
    }
    cs = cs & 0x7f;

    result.deleteFile();
	result.create();
    result.appendData(syxHeader, 9);
    result.appendData(data, 256);
    result.appendData(&cs, 1);
    result.appendData(syxEof, 1);
}
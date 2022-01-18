#include "../VirusParameterBinding.h"
#include "BinaryData.h"
#include "Ui_Utils.h"
#include "Virus_Panel5_PatchBrowser.h"
#include <juce_gui_extra/juce_gui_extra.h>

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
    m_bankList(FileBrowserComponent::canSelectFiles, File::getSpecialLocation(File::SpecialLocationType::currentApplicationFile), &m_fileFilter, NULL)
{
	setupBackground(*this, m_background, BinaryData::panel_5_png, BinaryData::panel_5_pngSize);    
    bRunning = false;

    auto bankDir = m_properties->getValue("virus_bank_dir", "");
    if (bankDir != "" && juce::File(bankDir).isDirectory())
    {
        m_bankList.setRoot(bankDir);
    }

    setLookAndFeel(&m_landf);

    setBounds(0, 0, kPanelWidth, kPanelHeight);
    
    m_modeIndex = m_properties->getIntValue("patch_mode", 1);

    m_bankList.setBounds(0, 50/fBrowserScaleFactor , 1030/fBrowserScaleFactor , 935/fBrowserScaleFactor );
    //m_bankList.setLookAndFeel (&m_landf);


    //PatchList
    m_patchList.setBounds(1049/fBrowserScaleFactor , 50/fBrowserScaleFactor , 1010/fBrowserScaleFactor , 930/fBrowserScaleFactor );
    //m_patchList.setLookAndFeel (&m_landf);

    m_patchList.getHeader().addColumn("#", ColumnsPatch::INDEX, 32);
    m_patchList.getHeader().addColumn("Name", ColumnsPatch::NAME, 140);
    m_patchList.getHeader().addColumn("Category 1", ColumnsPatch::CAT1, 90);
    m_patchList.getHeader().addColumn("Category 2", ColumnsPatch::CAT2, 90);
    m_patchList.getHeader().addColumn("Arp", ColumnsPatch::ARP, 32);
    m_patchList.getHeader().addColumn("Uni", ColumnsPatch::UNI, 32);
    m_patchList.getHeader().addColumn("Ver", ColumnsPatch::VER, 32);

    addAndMakeVisible(m_patchList);

    m_bankList.setTransform(AffineTransform::scale(fBrowserScaleFactor));
    m_patchList.setTransform(AffineTransform::scale(fBrowserScaleFactor));

    m_bankList.addListener(this);
    m_patchList.setModel(this);

    //ROM Bank
    m_romBankList.setBounds(10, 50/fBrowserScaleFactor , 970/fBrowserScaleFactor , 935/fBrowserScaleFactor );
    m_romBankList.getHeader().addColumn("ROM BANK", ColumnsRomBanks::ROM_BANK_NAME, 450);
    m_romBankList.setTransform(AffineTransform::scale(fBrowserScaleFactor));
    
    RomBank rRomBank;
    for (int i=1;i<=m_controller.getBankCount();i++)
    {
        rRomBank.m_RomNumber = static_cast<virusLib::BankNumber>(static_cast<int>(rRomBank.m_RomNumber) + 1) ;
        m_romBankes.add(rRomBank);
    }

    m_romBankList.setModel(this);
    m_romBankList.updateContent();
    m_romBankList.deselectAllRows();
    m_romBankList.repaint(); // force repaint since row number doesn't often change
    m_romBankList.selectRow(0);


    //Show Options Buttons/Cmb
    addAndMakeVisible(m_LoadBank);
    addAndMakeVisible(m_SavePreset);
    addAndMakeVisible(m_Mode);
    addAndMakeVisible(m_romBankList);
    addAndMakeVisible(m_bankList);

	m_LoadBank.setBounds(2195 - m_LoadBank.kWidth / 2, 182 - m_LoadBank.kHeight / 2, m_LoadBank.kWidth, m_LoadBank.kHeight);  
	m_SavePreset.setBounds(2195 - m_SavePreset.kWidth / 2, 241 - m_SavePreset.kHeight / 2, m_SavePreset.kWidth, m_SavePreset.kHeight);  
	m_Mode.setBounds(2206+comboBoxXMargin - comboBox3Width / 2, 83 - comboBox3Height / 2, comboBox3Width+5, comboBox3Height-5);
    
    m_Mode.setLookAndFeel(&m_lookAndFeel);
    m_Mode.addItem("ROM",1);
    m_Mode.addItem("FILE",2);

    m_Mode.onChange = [this]() 
    { 
        if(bRunning)
        {
            if (m_Mode.getSelectedItemIndex()==0) 
            {
                //ROM
                m_romBankList.setVisible(true);
                m_bankList.setVisible(false);
                m_romBankList.updateContent();
                m_romBankList.deselectAllRows();
                m_romBankList.repaint(); // force repaint since row number doesn't often change
                m_romBankList.selectRow(0);
            }
            else
            {
                //FILE
                m_romBankList.setVisible(false);
                m_bankList.setVisible(true);
            }
            m_properties->setValue("patch_mode", m_Mode.getSelectedItemIndex()); 
            m_properties->save();
            m_modeIndex = m_Mode.getSelectedItemIndex();
        }
    };

    m_Mode.setSelectedItemIndex(m_modeIndex);

    m_LoadBank.onClick = [this]() { loadFile(); };
    m_SavePreset.onClick = [this]() { savePreset(); };

	startTimerHz(4);
}

void PatchBrowser::timerCallback()
{

}

void PatchBrowser::selectionChanged() 
{
    bRunning=true; //ugly
}

VirusModel guessVersion(uint8_t *data) 
{
    if (data[51] > 3) 
    {
        // check extra filter modes
        return VirusModel::C;
    }
    if(data[179] == 0x40 && data[180] == 0x40) // soft knobs don't exist on B so they have fixed value
    {
        return VirusModel::B;
    }
    /*if (data[232] != 0x03 || data[235] != 0x6c || data[238] != 0x01) { // extra mod slots
        return VirusModel::C;
    }*/
    /*if(data[173] != 0x00 || data[174] != 0x00) // EQ
        return VirusModel::C;*/
    /*if (data[220] != 0x40 || data[221] != 0x54 || data[222] != 0x20 || data[223] != 0x40 || data[224] != 0x40) {
        // eq controls
        return VirusModel::C;
    }*/
    //return VirusModel::C;

}
void PatchBrowser::fileClicked(const juce::File &file, const juce::MouseEvent &e)
{
    auto ext = file.getFileExtension().toLowerCase();
    auto path = file.getParentDirectory().getFullPathName();
    juce::MemoryBlock data;

    file.loadFileAsData(data);

    m_properties->setValue("virus_bank_dir", path);
    if (ext == ".syx")
    {
        m_patches.clear();
        loadBankFileToRom(file);  

        int index = 0;
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
                if ((uint8_t)*(it + 266) != (uint8_t)0xf7 && (uint8_t)*(it + 266) != (uint8_t)0xf8) {
                        patch.model = VirusModel::TI;
                }
                else {
                    patch.model = guessVersion(patch.data);
                }
                m_patches.add(patch);
                index++;
            }
        }

        m_patchList.updateContent();
        m_patchList.deselectAllRows();
        m_patchList.repaint(); // force repaint since row number doesn't often change
        m_patchList.selectRow(0);
    }
    else if (ext == ".mid" || ext == ".midi")
    {
        m_patches.clear();
        loadBankFileToRom(file);  

        uint8_t index = 0;

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
                    if ((uint8_t)*(it + 266) != (uint8_t)0xf7 && (uint8_t)*(it + 266) != (uint8_t)0xf8) {
                        patch.model = VirusModel::TI;
                    }
                    else {
                        patch.model = guessVersion(patch.data);
                    }
                    m_patches.add(patch);
                    index++;
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
                    if ((uint8_t)*(it + 2 + 266) != (uint8_t)0xf7 && (uint8_t)*(it + 2 + 266) != (uint8_t)0xf8) {
                        patch.model = VirusModel::TI;
                    }
                    else {
                        patch.model = guessVersion(patch.data);
                    }
                    m_patches.add(patch);
                    index++;

                    it += 266;
                }

            }

            
        }

        m_patchList.updateContent();
        m_patchList.deselectAllRows();
        m_patchList.repaint();
        m_patchList.selectRow(0);
    }
}

void PatchBrowser::fileDoubleClicked(const juce::File &file) {}

void PatchBrowser::browserRootChanged(const File &newRoot) {}

int PatchBrowser::getNumRows() 
{ 
    if(m_modeIndex==1)
    {
        return m_patches.size(); 
    }
    else if (m_modeIndex==0)
    {
        return m_romBankes.size(); 
    }
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
    if(m_modeIndex==1)
    {
        g.setColour(rowIsSelected ? juce::Colours::darkblue: getLookAndFeel().findColour(juce::ListBox::textColourId)); // [5]
        juce::String text = "";

        auto rowElement = m_patches[rowNumber];
        //auto text = rowElement.name;

        if (columnId == ColumnsPatch::INDEX)
            text = juce::String(rowElement.progNumber);
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
        else if (columnId == ColumnsPatch::VER) 
        {
            if(rowElement.model < ModelList.size())
                text = ModelList[rowElement.model];
        }
        g.drawText(text, 2, 0, width - 4, 20, juce::Justification::centredLeft, true); // [6]
        g.setColour(getLookAndFeel().findColour(juce::ListBox::backgroundColourId));
        g.fillRect(width - 1, 0, 1, 20); // [7]
    }
    else if (m_modeIndex==0)
    {
        g.setColour(rowIsSelected ? juce::Colours::darkblue: getLookAndFeel().findColour(juce::ListBox::textColourId)); // [5]
        juce::String text = "";

        auto rowElement = m_romBankes[rowNumber];
        juce::String sBankName="ERR";
        text = "Bank: ";

		switch (virusLib::BankNumber(rowNumber+1))
		{
			case virusLib::BankNumber::A: text += "A"; break;
			case virusLib::BankNumber::B: text += "B"; break;
			case virusLib::BankNumber::C: text += "C"; break;
			case virusLib::BankNumber::D: text += "D"; break;
			case virusLib::BankNumber::E: text += "E"; break;
			case virusLib::BankNumber::F: text += "F"; break;
			case virusLib::BankNumber::G: text += "G"; break;
			case virusLib::BankNumber::H: text += "H"; break;
        }

        g.drawText(text, 2, 0, width - 4, 20, juce::Justification::centredLeft, true); // [6]
        g.setColour(getLookAndFeel().findColour(juce::ListBox::backgroundColourId));
        g.fillRect(width - 1, 0, 1, 20); // [7]
    }
}

void PatchBrowser::selectedRowsChanged(int lastRowSelected) {
    auto idx = m_patchList.getSelectedRow();
    
    if (idx == -1) {
        return;
    }

    m_controller.setCurrentPartPreset(m_controller.getCurrentPart(),m_controller.getCurrentPartBank(m_controller.getCurrentPart()),lastRowSelected);

    /*uint8_t syxHeader[9] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x00, 0x10, 0x00, 0x00};
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
    syx.push_back(syxEof);*/
    //m_controller.sendSysEx(syx); // send to edit buffer
    //m_controller.parseMessage(syx); // update ui
}

void PatchBrowser::cellDoubleClicked(int rowNumber, int columnId, const juce::MouseEvent &)
{

    if(m_modeIndex==1)
    {
        if(rowNumber == m_patchList.getSelectedRow()) 
        {
        selectedRowsChanged(0);
        }
    }
    else if (m_modeIndex==0)
    {
        if(rowNumber == m_romBankList.getSelectedRow()) 
        {
            selectedRowsChanged(0);
        }
    }
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
        //Load first patch
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

    result.appendData(syxHeader, 9);
    result.appendData(data, 256);
    result.appendData(&cs, 1);
    result.appendData(syxEof, 1);
}
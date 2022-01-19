#pragma once

#include "../PluginProcessor.h"
#include "Virus_Buttons.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include "../VirusController.h"
#include "Virus_LookAndFeel.h"

class VirusParameterBinding;
class Virus::LookAndFeelPatchBrowser;

const juce::Array<juce::String> ModelList = {"A","B","C","TI"};

struct Patch
{
    int progNumber;
    juce::String name;
    uint8_t category1;
    uint8_t category2;
    uint8_t data[256];
    virusLib::VirusModel model;
    uint8_t unison;
    uint8_t transpose;
};

struct RomBank
{
    virusLib::BankNumber m_RomNumber;
};

class PatchBrowser : public juce::Component, juce::FileBrowserListener, juce::TableListBoxModel
{

public:
    PatchBrowser(VirusParameterBinding &_parameterBinding, AudioPluginAudioProcessor &_processorRef);
    ~PatchBrowser();
    void loadFile();
    void loadBankFileToRom(const juce::File &file);
    void savePreset();

private:
    bool bRunning;

    Virus::LookAndFeelPatchBrowser m_landf;
    Virus::LookAndFeel m_lookAndFeel;

    VirusParameterBinding &m_parameterBinding;
    Virus::Controller& m_controller;
    template <typename T> juce::String parseAsciiText(const T &msg, const int start) const
    {
        char text[Virus::Controller::kNameLength + 1];
        text[Virus::Controller::kNameLength] = 0; // termination
        for (auto pos = 0; pos < Virus::Controller::kNameLength; ++pos)
            text[pos] = msg[start + pos];
        return juce::String(text);
    }
    juce::WildcardFileFilter m_fileFilter;
    juce::FileBrowserComponent m_bankList;
    juce::TableListBox m_patchList;
    juce::TableListBox m_romBankList;
    juce::TextEditor m_search;
    juce::Array<RomBank> m_romBankes;
    juce::Array<Patch> m_patches;
    juce::Array<Patch> m_filteredPatches;
    juce::PropertiesFile *m_properties;
    juce::HashMap<juce::String, bool> m_checksums;
    int loadBankFile(const juce::File &file, const int _startIndex, const bool dedupe);
    // Inherited via FileBrowserListener
    Buttons::OptionButtonLoadBank m_LoadBank;
    Buttons::OptionButtonSavePreset m_SavePreset;
    juce::ComboBox m_Mode;
    int m_modeIndex;


    juce::String m_previousPath;

    // Inherited via FileBrowserListener
    void selectionChanged() override;
    void fileClicked(const juce::File &file, const juce::MouseEvent &e) override;
    void fileDoubleClicked(const juce::File &file) override;
    void browserRootChanged(const juce::File &newRoot) override;

    // Inherited via TableListBoxModel
    virtual int getNumRows() override;
    virtual void paintRowBackground(juce::Graphics &, int rowNumber, int width, int height,
                                    bool rowIsSelected) override;
    virtual void paintCell(juce::Graphics &, int rowNumber, int columnId, int width, int height,
                           bool rowIsSelected) override;

    virtual void selectedRowsChanged(int lastRowSelected) override;
    virtual void cellDoubleClicked (int rowNumber, int columnId, const juce::MouseEvent &) override;
    void sortOrderChanged(int newSortColumnId, bool isForwards) override;
    class PatchBrowserSorter;
    
    enum ColumnsPatch {
        INDEX = 1,
        NAME = 2,
        CAT1 = 3,
        CAT2 = 4,
        ARP = 5,
        UNI = 6,
        ST = 7,
        VER = 8,
    };

    enum ColumnsRomBanks {
        ROM_BANK_NAME = 1,
    };

    std::unique_ptr<juce::Drawable> m_background;
};


class PatchBrowser::PatchBrowserSorter
{
public:
    PatchBrowserSorter (int attributeToSortBy, bool forwards)
        : attributeToSort (attributeToSortBy),
            direction (forwards ? 1 : -1)
    {}

    int compareElements (Patch first, Patch second) const
    {
        if(attributeToSort == ColumnsPatch::INDEX) {
            return direction * (first.progNumber - second.progNumber);
        }
        else if (attributeToSort == ColumnsPatch::NAME) {
            return direction * first.name.compareIgnoreCase(second.name);
        }
        else if (attributeToSort == ColumnsPatch::CAT1) {
            return direction * (first.category1 - second.category1);
        }
        else if (attributeToSort == ColumnsPatch::CAT2) {
            return direction * (first.category2 - second.category2);
        }
        else if (attributeToSort == ColumnsPatch::ARP) {
            return direction * (first.data[129]- second.data[129]);
        }
        else if (attributeToSort == ColumnsPatch::UNI) {
            return direction * (first.unison - second.unison);
        }
        else if (attributeToSort == ColumnsPatch::VER) {
            return direction * (first.model - second.model);
        }
        else if (attributeToSort == ColumnsPatch::ST) {
            return direction * (first.transpose - second.transpose);
        }
        return direction * (first.progNumber - second.progNumber);
    }

private:
    int attributeToSort;
    int direction;
};



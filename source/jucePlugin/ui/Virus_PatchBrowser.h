#pragma once

#include "../PluginProcessor.h"
#include "Virus_Buttons.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include "../VirusController.h"
class VirusParameterBinding;

const juce::Array<juce::String> ModelList = {"A","B","C","TI"};
struct Patch
{
    int progNumber;
    juce::String name;
    uint8_t category1;
    uint8_t category2;
    std::vector<uint8_t> data;
    std::vector<uint8_t> sysex;
    virusLib::VirusModel model;
    uint8_t unison;
    uint8_t transpose;
};

class PatchBrowser : public juce::Component, juce::FileBrowserListener, juce::TableListBoxModel
{

public:
	PatchBrowser(VirusParameterBinding &_parameterBinding, Virus::Controller& _controller);

	static uint32_t load(std::vector<Patch>& _result, std::set<std::string>* _dedupeChecksums, const std::vector<std::vector<uint8_t>>& _packets);
	static bool load(std::vector<Patch>& _result, std::set<std::string>* _dedupeChecksums, const std::vector<uint8_t>& _data);
	static uint32_t loadBankFile(std::vector<Patch>& _result, std::set<std::string>* _dedupeChecksums, const juce::File& file);

	static void splitMultipleSysex(std::vector<std::vector<uint8_t>>& _dst, const std::vector<uint8_t>& _src);

    juce::FileBrowserComponent& getBankList() {return m_bankList; }
    juce::TableListBox& getPatchList() {return m_patchList; }
    juce::TextEditor& getSearchBox() {return m_search; }

private:
    VirusParameterBinding &m_parameterBinding;
    Virus::Controller& m_controller;
    template <typename T> static juce::String parseAsciiText(const T &msg, const int start)
    {
        char text[Virus::Controller::kNameLength + 1];
        text[Virus::Controller::kNameLength] = 0; // termination
        for (auto pos = 0; pos < Virus::Controller::kNameLength; ++pos)
            text[pos] = msg[start + pos];
        return juce::String(text);
    }
    juce::WildcardFileFilter m_fileFilter;
    juce::FileBrowserComponent m_bankList;
    juce::TextEditor m_search;
    juce::TableListBox m_patchList;
    juce::Array<Patch> m_patches;
    juce::Array<Patch> m_filteredPatches;
    juce::PropertiesFile *m_properties;
    juce::HashMap<juce::String, bool> m_checksums;
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
    enum Columns {
        INDEX = 1,
        NAME = 2,
        CAT1 = 3,
        CAT2 = 4,
        ARP = 5,
        UNI = 6,
        ST = 7,
        VER = 8,
    };
};

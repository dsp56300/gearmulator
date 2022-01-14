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
    uint8_t data[256];
    virusLib::VirusModel model;
    uint8_t unison;
};

class PatchBrowser : public juce::Component, juce::FileBrowserListener, juce::TableListBoxModel
{

public:
    PatchBrowser(VirusParameterBinding &_parameterBinding, Virus::Controller& _controller);

private:
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
    juce::Array<Patch> m_patches;
    juce::PropertiesFile *m_properties;

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
        VER = 7
    };
};

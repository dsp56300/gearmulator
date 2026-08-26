#pragma once

#include "virusJucePlugin/VirusProcessor.h"

class OsirusProcessor : public virus::VirusProcessor
{
public:
    OsirusProcessor();
    ~OsirusProcessor() override;

    jucePluginEditorLib::PluginEditorState* createEditorState() override;
};

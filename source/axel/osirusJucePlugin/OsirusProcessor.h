#pragma once

#include "virusJucePlugin/VirusProcessor.h"

class OsirusProcessor : public virus::VirusProcessor
{
public:
    OsirusProcessor();

    jucePluginEditorLib::PluginEditorState* createEditorState() override;
};

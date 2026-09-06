#pragma once

#include "virusJucePlugin/VirusProcessor.h"

class OsTIrusProcessor : public virus::VirusProcessor
{
public:
    OsTIrusProcessor();
    ~OsTIrusProcessor() override;

	jucePluginEditorLib::PluginEditorState* createEditorState() override;
};

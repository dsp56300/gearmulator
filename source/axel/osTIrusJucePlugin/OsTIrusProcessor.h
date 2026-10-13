#pragma once

#include "virusJucePlugin/VirusProcessor.h"

class OsTIrusProcessor : public virus::VirusProcessor
{
public:
    OsTIrusProcessor();

	jucePluginEditorLib::PluginEditorState* createEditorState() override;
};

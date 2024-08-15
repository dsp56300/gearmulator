#pragma once

#include "virusJucePlugin/VirusProcessor.h"

class OsTIrusProcessor : public virus::VirusProcessor
{
public:
    OsTIrusProcessor();
    ~OsTIrusProcessor() override;
    const char* findEmbeddedResource(const char* _name, uint32_t& _size) const override;
    jucePluginEditorLib::PluginEditorState* createEditorState() override;
};

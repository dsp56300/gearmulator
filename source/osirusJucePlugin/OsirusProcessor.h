#pragma once

#include "virusJucePlugin/VirusProcessor.h"

class OsirusProcessor : public virus::VirusProcessor
{
public:
    OsirusProcessor();
    ~OsirusProcessor() override;

    const char* findEmbeddedResource(const char* _name, uint32_t& _size) const override;

    jucePluginEditorLib::PluginEditorState* createEditorState() override;
};

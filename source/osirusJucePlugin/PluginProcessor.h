#pragma once

#include "../virusJucePlugin/PluginProcessor.h"

class OsirusProcessor : public AudioPluginAudioProcessor
{
public:
    OsirusProcessor();
    ~OsirusProcessor() override;

    const char* findEmbeddedResource(const char* _name, uint32_t& _size) const override;

    jucePluginEditorLib::PluginEditorState* createEditorState() override;
};

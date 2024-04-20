#pragma once

#include "../virusJucePlugin/PluginProcessor.h"

//==============================================================================
class OsTIrusProcessor : public AudioPluginAudioProcessor
{
public:
    OsTIrusProcessor();
    ~OsTIrusProcessor() override;
    const char* findEmbeddedResource(const char* _name, uint32_t& _size) const override;
    jucePluginEditorLib::PluginEditorState* createEditorState() override;
};

#pragma once

#include "pluginProcessor.h"

namespace jucePluginEditorLib
{
	// The most derived processor of every product. Its destructor runs before the members of the product processor
	// are destroyed, and the editor and the patch manager use them, so both go here. Never move this into the base
	// destructor: the asserts in ~Processor would become a use-after-free
	template<class T> class FinalProcessor final : public T
	{
	public:
		~FinalProcessor() override
		{
			this->destroyEditorState();
			this->destroyPatchManager();
		}
	};
}

// the plugin entry point of a product, T is its processor
#define TUS_PLUGIN_ENTRY(T)															\
	juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()						\
	{																				\
		auto* processor = new jucePluginEditorLib::FinalProcessor<T>();			\
		processor->finishConstruction();											\
		return processor;															\
	}

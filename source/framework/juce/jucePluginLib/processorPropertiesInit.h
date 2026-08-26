#pragma once

#include "processor.h"

namespace pluginLib
{
	inline Processor::Properties initProcessorProperties()
	{
		return Processor::Properties
		{
			JucePlugin_Name,
			JucePlugin_Manufacturer,
			JucePlugin_IsSynth,
			JucePlugin_WantsMidiInput,
			JucePlugin_ProducesMidiOutput,
			JucePlugin_IsMidiEffect,
			Plugin4CC,
			JucePlugin_Lv2Uri,
			{
				BinaryData::namedResourceListSize,
				BinaryData::originalFilenames,
				BinaryData::namedResourceList,
				BinaryData::getNamedResource
			}
		};
	}
}

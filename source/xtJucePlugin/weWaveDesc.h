#pragma once

#include "weData.h"

#include "juce_gui_basics/juce_gui_basics.h"

namespace xtJucePlugin
{
	enum class WaveDescSource
	{
		Invalid,
		ControlList,
		WaveList
	};

	struct WaveDesc : juce::ReferenceCountedObject
	{
		uint32_t waveIndex = g_invalidWaveIndex;
		uint32_t tableIndex = g_invalidIndex;
		uint32_t listIndex = g_invalidIndex;
		xt::WaveData data;
		WaveDescSource source = WaveDescSource::Invalid;
		static WaveDesc* fromDragSource(const juce::DragAndDropTarget::SourceDetails& _sourceDetails);
	};
}

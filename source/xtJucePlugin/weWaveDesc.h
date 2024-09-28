#pragma once

#include "weData.h"

#include "juce_gui_basics/juce_gui_basics.h"

namespace xtJucePlugin
{
	enum class WaveDescSource
	{
		Invalid,
		ControlTableList,
		WaveList,
		TablesList
	};

	struct WaveDesc : juce::ReferenceCountedObject
	{
		xt::WaveId waveId;
		xt::TableId tableId;
		xt::TableIndex tableIndex;
		xt::WaveData data;
		WaveDescSource source = WaveDescSource::Invalid;
		static WaveDesc* fromDragSource(const juce::DragAndDropTarget::SourceDetails& _sourceDetails);
	};
}

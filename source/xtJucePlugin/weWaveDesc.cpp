#include "weWaveDesc.h"

namespace xtJucePlugin
{
	WaveDesc* WaveDesc::fromDragSource(const juce::DragAndDropTarget::SourceDetails& _sourceDetails)
	{
		auto* desc = dynamic_cast<WaveDesc*>(_sourceDetails.description.getObject());
		return desc;
	}
}

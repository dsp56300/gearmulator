#pragma once

#include "juce_core/juce_core.h"

namespace jucePluginEditorLib::patchManager
{
	class SavePatchDesc : public juce::ReferenceCountedObject
	{
	public:
		SavePatchDesc(int _part) : m_part(_part)
		{
		}

		auto getPart() const { return m_part; }

	private:
		int m_part;
	};
}

#pragma once

#include <juce_audio_plugin_client/juce_audio_plugin_client.h>

#include "../../jucePluginLib/patchdb/patchdbtypes.h"

namespace pluginLib::patchDB
{
	struct Search;
}

namespace jucePluginEditorLib::patchManager
{
	class PatchManager;

	class List : public juce::ListBox, juce::ListBoxModel
	{
	public:
		explicit List(PatchManager& _pm);

		void setContent(const pluginLib::patchDB::SearchHandle& _handle);
		void setContent(const pluginLib::patchDB::Search& _search);

		// ListBoxModel
		int getNumRows() override;
		void paintListBoxItem(int _rowNumber, juce::Graphics& _g, int _width, int _height, bool _rowIsSelected) override;
		juce::var getDragSourceDescription(const juce::SparseSet<int>& rowsToDescribe) override;

		void selectedRowsChanged(int lastRowSelected) override;

		pluginLib::patchDB::PatchPtr getPatch(const size_t _index) const
		{
			if (_index >= m_patches.size())
				return {};
			return m_patches[_index];
		}
	private:
		PatchManager& m_patchManager;

		std::vector<pluginLib::patchDB::PatchPtr> m_patches;
	};
}

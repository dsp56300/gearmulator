#pragma once

#include "list.h"
#include "savepatchdesc.h"
#include "juce_gui_basics/juce_gui_basics.h"

#include "../../jucePluginLib/patchdb/patchdbtypes.h"

namespace pluginLib::patchDB
{
	struct Search;
	struct SearchRequest;
}

namespace jucePluginEditorLib::patchManager
{
	class Tree;
	static constexpr uint32_t g_invalidCount = ~0;
	static constexpr uint32_t g_unknownCount = g_invalidCount - 1;

	class PatchManager;

	class TreeItem : public juce::TreeViewItem, juce::Label::Listener
	{
	public:
		using FinishedEditingCallback = std::function<void(bool, const std::string&)>;

		TreeItem(PatchManager& _patchManager, const std::string& _title, uint32_t _count = g_invalidCount);
		~TreeItem() override;

		PatchManager& getPatchManager() const { return m_patchManager; }

		void setTitle(const std::string& _title);
		virtual void setCount(uint32_t _count);

		auto getSearchHandle() const { return m_searchHandle; }

		virtual void processDirty(const std::set<pluginLib::patchDB::SearchHandle>& _dirtySearches);

		virtual bool beginEdit() { return false; }

		bool beginEdit(const std::string& _initialText, FinishedEditingCallback&& _callback);

		virtual void patchDropped(const pluginLib::patchDB::PatchPtr& _patch) {}
		virtual void patchesDropped(const std::vector<pluginLib::patchDB::PatchPtr>& _patches);

		bool hasSearch() const;

		Tree* getTree() const;

		void removeFromParent(bool _destroy) const;
		void setParent(TreeViewItem* _parent, bool _sorted = false);

		const std::string& getText() const { return m_text; }

		// TreeViewItem
		void itemSelectionChanged(bool _isNowSelected) override;
		void itemDropped(const juce::DragAndDropTarget::SourceDetails& dragSourceDetails, int insertIndex) override;

		bool isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& _dragSourceDetails) override;

		virtual bool isInterestedInPatchList(const List* _list, const juce::Array<juce::var>& _indices) { return false; }
		virtual bool isInterestedInSavePatchDesc(const SavePatchDesc& _desc) { return false; }

		// juce::Label::Listener
		void editorHidden(juce::Label*, juce::TextEditor&) override;
		void labelTextChanged(juce::Label* _label) override;

		virtual int compareElements(const TreeViewItem* _a, const TreeViewItem* _b);

	protected:
		void search(pluginLib::patchDB::SearchRequest&& _request);
		virtual void processSearchUpdated(const pluginLib::patchDB::Search& _search);

	private:
		bool mightContainSubItems() override { return true; }

		void setText(const std::string& _text);
		void updateText();
		void paintItem(juce::Graphics& _g, int _width, int _height) override;

		void destroyEditorLabel();

		PatchManager& m_patchManager;

		std::string m_title;
		uint32_t m_count = g_invalidCount;

		std::string m_text;

		uint32_t m_searchHandle = pluginLib::patchDB::g_invalidSearchHandle;

		FinishedEditingCallback m_finishedEditingCallback;
		juce::Label* m_editorLabel = nullptr;
		std::string m_editorInitialText;
	};
}

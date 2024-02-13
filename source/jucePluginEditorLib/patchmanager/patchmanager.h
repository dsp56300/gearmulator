#pragma once

#include "resizerbar.h"
#include "../../jucePluginLib/patchdb/db.h"

#include "juce_gui_basics/juce_gui_basics.h"

#include "state.h"

namespace jucePluginEditorLib
{
	enum class FileType;
	class Editor;
}

namespace genericUI
{
	class UiObject;
}

namespace jucePluginEditorLib::patchManager
{
	class Status;
	class TreeItem;
	class SearchTree;
	class SearchList;
	class Info;
	class List;
	class Tree;

	class PatchManager : public juce::Component, public pluginLib::patchDB::DB, juce::Timer, public juce::ChangeListener
	{
	public:
		explicit PatchManager(Editor& _editor, Component* _root, const juce::File& _dir);
		~PatchManager() override;

		void timerCallback() override;

		void setSelectedItem(Tree* _tree, const TreeItem* _item);
		void addSelectedItem(Tree* _tree, const TreeItem* _item);
		void removeSelectedItem(Tree* _tree, const TreeItem* _item);

		bool setSelectedPatch(const pluginLib::patchDB::PatchPtr& _patch, pluginLib::patchDB::SearchHandle _fromSearch);

		bool selectPrevPreset(uint32_t _part);
		bool selectNextPreset(uint32_t _part);

		bool selectPatch(uint32_t _part, const pluginLib::patchDB::DataSource& _ds, uint32_t _program);

		void setListStatus(uint32_t _selected, uint32_t _total);

		pluginLib::patchDB::Color getPatchColor(const pluginLib::patchDB::PatchPtr& _patch) const;

		bool addGroupTreeItemForTag(pluginLib::patchDB::TagType _type, const std::string& _name);

		void paint(juce::Graphics& g) override;

		void exportPresets(const juce::File& _file, const std::vector<pluginLib::patchDB::PatchPtr>& _patches, FileType _fileType) const;
		bool exportPresets(std::vector<pluginLib::patchDB::PatchPtr>&& _patches, FileType _fileType) const;

		void resized() override;

		juce::Colour getResizerBarColor() const;

		bool copyPart(uint8_t _target, uint8_t _source);

		bool setSelectedPatch(uint32_t _part, const pluginLib::patchDB::PatchPtr& _patch, pluginLib::patchDB::SearchHandle _fromSearch);

	private:
		bool selectPatch(uint32_t _part, int _offset);

		bool setSelectedPatch(uint32_t _part, const pluginLib::patchDB::PatchPtr& _patch);
		bool setSelectedPatch(uint32_t _part, const pluginLib::patchDB::PatchKey& _patch);

	public:
		auto& getEditor() const { return m_editor; }
		std::shared_ptr<genericUI::UiObject> getTemplate(const std::string& _name) const;

		virtual uint32_t getCurrentPart() const = 0;
		virtual bool activatePatch(const pluginLib::patchDB::PatchPtr& _patch) = 0;
		virtual bool activatePatch(const pluginLib::patchDB::PatchPtr& _patch, uint32_t _part) = 0;

		void onLoadFinished() override;

		void setPerInstanceConfig(const std::vector<uint8_t>& _data);
		void getPerInstanceConfig(std::vector<uint8_t>& _data);

		void onProgramChanged(uint32_t _part);

		void setCurrentPart(uint32_t _part);

	protected:
		void updateStateAsync(uint32_t _part, const pluginLib::patchDB::PatchPtr& _patch);

	private:
		pluginLib::patchDB::SearchHandle getSearchHandle(const pluginLib::patchDB::DataSource& _ds, bool _selectTreeItem);
		void onSelectedItemsChanged();

		void changeListenerCallback (juce::ChangeBroadcaster* _source) override;

		Editor& m_editor;

		Tree* m_treeDS = nullptr;
		Tree* m_treeTags = nullptr;
		List* m_list = nullptr;
		Info* m_info = nullptr;

		SearchTree* m_searchTreeDS = nullptr;
		SearchTree* m_searchTreeTags = nullptr;
		SearchList* m_searchList = nullptr;
		Status* m_status = nullptr;

		State m_state;

		std::map<Tree*, std::set<const TreeItem*>> m_selectedItems;

		juce::StretchableLayoutManager m_stretchableManager;

		ResizerBar m_resizerBarA{*this, &m_stretchableManager, 1};
		ResizerBar m_resizerBarB{*this, &m_stretchableManager, 3};
		ResizerBar m_resizerBarC{*this, &m_stretchableManager, 5};
	};
}

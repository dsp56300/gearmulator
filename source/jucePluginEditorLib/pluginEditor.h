#pragma once

#include "imagePool.h"
#include "parameterOverlays.h"
#include "skin.h"

#include "juceUiLib/editor.h"

#include "synthLib/buildconfig.h"

#include "baseLib/event.h"

#include "jucePluginLib/types.h"

namespace baseLib
{
	class ChunkReader;
	class BinaryStream;
	class ChunkWriter;
}

namespace pluginLib
{
	class FileType;
	class ParameterBinding;
}

namespace jucePluginEditorLib
{
	namespace patchManager
	{
		class PatchManager;
	}

	class Processor;

	class Editor : public genericUI::Editor, genericUI::EditorInterface
	{
	public:
		baseLib::Event<Editor*, juce::MouseEvent*> onOpenMenu;

		Editor(Processor& _processor, pluginLib::ParameterBinding& _binding, Skin _skin);
		~Editor() override;

		Editor(const Editor&) = delete;
		Editor(Editor&&) = delete;
		Editor& operator = (const Editor&) = delete;
		Editor& operator = (Editor&&) = delete;
		
		void create();

		const char* findResourceByFilename(const std::string& _filename, uint32_t& _size) const;

		void loadPreset(const std::function<void(const juce::File&)>& _callback);
		void savePreset(const pluginLib::FileType& _fileType, const std::function<void(const juce::File&)>& _callback);
#if !SYNTHLIB_DEMO_MODE
		static bool savePresets(const pluginLib::FileType& _type, const std::string& _pathName, const std::vector<std::vector<uint8_t>>& _presets);
#endif
		static std::string createValidFilename(pluginLib::FileType& _type, const juce::File& _file);

		virtual std::pair<std::string, std::string> getDemoRestrictionText() const = 0;

		void showDemoRestrictionMessageBox() const;

		Processor& getProcessor() const { return m_processor; }

		void setPatchManager(patchManager::PatchManager* _patchManager);

		patchManager::PatchManager* getPatchManager() const
		{
			return m_patchManager.get();
		}

		void setPerInstanceConfig(const std::vector<uint8_t>& _data) override;
		void getPerInstanceConfig(std::vector<uint8_t>& _data) override;

		virtual void saveChunkData(baseLib::BinaryStream& _s);
		virtual void loadChunkData(baseLib::ChunkReader& _cr);

		void setCurrentPart(uint8_t _part) override;

		void showDisclaimer() const;

		bool shouldDropFilesWhenDraggedExternally(const juce::DragAndDropTarget::SourceDetails& sourceDetails, juce::StringArray& files, bool& canMoveFiles) override;

		void copyCurrentPatchToClipboard() const;
		bool replaceCurrentPatchFromClipboard() const;

		virtual void openMenu(juce::MouseEvent* _event);
		virtual bool openContextMenuForParameter(const juce::MouseEvent* _event);

		bool copyRegionToClipboard(const std::string& _regionId) const;
		bool copyParametersToClipboard(const std::vector<std::string>& _params, const std::string& _regionId = {}) const;
		bool setParameters(const std::map<std::string, pluginLib::ParamValue>& _paramValues) const;

		auto& getImagePool() { return m_imagePool; }

		void parentHierarchyChanged() override;

		juce::PopupMenu createExportFileTypeMenu(const std::function<void(pluginLib::FileType)>& _func) const;
		virtual void createExportFileTypeMenu(juce::PopupMenu& _menu, const std::function<void(pluginLib::FileType)>& _func) const;

	protected:
		bool keyPressed(const juce::KeyPress& _key) override;

	private:
		void onDisclaimerFinished() const;

		const char* getResourceByFilename(const std::string& _name, uint32_t& _dataSize) override;
		int getParameterIndexByName(const std::string& _name) override;
		bool bindParameter(juce::Button& _target, int _parameterIndex) override;
		bool bindParameter(juce::ComboBox& _target, int _parameterIndex) override;
		bool bindParameter(juce::Slider& _target, int _parameterIndex) override;
		bool bindParameter(juce::Label& _target, int _parameterIndex) override;
		juce::Value* getParameterValue(int _parameterIndex, uint8_t _part) override;

		Processor& m_processor;
		pluginLib::ParameterBinding& m_binding;

		const Skin m_skin;

		std::map<std::string, std::vector<char>> m_fileCache;

		std::unique_ptr<juce::FileChooser> m_fileChooser;
		std::unique_ptr<patchManager::PatchManager> m_patchManager;
		std::vector<uint8_t> m_patchManagerConfig;
		std::vector<std::shared_ptr<juce::TemporaryFile>> m_dragAndDropTempFiles;
		std::vector<juce::File> m_dragAndDropFiles;
		ImagePool m_imagePool;
		ParameterOverlays m_overlays;
	};
}

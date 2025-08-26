#pragma once

#include "parameterOverlays.h"
#include "skin.h"

#include "juceUiLib/editor.h"

#include "synthLib/buildconfig.h"

#include "baseLib/event.h"

#include "jucePluginLib/types.h"

#include "juceRmlUi/rmlDataProvider.h"
#include "juceRmlUi/rmlHelper.h"
#include "juceRmlUi/rmlInterfaces.h"
#include "juceRmlUi/rmlMenu.h"

namespace juce
{
	class FileChooser;
	class TemporaryFile;
	class File;
}

namespace rmlPlugin
{
	namespace skinConverter
	{
		struct SkinConverterOptions;
	}

	class RmlPlugin;
	class RmlParameterBinding;
	class RmlPluginDocument;
}

namespace Rml
{
	class Context;
	class Element;
	class ElementDocument;
}

namespace juceRmlUi
{
	class RmlComponent;
}

namespace baseLib
{
	class ChunkReader;
	class BinaryStream;
	class ChunkWriter;
}

namespace pluginLib
{
	class FileType;
}

namespace jucePluginEditorLib
{
	class PluginDataModel;

	namespace patchManagerRml
	{
		class PatchManagerDataModel;
	}

	namespace patchManager
	{
		class PatchManager;
	}

	class Processor;

	class Editor : public genericUI::Editor, juceRmlUi::DataProvider
	{
	public:
		baseLib::Event<Editor*, Rml::Event&> onOpenMenu;

		Editor(Processor& _processor, Skin _skin);
		~Editor() override;

		Editor(const Editor&) = delete;
		Editor(Editor&&) = delete;
		Editor& operator = (const Editor&) = delete;
		Editor& operator = (Editor&&) = delete;
		
		void create();

		virtual void initSkinConverterOptions(rmlPlugin::skinConverter::SkinConverterOptions&) {}
		virtual void initPluginDataModel(PluginDataModel& _model);

		static void setEnabled(Rml::Element* _element, bool _enabled);

		bool selectTabWithElement(const Rml::Element* _element) const;

		virtual patchManager::PatchManager* createPatchManager(juceRmlUi::RmlComponent& _rmlCompnent, Rml::Element* _parent) { return nullptr; }

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

		virtual void setCurrentPart(uint8_t _part);

		void showDisclaimer() const;

		void copyCurrentPatchToClipboard() const;
		bool replaceCurrentPatchFromClipboard() const;

		virtual void openMenu(Rml::Event& _event);
		virtual bool openContextMenuForParameter(const Rml::Event& _event);

		bool copyRegionToClipboard(const std::string& _regionId) const;
		bool copyParametersToClipboard(const std::vector<std::string>& _params, const std::string& _regionId = {}) const;
		bool setParameters(const std::map<std::string, pluginLib::ParamValue>& _paramValues) const;

		juceRmlUi::Menu createExportFileTypeMenu(const std::function<void(pluginLib::FileType)>& _func) const;
		virtual void createExportFileTypeMenu(juceRmlUi::Menu& _menu, const std::function<void(pluginLib::FileType)>& _func) const;

		juce::Component* createRmlUiComponent(const std::string& _rmlFile);

		virtual void onRmlContextCreated(juceRmlUi::RmlComponent& _rmlComponent, Rml::Context& _context);

		const auto& getPatchManagerDataModel() const { return m_patchManagerDataModel; }
		auto& getPluginDataModel() { return m_pluginDataModel; }

		void registerDragAndDropFile(const juce::File& _file)
		{
			m_dragAndDropFiles.push_back(_file);
		}

		void registerDragAndDropTempFile(std::shared_ptr<juce::TemporaryFile>&& _tempFile)
		{
			m_dragAndDropTempFiles.push_back(std::move(_tempFile));
		}

		rmlPlugin::RmlPlugin* getRmlPlugin() const { return m_rmlPlugin.get(); }
		juceRmlUi::RmlComponent* getRmlComponent() const { return m_rmlComponent.get(); }
		rmlPlugin::RmlParameterBinding* getRmlParameterBinding() const;
		rmlPlugin::RmlPluginDocument* getRmlPluginDocument() const;
		Rml::ElementDocument* getDocument() const;

		Rml::Element* getRmlRootElement() const;

		template<typename T = Rml::Element> T* findChild(const std::string& _name, const bool _mustExist = true) const
		{
			auto* root = getRmlRootElement();
			return juceRmlUi::helper::findChildT<T>(root, _name, _mustExist);
		}

		const auto& getSkin() const { return m_skin; }

		int getDefaultWidth() const;
		int getDefaultHeight() const;

		bool setSize(int _width, int _height) const;

	private:
		void onDisclaimerFinished() const;

		const char* getResourceByFilename(const std::string& _name, uint32_t& _dataSize) override;

		std::vector<std::string> getAllFilenames() override;

		std::string getAbsoluteSkinFolder(const std::string& _skinFolder) const;

		Processor& m_processor;

		Skin m_skin;

		std::map<std::string, std::vector<char>> m_fileCache;

		std::unique_ptr<juce::FileChooser> m_fileChooser;
		std::unique_ptr<patchManager::PatchManager> m_patchManager;
		std::unique_ptr<patchManagerRml::PatchManagerDataModel> m_patchManagerDataModel;
		std::unique_ptr<PluginDataModel> m_pluginDataModel;
		std::vector<uint8_t> m_patchManagerConfig;
		std::vector<std::shared_ptr<juce::TemporaryFile>> m_dragAndDropTempFiles;
		std::vector<juce::File> m_dragAndDropFiles;
		std::unique_ptr<ParameterOverlays> m_overlays;

		juceRmlUi::RmlInterfaces m_rmlInterfaces;
		std::unique_ptr<juceRmlUi::RmlComponent> m_rmlComponent;

		std::unique_ptr<rmlPlugin::RmlPlugin> m_rmlPlugin;
	};
}

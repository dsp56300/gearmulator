#include "patchmanager.h"

#include "patchmanagerui.h"

#include "../pluginEditor.h"
#include "../pluginProcessor.h"

#include "jucePluginLib/clipboard.h"
#include "jucePluginLib/filetype.h"
#include "jucePluginLib/types.h"

#include "jucePluginEditorLib/patchmanagerUiRml/patchmanagerUiRml.h"

#include "juceRmlUi/rmlInterfaces.h"
#include "juceRmlUi/rmlMenu.h"

#include "juceUiLib/messageBox.h"

namespace jucePluginEditorLib::patchManager
{
	PatchManager::PatchManager(Processor& _processor, const std::initializer_list<GroupType>& _groupTypes/* = DefaultGroupTypes*/)
	: pluginLib::patchManager::PatchManager(juce::File(_processor.getPatchManagerDataFolder(false)))
	, m_processor(_processor)
	, m_groupTypes(_groupTypes)
	{
		setTagTypeName(pluginLib::patchDB::TagType::Category, "Category");
		setTagTypeName(pluginLib::patchDB::TagType::Tag, "Tag");
		setTagTypeName(pluginLib::patchDB::TagType::Favourites, "Favourite");

		connectProgramChangeRouter(_processor.getProgramChangeRouter(), [&_processor](const synthLib::SMidiEvent& _ev)
		{
			_processor.getPlugin().addMidiEvent(_ev);
		});
	}

	PatchManager::~PatchManager()
	{
		detachUi();
	}

	void PatchManager::attachUi(Editor& _editor, Rml::Element* _root)
	{
		detachUi();

		m_editor = &_editor;

		m_ui = std::make_unique<patchManagerRml::PatchManagerUiRml>(_editor, *this, *_editor.getRmlComponent(), _root, *_editor.getPatchManagerDataModel(), m_groupTypes);

		for (const auto& [type, name] : m_tagGroups)
			m_ui->createTag(toGroupType(type), name);

		setUi(m_ui.get());

		// the database may have finished loading long ago, show everything it has
		pluginLib::patchDB::Dirty dirty;
		dirty.dataSources = true;
		dirty.patches = true;
		for (auto t = static_cast<int>(pluginLib::patchDB::TagType::Invalid) + 1; t < static_cast<int>(pluginLib::patchDB::TagType::Count); ++t)
			dirty.tags.insert(static_cast<pluginLib::patchDB::TagType>(t));
		m_ui->processDirty(dirty);

		setCurrentPart(getCurrentPart());
	}

	void PatchManager::detachUi()
	{
		if (!m_ui)
			return;

		setUi(nullptr);
		m_ui.reset();
		m_editor = nullptr;
	}

	void PatchManager::processPending()
	{
		if (!m_editor)
		{
			pluginLib::patchManager::PatchManager::processPending();
			return;
		}

		const juceRmlUi::RmlInterfaces::ScopedAccess access(*m_editor->getRmlComponent());
		pluginLib::patchManager::PatchManager::processPending();
	}

	void PatchManager::onErrors(const std::vector<std::string>& _errors) const
	{
		pluginLib::patchManager::PatchManager::onErrors(_errors);

		if (!m_editor)
			return;

		std::string msg = "Patch Manager encountered errors:\n\n";
		for(size_t i=0; i<_errors.size(); ++i)
		{
			msg += _errors[i];
			if(i < _errors.size() - 1)
				msg += "\n";
		}

		genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning, "Patch Manager Error", msg);
	}

	bool PatchManager::canLoadProgramChangeDirectly(const uint32_t _part) const
	{
		// activatePatch() sends the locked parameters again after the patch, so they keep their values
		return !m_processor.getController().getParameterLocking().hasLockedRegions(static_cast<uint8_t>(_part));
	}

	uint32_t PatchManager::getCurrentPart() const
	{
		return m_processor.getController().getCurrentPart();
	}

	uint32_t PatchManager::getPartCount() const
	{
		return std::min(m_processor.getController().getPartCount(), static_cast<uint8_t>(getState().getPartCount()));
	}

	uint32_t PatchManager::createSaveMenuEntries(juceRmlUi::Menu& _menu, uint32_t _part, const std::string& _name/* = "patch"*/, uint64_t _userData/* = 0*/)
	{
		const auto& state = getState();
		const auto key = state.getPatch(_part);

		uint32_t countAdded = 0;

		if(key.isValid() && key.source->type == pluginLib::patchDB::SourceType::LocalStorage)
		{
			// the key that is stored in the state might not contain patches, find the real data source in the DB
			const auto ds = getDataSource(*key.source);

			if(ds)
			{
				if(const auto p = ds->getPatch(key))
				{
					if(*p == key)
					{
						++countAdded;
						_menu.addEntry("Overwrite '" + p->getName() + "' in user bank '" + ds->name + "' with " + _name, true, false, [this, p, _part, _userData]
						{
							const auto newPatch = requestPatchForPart(_part, _userData);
							if(newPatch)
							{
								replacePatch(p, newPatch);
								getMutableState().setSelectedPatch(_part, pluginLib::patchDB::PatchKey(*p), getState().getSearchHandle(_part));
							}
						});
					}
				}
			}
		}

		const auto existingLocalDS = getDataSourcesOfSourceType(pluginLib::patchDB::SourceType::LocalStorage);

		if(!existingLocalDS.empty())
		{
			if(countAdded)
				_menu.addSeparator();

			for (const auto& ds : existingLocalDS)
			{
				++countAdded;
				_menu.addEntry("Add " + _name + " to user bank '" + ds->name + "'", true, false, [this, ds, _part, _userData]
				{
					const auto newPatch = requestPatchForPart(_part, _userData);

					if(!newPatch)
						return;

					copyPatchesToLocalStorage(ds, {newPatch}, static_cast<int>(_part));
				});
			}
		}
		else
		{
			++countAdded;
			_menu.addEntry("Create new user bank and add " + _name, true, false, [this, _part, _userData]
			{
				const auto newPatch = requestPatchForPart(_part, _userData);

				if(!newPatch)
					return;

				pluginLib::patchDB::DataSource ds;

				ds.name = "User Bank";
				ds.type = pluginLib::patchDB::SourceType::LocalStorage;
				ds.origin = pluginLib::patchDB::DataSourceOrigin::Manual;
				ds.timestamp = std::chrono::system_clock::now();
				addDataSource(ds, false, [newPatch, _part, this](const bool _success, const std::shared_ptr<pluginLib::patchDB::DataSourceNode>& _ds)
				{
					if(_success)
						copyPatchesToLocalStorage(_ds, {newPatch}, static_cast<int>(_part));
				});
			});
		}

		return countAdded;
	}

	bool PatchManager::addGroupTreeItemForTag(const pluginLib::patchDB::TagType _type)
	{
		return addGroupTreeItemForTag(_type, getTagTypeName(_type));
	}

	bool PatchManager::addGroupTreeItemForTag(const pluginLib::patchDB::TagType _type, const std::string& _name)
	{
		if(toGroupType(_type) == GroupType::Invalid)
			return false;
		if(_name.empty())
			return false;

		m_tagGroups.emplace_back(_type, _name);

		if (m_ui)
			return m_ui->createTag(toGroupType(_type), _name);

		return true;
	}

	void PatchManager::exportPresets(const juce::File& _file, const std::vector<pluginLib::patchDB::PatchPtr>& _patches, const pluginLib::FileType& _fileType) const
	{
#if SYNTHLIB_DEMO_MODE
		if (m_editor)
			m_editor->showDemoRestrictionMessageBox();
#else
		pluginLib::FileType type = _fileType;
		const auto name = Editor::createValidFilename(type, _file);

		std::vector<pluginLib::patchDB::Data> patchData;

		uint32_t index = 0;

		for (const auto& patch : _patches)
		{
			// create consecutive program numbers for exported patches
			auto p = std::make_shared<pluginLib::patchDB::Patch>();

			p->replaceData(*patch);

			p->modifications = patch->modifications;

			p->bank = index >> 7;
			p->program = index & 0x7Ff;

			++index;

			const auto patchSysex = applyModifications(p, type, pluginLib::ExportType::File);

			if(!patchSysex.empty())
				patchData.push_back(patchSysex);
		}

		if(!Editor::savePresets(type, name, patchData))
			genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning, "Save failed", "Failed to write data to " + _file.getFullPathName().toStdString());
#endif
	}

	bool PatchManager::exportPresets(std::vector<pluginLib::patchDB::PatchPtr>&& _patches, const pluginLib::FileType& _fileType) const
	{
		if (!m_editor)
			return false;

		const auto patchCount = _patches.size();

		auto exportPatches = [p = std::move(_patches), this, _fileType]
		{
			if (!m_editor)
				return;

			auto patches = p;
			pluginLib::patchManager::sortPatches(patches, pluginLib::patchDB::SourceType::LocalStorage);
			m_editor->savePreset(_fileType, [this, p = std::move(patches), _fileType](const juce::File& _file)
			{
				exportPresets(_file, p, _fileType);
			});
		};

		if(patchCount > 128)
		{
			genericUI::MessageBox::showOkCancel(
				genericUI::MessageBox::Icon::Warning,
				"Patch Manager",
				"You are trying to export more than 128 presets into a single file. Note that this dump exceeds the size of one bank and may not be compatible with your hardware",
				[exportPatches](const genericUI::MessageBox::Result _result)
				{
					if (_result != genericUI::MessageBox::Result::Ok)
						return;

					exportPatches();
				});
		}
		else
		{
			exportPatches();
		}

		return true;
	}

	std::vector<pluginLib::patchDB::PatchPtr> PatchManager::loadPatchesFromFiles(const juce::StringArray& _files)
	{
		std::vector<std::string> files;

		for (const auto& file : _files)
			files.push_back(file.toStdString());

		return loadPatchesFromFiles(files);
	}

	void PatchManager::setCustomSearch(const pluginLib::patchDB::SearchHandle _sh) const
	{
		if (m_ui)
			m_ui->setCustomSearch(_sh);
	}

	void PatchManager::bringToFront() const
	{
		if (m_ui)
			m_ui->bringToFront();
	}

	void PatchManager::startLoaderThread(const juce::File& _migrateFromDir/* = {}*/)
	{
		if(_migrateFromDir.getFullPathName().isEmpty())
		{
			const auto& configOptions = m_processor.getConfigOptions();

			// Without options there is no old patch database. Their default file would point the migration at a
			// folder outside the product's own, it deletes *.cache files there and takes over *.json and *.syx
			if(configOptions.applicationName.isEmpty())
				return DB::startLoaderThread();

			DB::startLoaderThread(configOptions.getDefaultFile().getParentDirectory());
			return;
		}
		DB::startLoaderThread(_migrateFromDir);
	}

	std::vector<pluginLib::patchDB::PatchPtr> PatchManager::getPatchesFromString(const std::string& _text)
	{
		auto data = pluginLib::Clipboard::getDataFromString(m_processor, _text);

		if(data.sysex.empty())
			return {};

		pluginLib::patchDB::DataList results;

		const pluginLib::patchDB::Data sysexData(data.sysex.begin(), data.sysex.end());
		if (!parseFileData(results, sysexData, {}))
			return {};

		std::vector<pluginLib::patchDB::PatchPtr> patches;

		for (auto& result : results)
		{
			if(const auto patch = createPatch(std::move(result), {}))
				patches.push_back(patch);
		}

		return patches;
	}

	std::vector<pluginLib::patchDB::PatchPtr> PatchManager::getPatchesFromClipboard()
	{
		return getPatchesFromString(juce::SystemClipboard::getTextFromClipboard().toStdString());
	}

	bool PatchManager::activatePatchFromString(const std::string& _text)
	{
		const auto patches = getPatchesFromString(_text);

		if(patches.size() != 1)
			return false;

		return activatePatch(patches.front(), getCurrentPart());
	}

	bool PatchManager::activatePatchFromClipboard()
	{
		return activatePatchFromString(juce::SystemClipboard::getTextFromClipboard().toStdString());
	}

	std::string PatchManager::toString(const pluginLib::patchDB::PatchPtr& _patch, const pluginLib::FileType& _fileType, const pluginLib::ExportType _exportType) const
	{
		if (!_patch)
			return {};

		const auto data = applyModifications(_patch, _fileType, _exportType);

		std::vector<uint8_t> sysexVec(data.begin(), data.end());
		return pluginLib::Clipboard::createJsonString(m_processor, {}, {}, sysexVec);
	}
}

#include "pluginEditor.h"

#include "pluginProcessor.h"
#include "skin.h"

#include "baseLib/filesystem.h"

#include "jucePluginLib/clipboard.h"
#include "jucePluginLib/filetype.h"
#include "jucePluginLib/tools.h"

#include "juceRmlUi/juceRmlComponent.h"
#include "juceRmlUi/rmlEventListener.h"

#include "jucePluginData.h"
#include "pluginDataModel.h"
#include "pluginEditorState.h"

#include "juceRmlPlugin/rmlPlugin.h"
#include "juceRmlPlugin/skinConverter/skinConverter.h"

#include "juceUiLib/messageBox.h"

#include "synthLib/os.h"
#include "synthLib/sysexToMidi.h"

#include "patchmanager/patchmanager.h"
#include "patchmanager/savepatchdesc.h"
#include "patchmanagerUiRml/patchmanagerDataModel.h"

#include "RmlUi/Core/ElementDocument.h"

namespace jucePluginEditorLib
{
	namespace
	{
		constexpr Processor::BinaryDataRef g_binaryDefaultData
		{
			jucePluginData::namedResourceListSize,
			jucePluginData::originalFilenames,
			jucePluginData::namedResourceList,
			jucePluginData::getNamedResource
		};
	}
	Editor::Editor(Processor& _processor, Skin _skin)
		: m_processor(_processor)
		, m_skin(std::move(_skin))
		, m_rmlInterfaces(*this)
	{
		showDisclaimer();
	}

	Editor::~Editor()
	{
		m_overlays.reset();

		for (const auto& file : m_dragAndDropFiles)
			file.deleteFile();

		m_patchManager.reset();
		m_rmlPlugin.reset();
		m_rmlComponent.reset();
	}

	void Editor::create()
	{
		if (juce::String(m_skin.filename).endsWithIgnoreCase(".json"))
		{
			genericUI::Editor::create(m_skin.filename);

			const auto newName = baseLib::filesystem::stripExtension(m_skin.filename);

			rmlPlugin::skinConverter::SkinConverterOptions options;
			initSkinConverterOptions(options);

			if(m_skin.folder.empty())
			{
				m_skin.folder = PluginEditorState::getSkinFolder(getProcessor().getDataFolder());
				m_skin.folder = PluginEditorState::getSkinSubfolder(m_skin, m_skin.folder);
			}

			const auto folder = getAbsoluteSkinFolder(m_skin.folder);

			juce::File(juce::String::fromUTF8(folder.c_str())).createDirectory();

			rmlPlugin::skinConverter::SkinConverter sc(*this, getRootObject(), folder, newName + ".rml", newName + ".rcss", std::move(options));

			m_skin.filename = newName + ".rml";
			m_skin.displayName = PluginEditorState::createSkinDisplayName(m_skin.filename);
		}

		m_rmlComponent.reset(dynamic_cast<juceRmlUi::RmlComponent*>(createRmlUiComponent(m_skin.filename)));

		auto* doc = m_rmlComponent->getDocument();

		if (auto* elem = doc->GetElementById("patchmanager"))
		{
			juceRmlUi::RmlInterfaces::ScopedAccess sa(*m_rmlComponent);
			setPatchManager(createPatchManager(elem));
		}

		initRootScale(doc->GetAttribute("rootScale", 1.0f));

		juceRmlUi::EventListener::Add(doc, Rml::EventId::Mousedown, [this](Rml::Event& _event)
		{
			if (juceRmlUi::helper::getMouseButton(_event) == juceRmlUi::MouseButton::Right)
			{
				_event.StopPropagation();
				openMenu(_event);
			}
		});
	}

	void Editor::initPluginDataModel(PluginDataModel& _model)
	{
		_model.set("currentPart", std::to_string(getProcessor().getController().getCurrentPart()));
	}

	void Editor::setEnabled(Rml::Element* _element, bool _enabled)
	{
		if (!_element)
			return;

		if (const auto opacity = _element->GetAttribute("disabledAlpha", 0.0f); opacity > 0)
		{
			_element->SetProperty(Rml::PropertyId::Opacity, Rml::Property(_enabled ? 1.0f : opacity, Rml::Unit::NUMBER));
			_element->SetProperty(Rml::PropertyId::PointerEvents, _enabled ? Rml::Style::PointerEvents::Auto : Rml::Style::PointerEvents::None);

			juceRmlUi::helper::setVisible(_element, opacity > 0);
		}
		else
		{
			juceRmlUi::helper::setVisible(_element, _enabled);
		}
	}

	bool Editor::selectTabWithElement(const Rml::Element* _element) const
	{
		if (!m_rmlPlugin)
			return false;
		return m_rmlPlugin->selectTabWithElement(_element);
	}

	const char* Editor::findResourceByFilename(const std::string& _filename, uint32_t& _size) const
	{
		auto res = m_processor.findResource(_filename);
		if(!res)
		{
			res = Processor::findResource(g_binaryDefaultData, _filename);
			if (!res)
				return nullptr;
		}
		_size = res->second;
		return res->first;
	}

	void Editor::loadPreset(const std::function<void(const juce::File&)>& _callback)
	{
		const auto path = m_processor.getConfig().getValue("load_path", "");

		m_fileChooser = std::make_unique<juce::FileChooser>(
			"Choose syx/midi banks to import",
			path.isEmpty() ? juce::File::getSpecialLocation(juce::File::currentApplicationFile).getParentDirectory() : path,
			"*.syx,*.mid,*.midi,*.vstpreset,*.fxb,*.cpr", true);

		constexpr auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::FileChooserFlags::canSelectFiles;

		const std::function onFileChosen = [this, _callback](const juce::FileChooser& _chooser)
		{
			if (_chooser.getResults().isEmpty())
				return;

			const auto result = _chooser.getResult();

			m_processor.getConfig().setValue("load_path", result.getParentDirectory().getFullPathName());

			_callback(result);
		};
		m_fileChooser->launchAsync(flags, onFileChosen);
	}

	void Editor::savePreset(const pluginLib::FileType& _fileType, const std::function<void(const juce::File&)>& _callback)
	{
#if !SYNTHLIB_DEMO_MODE
		const auto path = m_processor.getConfig().getValue("save_path", "");

		m_fileChooser = std::make_unique<juce::FileChooser>(
			"Save preset(s) as " + _fileType.type(),
			path.isEmpty() ? juce::File::getSpecialLocation(juce::File::currentApplicationFile).getParentDirectory() : path,
			"*." + _fileType.type(), true);

		constexpr auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::FileChooserFlags::canSelectFiles;

		auto onFileChosen = [this, _callback](const juce::FileChooser& _chooser)
		{
			if (_chooser.getResults().isEmpty())
				return;

			const auto result = _chooser.getResult();
			m_processor.getConfig().setValue("save_path", result.getParentDirectory().getFullPathName());

			if (!result.existsAsFile())
			{
				_callback(result);
			}
			else
			{
				genericUI::MessageBox::showYesNo(genericUI::MessageBox::Icon::Warning, "File exists", "Do you want to overwrite the existing file?", 
					[this, _callback, result](const genericUI::MessageBox::Result _result)
				{
					if (_result == genericUI::MessageBox::Result::Yes)
						_callback(result);
				});
			}
		};
		m_fileChooser->launchAsync(flags, onFileChosen);
#else
		showDemoRestrictionMessageBox();
#endif
	}

#if !SYNTHLIB_DEMO_MODE
	bool Editor::savePresets(const pluginLib::FileType& _type, const std::string& _pathName, const std::vector<std::vector<uint8_t>>& _presets)
	{
		if (_presets.empty())
			return false;

		if (_type == pluginLib::FileType::Mid)
			return synthLib::SysexToMidi::write(_pathName.c_str(), _presets);

		FILE* hFile = baseLib::filesystem::openFile(_pathName, "wb");

		if (!hFile)
			return false;

		for (const auto& message : _presets)
		{
			const auto written = fwrite(&message.front(), 1, message.size(), hFile);

			if (written != message.size())
			{
				fclose(hFile);
				return false;
			}
		}
		fclose(hFile);
		return true;
	}
#endif

	std::string Editor::createValidFilename(pluginLib::FileType& _type, const juce::File& _file)
	{
		const auto ext = _file.getFileExtension();
		auto file = _file.getFullPathName().toStdString();

		if (ext.endsWithIgnoreCase(_type.type()))
			return file;
		
		if (ext.endsWithIgnoreCase("mid"))
			_type = pluginLib::FileType::Mid;
		else if (ext.endsWithIgnoreCase("syx"))
			_type = pluginLib::FileType::Syx;
		else
			file += "." + _type.type();
		return file;
	}

	void Editor::showDemoRestrictionMessageBox() const
	{
		const auto &[title, msg] = getDemoRestrictionText();
		genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning, title, msg);
	}

	void Editor::setPatchManager(patchManager::PatchManager* _patchManager)
	{
		m_patchManager.reset(_patchManager);

		if(_patchManager && !m_patchManagerConfig.empty())
			m_patchManager->setPerInstanceConfig(m_patchManagerConfig);
	}

	void Editor::setPerInstanceConfig(const std::vector<uint8_t>& _data)
	{
		{
			// test if its an old version that didn't use chunks yet
			pluginLib::PluginStream oldStream(_data);
			const auto version = oldStream.read<uint32_t>();

			if(version == 1)
			{
				m_patchManagerConfig = _data;
				if(m_patchManager)
					m_patchManager->setPerInstanceConfig(_data);
				return;
			}
		}

		baseLib::BinaryStream s(_data);
		baseLib::ChunkReader cr(s);
		loadChunkData(cr);
		cr.read();
	}

	void Editor::loadChunkData(baseLib::ChunkReader& _cr)
	{
		_cr.add("pmDt", 2, [this](baseLib::BinaryStream& _s, uint32_t/* _version*/)
		{
			m_patchManagerConfig.clear();
			_s.read(m_patchManagerConfig);
			if(m_patchManager)
				m_patchManager->setPerInstanceConfig(m_patchManagerConfig);
		});
	}

	void Editor::getPerInstanceConfig(std::vector<uint8_t>& _data)
	{
		baseLib::BinaryStream s;
		saveChunkData(s);
		s.toVector(_data);
	}

	void Editor::saveChunkData(baseLib::BinaryStream& _s)
	{
		if(m_patchManager)
		{
			m_patchManagerConfig.clear();
			m_patchManager->getPerInstanceConfig(m_patchManagerConfig);
		}
		baseLib::ChunkWriter cw(_s, "pmDt", 2);
		_s.write(m_patchManagerConfig);
	}

	void Editor::setCurrentPart(const uint8_t _part)
	{
		getProcessor().getController().setCurrentPart(_part);
		if(m_patchManager)
			m_patchManager->setCurrentPart(_part);

		m_pluginDataModel->set("currentPart", std::to_string(_part));
	}

	void Editor::showDisclaimer() const
	{
		if(pluginLib::Tools::isHeadless())
			return;

		if(!m_processor.getConfig().getBoolValue("disclaimerSeen", false))
		{
			const juce::MessageBoxOptions options = juce::MessageBoxOptions::makeOptionsOk(juce::MessageBoxIconType::WarningIcon, m_processor.getProperties().name,
	           "It is the sole responsibility of the user to operate this emulator within the bounds of all applicable laws.\n\n"

				"Usage of emulators in conjunction with ROM images you are not legally entitled to own is forbidden by copyright law.\n\n"

				"If you are not legally entitled to use this emulator please discontinue usage immediately.\n\n", 

				"I Agree"
			);

			juce::NativeMessageBox::showAsync(options, [this](int)
			{
				m_processor.getConfig().setValue("disclaimerSeen", true);
				onDisclaimerFinished();
			});
		}
		else
		{
			onDisclaimerFinished();
		}
	}

	void Editor::copyCurrentPatchToClipboard() const
	{
		// copy patch of current part to Clipboard
		if(!m_patchManager)
			return;

		const auto p = m_patchManager->requestPatchForPart(m_patchManager->getCurrentPart());

		if(!p)
			return;

		const auto patchAsString = m_patchManager->toString(p, pluginLib::FileType::Empty, pluginLib::ExportType::Clipboard);

		if(!patchAsString.empty())
			juce::SystemClipboard::copyTextToClipboard(patchAsString);
	}

	bool Editor::replaceCurrentPatchFromClipboard() const
	{
		if(!m_patchManager)
			return false;
		return m_patchManager->activatePatchFromClipboard();
	}

	void Editor::openMenu(Rml::Event& _event)
	{
		onOpenMenu(this, _event);
	}

	bool Editor::openContextMenuForParameter(const Rml::Event& _event)
	{
		const auto* param = getRmlParameterBinding()->getParameterForElement(_event.GetTargetElement());
		if(!param)
			return false;

		auto& controller = m_processor.getController();

		const auto& regions = controller.getParameterDescriptions().getRegions();
		const auto paramRegionIds = controller.getRegionIdsForParameter(param);

		if(paramRegionIds.empty())
			return false;

		const auto part = param->getPart();

		juceRmlUi::Menu menu;

		// Lock / Unlock

		for (const auto& regionId : paramRegionIds)
		{
			const auto& regionName = regions.find(regionId)->second.getName();

			const auto isLocked = controller.getParameterLocking().isRegionLocked(part, regionId);

			menu.addEntry(std::string(isLocked ? "Unlock" : "Lock") + std::string(" region '") + regionName + "'", [this, regionId, isLocked, part]
			{
				auto& locking = m_processor.getController().getParameterLocking();
				if(isLocked)
					locking.unlockRegion(part, regionId);
				else
					locking.lockRegion(part, regionId);
			});
		}

		// Copy to clipboard

		menu.addSeparator();

		for (const auto& regionId : paramRegionIds)
		{
			const auto& regionName = regions.find(regionId)->second.getName();

			menu.addEntry(std::string("Copy region '") + regionName + "'", [this, regionId]
			{
				copyRegionToClipboard(regionId);
			});
		}

		// Paste from clipboard

		const auto data = pluginLib::Clipboard::getDataFromString(m_processor, juce::SystemClipboard::getTextFromClipboard().toStdString());

		if(!data.parameterValuesByRegion.empty())
		{
			bool haveSeparator = false;

			for (const auto& paramRegionId : paramRegionIds)
			{
				const auto it = data.parameterValuesByRegion.find(paramRegionId);

				if(it == data.parameterValuesByRegion.end())
					continue;

				// if region is not fully covered, skip it
				const auto& region = regions.find(it->first)->second;
				if(it->second.size() < region.getParams().size())
					continue;

				const auto& parameterValues = it->second;

				if(!haveSeparator)
				{
					menu.addSeparator();
					haveSeparator = true;
				}

				const auto& regionName = regions.find(paramRegionId)->second.getName();

				menu.addEntry("Paste region '" + regionName + "'", [this, parameterValues]
				{
					setParameters(parameterValues);
				});
			}

			menu.addSeparator();

			const auto& desc = param->getDescription();
			const auto& paramName = desc.name;

			const auto itParam = data.parameterValues.find(paramName);

			if(itParam != data.parameterValues.end())
			{
				const auto& paramValue = itParam->second;

				const auto& valueText = desc.valueList.valueToText(paramValue);

				menu.addEntry("Paste value '" + valueText + "' for parameter '" + desc.displayName + "'", [this, paramName, paramValue]
				{
					pluginLib::Clipboard::Data::ParameterValues params;
					params.insert({paramName, paramValue});
					setParameters(params);
				});
			}
		}

		// Parameter links

		juceRmlUi::Menu linkMenu;

		menu.addSeparator();

		for (const auto& regionId : paramRegionIds)
		{
			juceRmlUi::Menu regionMenu;

			const auto currentPart = controller.getCurrentPart();

			for(uint8_t p=0; p<controller.getPartCount(); ++p)
			{
				if(p == currentPart)
					continue;

				const auto isLinked = controller.getParameterLinks().isRegionLinked(regionId, currentPart, p);

				regionMenu.addEntry(std::string("Link Part ") + std::to_string(p+1), isLinked, [this, regionId, isLinked, currentPart, p]
				{
					auto& links = m_processor.getController().getParameterLinks();

					if(isLinked)
						links.unlinkRegion(regionId, currentPart, p);
					else
						links.linkRegion(regionId, currentPart, p, true);
				});
			}

			const auto& regionName = regions.find(regionId)->second.getName();
			linkMenu.addSubMenu("Region '" + regionName + "'", std::move(regionMenu));
		}

		menu.addSubMenu("Parameter Links", std::move(linkMenu));

		auto& midiPackets = m_processor.getController().getParameterDescriptions().getMidiPackets();
		for (const auto& mp : midiPackets)
		{
			auto defIndices = mp.second.getDefinitionIndicesForParameterName(param->getDescription().name);
			if (defIndices.empty())
				continue;

			const auto expectedValue = param->getUnnormalizedValue();

			menu.addSeparator();

			auto findSimilar = [this, defIndices, packet = &mp.second, expectedValue](const int _offsetMin, const int _offsetMax)
			{
				pluginLib::patchDB::SearchRequest sr;

				sr.customCompareFunc = [packet, expectedValue, defIndices, _offsetMin, _offsetMax](const pluginLib::patchDB::Patch& _patch) -> bool
				{
					if (_patch.sysex.empty())
						return false;
					const auto v = packet->getParameterValue(_patch.sysex, defIndices);
					if (v >= expectedValue + _offsetMin && v <= expectedValue + _offsetMax)
						return true;
					return false;
				};

				const auto sh = getPatchManager()->search(std::move(sr));

				if (sh != pluginLib::patchDB::g_invalidSearchHandle)
				{
					getPatchManager()->setCustomSearch(sh);
					getPatchManager()->bringToFront();
				}
			};

			juceRmlUi::Menu subMenu;
			subMenu.addEntry("Exact Match (Value " + param->getCurrentValueAsText().toStdString() + ")", [this, findSimilar]{ findSimilar(0, 0); });
			subMenu.addEntry("-/+ 4", [this, findSimilar]{ findSimilar(-4, 4); });
			subMenu.addEntry("-/+ 12", [this, findSimilar]{ findSimilar(-12, 12); });
			subMenu.addEntry("-/+ 24", [this, findSimilar]{ findSimilar(-24, 24); });

			menu.addSubMenu("Find similar Patches for parameter " + param->getDescription().displayName, std::move(subMenu));

			break;
		}
		menu.runModal(_event);

		return true;
	}

	bool Editor::copyRegionToClipboard(const std::string& _regionId) const
	{
		const auto& regions = m_processor.getController().getParameterDescriptions().getRegions();
		const auto it = regions.find(_regionId);
		if(it == regions.end())
			return false;

		const auto& region = it->second;

		const auto& params = region.getParams();

		std::vector<std::string> paramsList;
		paramsList.reserve(params.size());

		for (const auto& p : params)
			paramsList.push_back(p.first);

		return copyParametersToClipboard(paramsList, _regionId);
	}

	bool Editor::copyParametersToClipboard(const std::vector<std::string>& _params, const std::string& _regionId) const
	{
		const auto result = pluginLib::Clipboard::parametersToString(m_processor, _params, _regionId);

		if(result.empty())
			return false;

		juce::SystemClipboard::copyTextToClipboard(result);

		return true;
	}

	bool Editor::setParameters(const std::map<std::string, pluginLib::ParamValue>& _paramValues) const
	{
		if(_paramValues.empty())
			return false;

		return getProcessor().getController().setParameters(_paramValues, m_processor.getController().getCurrentPart(), pluginLib::Parameter::Origin::Ui);
	}

	juceRmlUi::Menu Editor::createExportFileTypeMenu(const std::function<void(pluginLib::FileType)>& _func) const
	{
		juceRmlUi::Menu menu;
		createExportFileTypeMenu(menu, _func);
		return menu;
	}

	void Editor::createExportFileTypeMenu(juceRmlUi::Menu& _menu, const std::function<void(pluginLib::FileType)>& _func) const
	{
		_menu.addEntry(".syx", [this, _func]{_func(pluginLib::FileType::Syx);});
		_menu.addEntry(".mid", [this, _func]{_func(pluginLib::FileType::Mid);});
	}

	juce::Component* Editor::createRmlUiComponent(const std::string& _rmlFile)
	{
		if (!m_rmlPlugin)
			m_rmlPlugin.reset(new rmlPlugin::RmlPlugin(m_rmlInterfaces.getCoreInstance(), getProcessor().getController()));

		auto* comp = new juceRmlUi::RmlComponent(m_rmlInterfaces, *this, _rmlFile, 1.0f
			, [this](juceRmlUi::RmlComponent& _rmlComponent, Rml::Context& _context)
			{
				onRmlContextCreated(_rmlComponent, _context);
			}, [this](juceRmlUi::RmlComponent& _rmlComponent, Rml::Context& _context)
			{
				onRmlDocumentLoadFailed(_rmlComponent, _context);
			});

		auto* doc = comp->getDocument();

		juceRmlUi::EventListener::Add(doc, Rml::EventId::Keydown, [this](const Rml::Event& _event)
		{
			if (!juceRmlUi::helper::getKeyModCommand(_event))
				return;

			switch (juceRmlUi::helper::getKeyIdentifier(_event))
			{
			case Rml::Input::KI_C:
				copyCurrentPatchToClipboard();
				break;
			case Rml::Input::KI_V:
				replaceCurrentPatchFromClipboard();
				break;
			default:;
			}
		});
		return comp;
	}

	void Editor::onRmlContextCreated(juceRmlUi::RmlComponent& _rmlComponent, Rml::Context& _context)
	{
		m_rmlPlugin->onContextCreate(&_context, _rmlComponent);

		m_overlays.reset(new ParameterOverlays(*this, *m_rmlPlugin->getParameterBinding(&_context)));

		m_pluginDataModel.reset(new PluginDataModel(*this, _context, [this](PluginDataModel& _model)
		{
			initPluginDataModel(_model);
		}));
		m_patchManagerDataModel.reset(new patchManagerRml::PatchManagerDataModel(_context));
	}

	void Editor::onRmlDocumentLoadFailed(juceRmlUi::RmlComponent& _rmlComponent, Rml::Context& _context)
	{
		m_patchManagerDataModel.reset();
		m_pluginDataModel.reset();
		m_overlays.reset();
	}

	juce::File Editor::createTempFile(const std::string& _filename)
	{
		// try to create human-readable filename first
		const auto pathName = juce::File::getSpecialLocation(juce::File::tempDirectory).getFullPathName().toStdString() + "/" + _filename;

		auto file = juce::File(pathName);

		if(file.hasWriteAccess())
		{
			registerDragAndDropFile(file);
			return file;
		}

		// failed, create temp file
		auto tempFile = std::make_shared<juce::TemporaryFile>(_filename);
		file = tempFile->getFile();
		registerDragAndDropTempFile(std::move(tempFile));
		return file;
	}

	void Editor::registerDragAndDropFile(const juce::File& _file)
	{
		m_dragAndDropFiles.push_back(_file);
	}

	void Editor::registerDragAndDropTempFile(std::shared_ptr<juce::TemporaryFile>&& _tempFile)
	{
		m_dragAndDropTempFiles.push_back(std::move(_tempFile));
	}

	rmlPlugin::RmlParameterBinding* Editor::getRmlParameterBinding() const
	{
		return m_rmlPlugin->getParameterBinding(m_rmlComponent->getContext());
	}

	rmlPlugin::RmlPluginDocument* Editor::getRmlPluginDocument() const
	{
		return m_rmlPlugin ? m_rmlPlugin->getPluginDocument(m_rmlComponent->getDocument()) : nullptr;
	}

	Rml::ElementDocument* Editor::getDocument() const
	{
		return m_rmlComponent ? m_rmlComponent->getDocument() : nullptr;
	}

	Rml::Element* Editor::getRmlRootElement() const
	{
		if (!m_rmlComponent)
			return {};
		return m_rmlComponent->getDocument();
	}

	std::vector<Rml::Element*> Editor::findChildreByParam(const std::string& _param, uint8_t _part,	const size_t _expectedCount, bool _visibleOnly) const
	{
		std::vector<Rml::Element*> results;
		getRmlParameterBinding()->getElementsForParameter(results, _param, _part, _visibleOnly);
		if (_expectedCount != 0 && results.size() != _expectedCount)
			throw std::runtime_error("Failed to find " + std::to_string(_expectedCount) + " elements for parameter " + _param + ", found " + std::to_string(results.size()));
		return results;
	}

	Rml::Element* Editor::findChildByParam(const std::string& _param, uint8_t _part, bool _mustExist, bool _visibleOnly) const
	{
		auto* res = getRmlParameterBinding()->getElementForParameter(_param, _part, _visibleOnly);
		if (_mustExist && !res)
			throw std::runtime_error("Failed to find element for parameter " + _param);
		return res;
	}

	Rml::Element* Editor::addClick(const std::string& _elementName, const std::function<void(Rml::Event&)>& _func, const bool _mustExist) const
	{
		if (auto* element = findChild(_elementName, _mustExist))
		{
			juceRmlUi::EventListener::Add(element, Rml::EventId::Click, [this, _func](Rml::Event& _event)
			{
				_func(_event);
			});
			return element;
		}
		return {};
	}

	int Editor::getDefaultWidth() const
	{
		return m_rmlComponent ? m_rmlComponent->getDocumentSize().x : 0;
	}

	int Editor::getDefaultHeight() const
	{
		return m_rmlComponent ? m_rmlComponent->getDocumentSize().y : 0;
	}

	bool Editor::setSize(const int _width, const int _height) const
	{
		if (!m_rmlComponent)
			return false;
		m_rmlComponent->resize(_width, _height);
		return true;
	}

	std::string Editor::getAbsoluteSkinFolder(const Processor& _processor, const std::string& _skinFolder)
	{
		const auto modulePath = synthLib::getModulePath();
		const auto publicDataPath = _processor.getDataFolder();

		return baseLib::filesystem::validatePath(_skinFolder.find(modulePath) == 0 || _skinFolder.find(publicDataPath) == 0 ? _skinFolder : modulePath + _skinFolder);
	}

	void Editor::onDisclaimerFinished() const
	{
		if(!synthLib::isRunningUnderRosetta())
			return;

		const auto& name = m_processor.getProperties().name;

		genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning,
			name + " - Rosetta detected", 
			name + " appears to be running in Rosetta mode.\n"
			"\n"
			"The DSP emulation core will perform much worse when being executed under Rosetta. We strongly recommend to run your DAW as a native Apple Silicon application");
	}

	const char* Editor::getResourceByFilename(const std::string& _name, uint32_t& _dataSize)
	{
		if(!m_skin.folder.empty())
		{
			auto readFromCache = [this, &_name, &_dataSize]()
			{
				const auto it = m_fileCache.find(_name);
				if(it == m_fileCache.end())
				{
					_dataSize = 0;
					return static_cast<char*>(nullptr);
				}
				_dataSize = static_cast<uint32_t>(it->second.size());
				return &it->second.front();
			};

			const auto* res = readFromCache();

			if(res)
				return res;

			const auto folder = getAbsoluteSkinFolder(m_skin.folder);

			// try to load from disk first
			FILE* hFile = baseLib::filesystem::openFile(folder + _name, "rb");
			if(hFile)
			{
				fseek(hFile, 0, SEEK_END);
				_dataSize = ftell(hFile);
				fseek(hFile, 0, SEEK_SET);

				std::vector<char> data;
				data.resize(_dataSize);
				const auto readCount = fread(&data.front(), 1, _dataSize, hFile);
				fclose(hFile);

				if(readCount == _dataSize)
					m_fileCache.insert(std::make_pair(_name, std::move(data)));

				res = readFromCache();

				if(res)
					return res;
			}
		}

		uint32_t size = 0;
		const auto res = findResourceByFilename(_name, size);
		if(!res)
			throw std::runtime_error("Failed to find file named " + _name);
		_dataSize = size;
		return res;
	}

	std::vector<std::string> Editor::getAllFilenames()
	{
		std::vector<std::string> filenames;

		auto addFile = [&filenames](const std::string& _file)
		{
			// we can't use a set here, because we want to keep the order
			if (std::find(filenames.begin(), filenames.end(), _file) == filenames.end())
				filenames.push_back(_file);
		};

		if (!m_skin.folder.empty())
		{
			const auto folder = getAbsoluteSkinFolder(m_skin.folder);

			juce::File skinFolder(folder);
			if (skinFolder.exists() && skinFolder.isDirectory())
			{
				auto files = skinFolder.findChildFiles(juce::File::findFiles, false, "*");
				for (const auto& file : files)
					addFile(file.getFileName().toStdString());
			}
		}
		else
		{
			auto data = getProcessor().getProperties().binaryData;
			for (size_t i=0; i<data.listSize; ++i)
				addFile(data.originalFileNames[i]);
		}

		for (const auto& file : m_skin.files)
			addFile(file);

		constexpr auto data = g_binaryDefaultData;

		for (size_t i=0; i<data.listSize; ++i)
			addFile(data.originalFileNames[i]);

		return filenames;
	}

	std::string Editor::getAbsoluteSkinFolder(const std::string& _skinFolder) const
	{
		return getAbsoluteSkinFolder(m_processor, _skinFolder);
	}
}

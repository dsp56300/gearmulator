#include "jeEditor.h"

#include "jeAssign.h"
#include "jeController.h"
#include "jeFocusedParameter.h"
#include "jeLcd.h"
#include "jePartButton.h"
#include "jePartSelect.h"
#include "jePatchManager.h"
#include "jePluginProcessor.h"

#include "baseLib/filesystem.h"

#include "jucePluginEditorLib/focusedParameter.h"
#include "jucePluginEditorLib/midiPorts.h"
#include "jucePluginEditorLib/pluginDataModel.h"
#include "jucePluginEditorLib/pluginProcessor.h"

#include "juceRmlUi/rmlElemComboBox.h"

#include <mutex>
#include <set>

namespace jeJucePlugin
{
	namespace
	{
		// guards the ROM change callbacks that are queued to the message thread
		std::mutex& instancesMutex() { static std::mutex mutex; return mutex; }
		std::set<Editor*>& instances() { static std::set<Editor*> set; return set; }

		const char* getDeviceModelName(const jeLib::Rom& _rom)
		{
			return _rom.getDeviceType() == jeLib::DeviceType::Keyboard ? "keyboard" : "rack";
		}
	}

	Editor::Editor(jucePluginEditorLib::Processor& _processor, const jucePluginEditorLib::Skin& _skin)
		: jucePluginEditorLib::Editor(_processor, _skin)
		, m_controller(dynamic_cast<Controller&>(_processor.getController()))
	{
		std::lock_guard lock(instancesMutex());
		instances().insert(this);
	}
	void Editor::create()
	{
		jucePluginEditorLib::Editor::create();

		m_romSelector = findChild<juceRmlUi::ElemComboBox>("romselector");

		if(m_romSelector)
		{
			const auto& roms = getJeProcessor().getRoms();

			if (roms.empty())
				m_romSelector->addOption("<No ROM found>");

			for (const auto& rom : roms)
				m_romSelector->addOption(rom.getName());

			m_romSelector->setSelectedIndex(getJeProcessor().getSelectedRomIndex(), false);

			juceRmlUi::EventListener::Add(m_romSelector, Rml::EventId::Change, [this](Rml::Event&)
			{
				const auto index = m_romSelector->getSelectedIndex();

				if (index < 0 || !getJeProcessor().setSelectedRom(static_cast<size_t>(index)))
					m_romSelector->setSelectedIndex(getJeProcessor().getSelectedRomIndex(), false);
			});
		}

		// a state restored by the host switches the ROM on the host's thread
		m_onRomChanged.set(getJeProcessor().evRomChanged, [this](const jeLib::Rom*)
		{
			juce::MessageManager::callAsync([this]
			{
				std::lock_guard lock(instancesMutex());
				if (instances().find(this) != instances().end())
					onRomChanged();
			});
		});

		if (auto* lcd = findChild("lcd"))
			m_lcd.reset(new JeLcd(*this, lcd));

		m_midiPorts.reset(new jucePluginEditorLib::MidiPorts(*this, getProcessor()));

		if (m_lcd)
			m_focusedParameter.reset(new JeFocusedParameter(*this, *m_lcd));
		else
			m_focusedParameter.reset(new jucePluginEditorLib::FocusedParameter(m_controller, *this));

		m_partSelect.reset(new PartSelect(*this));

		const auto writeButtons = findChildren("btWrite");

		for (auto* writeButton : writeButtons)
		{
			m_writeButtons.emplace_back(std::make_unique<JePartButton>(writeButton, *this));
			m_writeButtons.back()->initalize(static_cast<uint8_t>(JePart::Performance));

			juceRmlUi::EventListener::Add(writeButton, Rml::EventId::Click, [this](Rml::Event& _event)
			{
				onBtWrite(_event);
			});
		}

		m_assign.reset(new JeAssign(*this));

		addClick("btDec", [this](Rml::Event&)
		{
			getPatchManager()->selectPrevPreset(getPatchManager()->getCurrentPart());
		});

		addClick("btInc", [this](Rml::Event&)
		{
			getPatchManager()->selectNextPreset(getPatchManager()->getCurrentPart());
		});

		addClick("btEdit", [this](Rml::Event& _event)
		{
			juceRmlUi::Menu menu;
			menu.addEntry("Rename Performance...", [this]
			{
				m_lcd->renamePerformance();
			});

			_event.StopPropagation();
			menu.runModal(_event);
		});

		addClick("btMenu", [this](Rml::Event& _event)
		{
			juceRmlUi::Menu menu;
			menu.addEntry("Rename Performance...", [this]
			{
				m_lcd->renamePerformance();
			});

			menu.addSeparator();

			createWritePatchesMenu(menu);

			_event.StopPropagation();
			menu.runModal(_event);
		});
	}

	void Editor::initPluginDataModel(jucePluginEditorLib::PluginDataModel& _model)
	{
		jucePluginEditorLib::Editor::initPluginDataModel(_model);

		_model.set("deviceModel", getDeviceModelName(getJeProcessor().getSelectedRom()));
	}

	Editor::~Editor()
	{
		{
			std::lock_guard lock(instancesMutex());
			instances().erase(this);
		}

		m_assign.reset();
		m_partSelect.reset();
		m_midiPorts.reset();
		m_focusedParameter.reset();
		m_lcd.reset();
	}

	jucePluginEditorLib::patchManager::PatchManager* Editor::createPatchManager(Rml::Element* _parent)
	{
		return new PatchManager(*this, _parent);
	}

	std::pair<std::string, std::string> Editor::getDemoRestrictionText() const
	{
		return {};
	}

	AudioPluginAudioProcessor& Editor::getJeProcessor() const
	{
		return static_cast<AudioPluginAudioProcessor&>(getProcessor());
	}

	void Editor::onRomChanged()
	{
		getPluginDataModel()->set("deviceModel", getDeviceModelName(getJeProcessor().getSelectedRom()));

		if (m_romSelector)
			m_romSelector->setSelectedIndex(getJeProcessor().getSelectedRomIndex(), false);
	}

	void Editor::onBtWrite(Rml::Event& _event) const
	{
		auto* pm = getPatchManager();
		if (!pm)
			return;

		_event.StopPropagation();

		juceRmlUi::Menu menu;

		createWritePatchesMenu(menu);

		menu.runModal(_event);
	}

	bool Editor::createWritePatchesMenu(juceRmlUi::Menu& _menu) const
	{
		auto* pm = getPatchManager();
		if (!pm)
			return false;

		juceRmlUi::Menu menuLower;
		juceRmlUi::Menu menuUpper;
		juceRmlUi::Menu menuPerformance;

		const auto numUpper = pm->createSaveMenuEntries(menuUpper, 0, "Patch Upper", 0);
		const auto numLower = pm->createSaveMenuEntries(menuLower, 1, "Patch Lower", 1);
		const auto numPerformance = pm->createSaveMenuEntries(menuPerformance, 0, "Performance", 2);

		if (numUpper)
			_menu.addSubMenu("Patch Upper", std::move(menuUpper));
		if (numLower)
			_menu.addSubMenu("Patch Lower", std::move(menuLower));
		if (numPerformance)
			_menu.addSubMenu("Performance", std::move(menuPerformance));

		return numUpper + numLower + numPerformance > 0;
	}
}

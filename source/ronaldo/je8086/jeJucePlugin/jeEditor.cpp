#include "jeEditor.h"

#include "jeAssign.h"
#include "jeController.h"
#include "jeLcd.h"
#include "jePartButton.h"
#include "jePartSelect.h"
#include "jePatchManager.h"
#include "jePluginProcessor.h"

#include "baseLib/filesystem.h"

#include "jucePluginEditorLib/firmwareSelector.h"
#include "jucePluginEditorLib/frontPanel.h"
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

		m_firmwareSelector.reset(new jucePluginEditorLib::FirmwareSelector(*this, findChild<juceRmlUi::ElemComboBox>("romselector")));

		// the firmware selector follows a ROM switch on its own, the device model of the skin follows it here. A state
		// restored by the host switches the ROM on the host's thread
		m_onFirmwareChanged.set(getJeProcessor().evFirmwareChanged, [this](uint32_t)
		{
			juce::MessageManager::callAsync([this]
			{
				std::lock_guard lock(instancesMutex());
				if (instances().find(this) != instances().end())
					onFirmwareChanged();
			});
		});

		if (auto* lcd = findChild("lcd"))
			m_lcd.reset(new JeLcd(*this, lcd));

		jucePluginEditorLib::FrontPanel::Binding binding;
		binding.lcdCols = 20;
		binding.lcdRows = 2;
		binding.lcdSink = [this](const std::vector<uint8_t>& _cells)
		{
			if (m_lcd)
				m_lcd->setDeviceText(_cells);
		};
		binding.cgRamSink = [this](const std::array<uint8_t, 64>& _cgRam)
		{
			if (m_lcd)
				m_lcd->setCgRam(_cgRam);
		};
		m_frontPanel.reset(new jucePluginEditorLib::FrontPanel(*this, std::move(binding)));

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

		m_firmwareSelector.reset();
		m_assign.reset();
		m_partSelect.reset();
		m_frontPanel.reset();
		m_lcd.reset();
	}

	void Editor::onFocusedParameterChanged(const std::string& _name, const std::string& _value)
	{
		// null while the base create() runs and in a skin without LCD
		if (m_lcd)
			m_lcd->setParameterDisplay(_name, _value);
	}

	AudioPluginAudioProcessor& Editor::getJeProcessor() const
	{
		return static_cast<AudioPluginAudioProcessor&>(getProcessor());
	}

	void Editor::onFirmwareChanged()
	{
		getPluginDataModel()->set("deviceModel", getDeviceModelName(getJeProcessor().getSelectedRom()));
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

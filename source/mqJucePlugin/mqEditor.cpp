#include "mqEditor.h"

#include <juceRmlPlugin/skinConverter/skinConverterOptions.h>

#include "PluginProcessor.h"

#include "mqController.h"
#include "mqFrontPanel.h"
#include "mqPartSelect.h"
#include "mqPatchManager.h"

#include "mqLib/mqbuildconfig.h"

#include "jucePluginEditorLib/focusedParameter.h"

#include "jucePluginLib/filetype.h"
#include "juceRmlPlugin/rmlTabGroup.h"
#include "juceRmlUi/rmlElemButton.h"

namespace mqJucePlugin
{
	static constexpr uint32_t PlayModeListenerId = 1;

	Editor::Editor(jucePluginEditorLib::Processor& _processor, const jucePluginEditorLib::Skin& _skin)
		: jucePluginEditorLib::Editor(_processor, _skin)
		, m_controller(dynamic_cast<Controller&>(_processor.getController()))
	{
	}

	void Editor::create()
	{
		jucePluginEditorLib::Editor::create();

		m_frontPanel.reset(new FrontPanel(*this, m_controller));

		auto disableButton = [](Rml::Element* _comp)
		{
			_comp->SetProperty(Rml::PropertyId::Opacity, Rml::Property(0.5f, Rml::Unit::NUMBER));
			_comp->SetProperty(Rml::PropertyId::PointerEvents, Rml::Style::PointerEvents::None);
		};

		auto disableByName = [this, &disableButton](const std::string& _button)
		{
			if (auto* bt = findChild(_button, false))
				disableButton(bt);
		};

		m_btPlayModeMulti = findChild("btPlayModeMulti", false);
		if(m_btPlayModeMulti)
		{
			if constexpr(mqLib::g_pluginDemo)
			{
				disableButton(m_btPlayModeMulti);
			}
			else
			{
				juceRmlUi::EventListener::AddClick(m_btPlayModeMulti, [this]
				{
					const auto checked = juceRmlUi::ElemButton::isChecked(m_btPlayModeMulti);
					juceRmlUi::ElemButton::setChecked(m_btPlayModeMulti, !checked);
					m_controller.setPlayMode(checked);
				});

				juceRmlUi::ElemButton::setChecked(m_btPlayModeMulti, m_controller.isMultiMode());
			}
		}

		m_btSave = findChild("btSave", false);
		if (m_btSave)
		{
			if constexpr (mqLib::g_pluginDemo)
			{
				disableButton(m_btSave);
			}
			else
			{
				juceRmlUi::EventListener::Add(m_btSave, Rml::EventId::Click, [this](const Rml::Event& _event)
				{
					onBtSave(_event);
				});
			}
		}

		if constexpr(mqLib::g_pluginDemo)
		{
			disableByName("btPageMulti");
			disableByName("btPageDrum");
		}

		m_btPresetPrev = findChild("btPresetPrev", false);
		m_btPresetNext = findChild("btPresetNext", m_btPresetPrev != nullptr);

		if (m_btPresetPrev)
		{
			juceRmlUi::EventListener::AddClick(m_btPresetPrev, [this]			{ onBtPresetPrev(); });
			juceRmlUi::EventListener::AddClick(m_btPresetNext, [this]			{ onBtPresetNext(); });
		}

		m_focusedParameter.reset(new jucePluginEditorLib::FocusedParameter(m_controller, *this));

		if(findChild("partSelectButton", false))
			m_partSelect.reset(new mqPartSelect(*this, m_controller));

		m_controller.onPlayModeChanged.addListener(PlayModeListenerId, [this](bool)
		{
			if(m_btPlayModeMulti)
				rmlPlugin::TabGroup::setChecked(m_btPlayModeMulti, m_controller.isMultiMode());
			if(m_partSelect)
				m_partSelect->onPlayModeChanged();
		});
	}

	jucePluginEditorLib::patchManager::PatchManager* Editor::createPatchManager(Rml::Element* _parent)
	{
		return new PatchManager(*this, _parent);
	}

	void Editor::initSkinConverterOptions(rmlPlugin::skinConverter::SkinConverterOptions& _skinConverterOptions)
	{
		jucePluginEditorLib::Editor::initSkinConverterOptions(_skinConverterOptions);

		_skinConverterOptions.idReplacements.insert({"patchbrowser", "container-patchmanager"});
	}

	Editor::~Editor()
	{
		m_controller.onPlayModeChanged.removeListener(PlayModeListenerId);

		m_partSelect.reset();
		m_focusedParameter.reset();
		m_frontPanel.reset();
	}

	std::pair<std::string, std::string> Editor::getDemoRestrictionText() const
	{
		return {"Vavra",
			"Vavra runs in demo mode\n"
			"\n"
			"The following features are disabled:\n"
			"- Saving/Exporting Presets\n"
			"- Plugin state is not preserved"
		};
	}

	void Editor::savePreset(const pluginLib::FileType& _type)
	{
		jucePluginEditorLib::Editor::savePreset(_type, [&](const juce::File& _file)
		{
			auto type = _type;
			const auto file = createValidFilename(type, _file);

			const auto part = m_controller.getCurrentPart();
			const auto p = m_controller.createSingleDump(mqLib::MidiBufferNum::SingleBankA, static_cast<mqLib::MidiSoundLocation>(0), part, part);
			if(!p.empty())
			{
				const synthLib::SysexBufferList presets{p};
				jucePluginEditorLib::Editor::savePresets(_type, file, presets);
			}
		});
	}

	void Editor::onBtSave(const Rml::Event& _event)
	{
		juceRmlUi::Menu menu;

		const auto countAdded = getPatchManager()->createSaveMenuEntries(menu);

		if(countAdded)
			menu.addSeparator();

		auto addEntry = [&](juceRmlUi::Menu& _menu, const std::string& _name, const std::function<void(pluginLib::FileType)>& _callback)
		{
			juceRmlUi::Menu subMenu;

			subMenu.addEntry(".syx", [_callback]() {_callback(pluginLib::FileType::Syx); });
			subMenu.addEntry(".mid", [_callback]() {_callback(pluginLib::FileType::Mid); });

			_menu.addSubMenu(_name, std::move(subMenu));
		};

		addEntry(menu, "Current Single (Edit Buffer)", [this](const pluginLib::FileType& _type)
		{
			savePreset(_type);
		});

		menu.runModal(_event);
	}

	void Editor::onBtPresetPrev() const
	{
		if (getPatchManager() && getPatchManager()->selectPrevPreset(m_controller.getCurrentPart()))
			return;
		m_controller.selectPrevPreset();
	}

	void Editor::onBtPresetNext() const
	{
		if (getPatchManager() && getPatchManager()->selectNextPreset(m_controller.getCurrentPart()))
			return;
		m_controller.selectNextPreset();
	}
}

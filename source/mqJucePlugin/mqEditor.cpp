#include "mqEditor.h"

#include "PluginProcessor.h"

#include "mqController.h"
#include "mqFrontPanel.h"
#include "mqPartButton.h"
#include "mqPartSelect.h"
#include "mqPatchManager.h"

#include "mqLib/mqbuildconfig.h"

#include "jucePluginEditorLib/focusedParameter.h"

namespace mqJucePlugin
{
	static constexpr uint32_t PlayModeListenerId = 1;

	Editor::Editor(jucePluginEditorLib::Processor& _processor, pluginLib::ParameterBinding& _binding, const jucePluginEditorLib::Skin& _skin)
	: jucePluginEditorLib::Editor(_processor, _binding, _skin)
	, m_controller(dynamic_cast<Controller&>(_processor.getController()))
	{
		create();

		m_frontPanel.reset(new FrontPanel(*this, m_controller));

		if(auto* container = findComponent("patchbrowser", false))
		{
			constexpr auto scale = 1.3f;
			const float x = static_cast<float>(container->getX());
			const float y = static_cast<float>(container->getY());
			const float w = static_cast<float>(container->getWidth());
			const float h = static_cast<float>(container->getHeight());
			container->setTransform(juce::AffineTransform::scale(scale, scale));
			container->setSize(static_cast<int>(w / scale),static_cast<int>(h / scale));
			container->setTopLeftPosition(static_cast<int>(x / scale),static_cast<int>(y / scale));

			setPatchManager(new PatchManager(*this, container));
		}

		auto disableButton = [](juce::Component* _comp)
		{
			_comp->setAlpha(0.5f);
			_comp->setEnabled(false);
		};

		auto disableByName = [this, &disableButton](const std::string& _button)
		{
			if (auto* bt = findComponentT<juce::Button>(_button, false))
				disableButton(bt);
		};

		m_btPlayModeMulti = findComponentT<juce::Button>("btPlayModeMulti", false);
		if(m_btPlayModeMulti)
		{
			if constexpr(mqLib::g_pluginDemo)
			{
				disableButton(m_btPlayModeMulti);
			}
			else
			{
				m_btPlayModeMulti->onClick = [this]()
				{
					m_controller.setPlayMode(m_btPlayModeMulti->getToggleState());
				};
			}
		}

		m_btSave = findComponentT<juce::Button>("btSave", false);
		if (m_btSave)
		{
			if constexpr (mqLib::g_pluginDemo)
			{
				disableButton(m_btSave);
			}
			else
			{
				m_btSave->onClick = [this]()
				{
					onBtSave();
				};
			}
		}

		if constexpr(mqLib::g_pluginDemo)
		{
			disableByName("btPageMulti");
			disableByName("btPageDrum");
		}

		m_btPresetPrev = findComponentT<juce::Button>("btPresetPrev", false);
		m_btPresetNext = findComponentT<juce::Button>("btPresetNext", m_btPresetPrev != nullptr);

		if (m_btPresetPrev)
		{
			m_btPresetPrev->onClick = [this]			{ onBtPresetPrev();	};
			m_btPresetNext->onClick = [this]			{ onBtPresetNext();	};
		}

		m_focusedParameter.reset(new jucePluginEditorLib::FocusedParameter(m_controller, _binding, *this));

		if(!findComponents("partSelectButton", false).empty())
			m_partSelect.reset(new mqPartSelect(*this, m_controller, _binding));

		addMouseListener(this, true);

		m_controller.onPlayModeChanged.addListener(PlayModeListenerId, [this](bool)
		{
			if(m_btPlayModeMulti)
				m_btPlayModeMulti->setToggleState(m_controller.isMultiMode(), juce::dontSendNotification);
			if(m_partSelect)
				m_partSelect->onPlayModeChanged();
		});
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
			"- Plugin state is not preserve"
		};
	}

	genericUI::Button<juce::DrawableButton>* Editor::createJuceComponent(genericUI::Button<juce::DrawableButton>* _button, genericUI::UiObject& _object, const std::string& _name, juce::DrawableButton::ButtonStyle _buttonStyle)
	{
		if(_object.getName() == "partSelectButton")
			return new mqPartButton(*this, _name, _buttonStyle);

		return jucePluginEditorLib::Editor::createJuceComponent(_button, _object, _name, _buttonStyle);
	}

	void Editor::mouseEnter(const juce::MouseEvent& _event)
	{
		m_focusedParameter->onMouseEnter(_event);
	}

	void Editor::savePreset(const jucePluginEditorLib::FileType _type)
	{
		jucePluginEditorLib::Editor::savePreset([&](const juce::File& _file)
		{
			jucePluginEditorLib::FileType type = _type;
			const auto file = createValidFilename(type, _file);

			const auto part = m_controller.getCurrentPart();
			const auto p = m_controller.createSingleDump(mqLib::MidiBufferNum::SingleBankA, static_cast<mqLib::MidiSoundLocation>(0), part, part);
			if(!p.empty())
			{
				const std::vector<std::vector<uint8_t>> presets{p};
				jucePluginEditorLib::Editor::savePresets(_type, file, presets);
			}
		});
	}

	void Editor::onBtSave()
	{
		juce::PopupMenu menu;

		const auto countAdded = getPatchManager()->createSaveMenuEntries(menu);

		if(countAdded)
			menu.addSeparator();

		auto addEntry = [&](juce::PopupMenu& _menu, const std::string& _name, const std::function<void(jucePluginEditorLib::FileType)>& _callback)
		{
			juce::PopupMenu subMenu;

			subMenu.addItem(".syx", [_callback]() {_callback(jucePluginEditorLib::FileType::Syx); });
			subMenu.addItem(".mid", [_callback]() {_callback(jucePluginEditorLib::FileType::Mid); });

			_menu.addSubMenu(_name, subMenu);
		};

		addEntry(menu, "Current Single (Edit Buffer)", [this](jucePluginEditorLib::FileType _type)
		{
			savePreset(_type);
		});

		menu.showMenuAsync(juce::PopupMenu::Options());
	}

	void Editor::onBtPresetPrev()
	{
		if (getPatchManager() && getPatchManager()->selectPrevPreset(m_controller.getCurrentPart()))
			return;
		m_controller.selectPrevPreset();
	}

	void Editor::onBtPresetNext()
	{
		if (getPatchManager() && getPatchManager()->selectNextPreset(m_controller.getCurrentPart()))
			return;
		m_controller.selectNextPreset();
	}
}

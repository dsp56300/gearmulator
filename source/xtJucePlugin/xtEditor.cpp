#include "xtEditor.h"

#include "juceRmlPlugin/skinConverter/skinConverterOptions.h"

#include "PluginProcessor.h"

#include "xtArp.h"
#include "xtController.h"
#include "xtFocusedParameter.h"
#include "xtFrontPanel.h"
#include "xtPatchManager.h"
#include "xtWaveEditor.h"

#include "jucePluginEditorLib/midiPorts.h"

#include "juceRmlUi/rmlElemButton.h"

namespace xtJucePlugin
{
	Editor::Editor(jucePluginEditorLib::Processor& _processor, const jucePluginEditorLib::Skin& _skin)
		: jucePluginEditorLib::Editor(_processor, _skin)
		, m_controller(dynamic_cast<Controller&>(_processor.getController()))
		, m_playModeChangeListener(m_controller.onPlayModeChanged)
	{
	}
	void Editor::create()
	{
		jucePluginEditorLib::Editor::create();

		m_focusedParameter.reset(new FocusedParameter(m_controller, *this));

		m_frontPanel.reset(new FrontPanel(*this, m_controller));

		m_parts.reset(new Parts(*this));

		m_ledMultiMode = findChild("MultiModeLED");

		m_btMultiMode = addClick("MultiModeButton", [this](Rml::Event&)
		{
			m_controller.setPlayMode(juceRmlUi::ElemButton::isChecked(m_btMultiMode));
		}, true);

		auto updateMultiButton = [this](const bool _isMultiMode)
		{
			juceRmlUi::ElemButton::setChecked(m_btMultiMode, _isMultiMode);
			juceRmlUi::ElemButton::setChecked(m_ledMultiMode, _isMultiMode);
		};

		updateMultiButton(m_controller.isMultiMode());

		m_playModeChangeListener = [this, updateMultiButton](const bool& _isMultiMode)
		{
			updateMultiButton(_isMultiMode);
			m_frontPanel->getLcd()->refresh();

			if(!_isMultiMode)
				m_parts->selectPart(0);
		};

		addClick("SaveButton", [this](const Rml::Event& _event)
		{
			juceRmlUi::Menu menu;

			const auto countAdded = getPatchManager()->createSaveMenuEntries(menu);

			if(countAdded)
				menu.runModal(_event);
		});

		addClick("wtPlus", [this](Rml::Event&)		{ changeWave(1); });
		addClick("wtMinus", [this](Rml::Event&)	{ changeWave(-1); });
		addClick("patchPrev", [this](Rml::Event&)	{ getPatchManager()->selectPrevPreset(m_controller.getCurrentPart()); });
		addClick("patchNext", [this](Rml::Event&)	{ getPatchManager()->selectNextPreset(m_controller.getCurrentPart()); });

		if (auto* waveEditorParent = findChild("waveEditorContainer"))
		{
			const auto dir = getProcessor().getDataFolder(false) + "wavetables/";

			m_waveEditor = new WaveEditor(*this, waveEditorParent, juce::File(dir));
			getXtController().setWaveEditor(m_waveEditor);
		}
		else if (auto* waveEditorButtonParent = findChild("waveEditorButtonParent"))
		{
			waveEditorButtonParent->SetProperty(Rml::PropertyId::Display, Rml::Property(Rml::Style::Display::None));
		}

		m_midiPorts.reset(new jucePluginEditorLib::MidiPorts(*this, getProcessor()));

		m_arp.reset(new Arp(*this));
	}

	Editor::~Editor()
	{
		m_arp.reset();
		if(m_waveEditor)
		{
			delete m_waveEditor;
			m_waveEditor = nullptr;
		}
		getXtController().setWaveEditor(nullptr);
		m_frontPanel.reset();
	}

	jucePluginEditorLib::patchManager::PatchManager* Editor::createPatchManager(Rml::Element* _parent)
	{
		return new PatchManager(*this, _parent);
	}

	void Editor::initSkinConverterOptions(rmlPlugin::skinConverter::SkinConverterOptions& _skinConverterOptions)
	{
		jucePluginEditorLib::Editor::initSkinConverterOptions(_skinConverterOptions);

		_skinConverterOptions.includeStyles.push_back("xt-waveeditor-defaults.rcss");

		_skinConverterOptions.idReplacements.insert({"ContainerPatchManager", "container-patchmanager"});
	}

	std::pair<std::string, std::string> Editor::getDemoRestrictionText() const
	{
		return {"Vavra",
			"Xenia runs in demo mode\n"
			"\n"
			"The following features are disabled:\n"
			"- Saving/Exporting Presets\n"
			"- Plugin state is not preserved"
		};
	}

	XtLcd* Editor::getLcd() const
	{
		return m_frontPanel->getLcd();
	}

	Parts& Editor::getParts() const
	{
		assert(m_parts);
		return *m_parts;
	}

	void Editor::setCurrentPart(const uint8_t _part)
	{
		m_controller.setCurrentPart(_part);

		jucePluginEditorLib::Editor::setCurrentPart(_part);

		m_frontPanel->getLcd()->refresh();
	}

	void Editor::changeWave(const int _step) const
	{
		if(!_step)
			return;

		uint32_t index;
		if(!m_controller.getParameterDescriptions().getIndexByName(index, "Wave"))
			return;

		auto* p = m_controller.getParameter(index, m_controller.getCurrentPart());

		if(!p)
			return;

		const auto& desc = p->getDescription();
		const auto& range = desc.range;

		int v = p->getUnnormalizedValue();

		const auto& valList = desc.valueList;

		while(v >= range.getStart() && v <= range.getEnd())
		{
			v += _step;
			const auto newText = valList.valueToText(v);

			if(newText.empty())
				continue;

			p->setUnnormalizedValueNotifyingHost(v, pluginLib::Parameter::Origin::Ui);
			break;
		}
	}
}

#include "SettingsDspAudioOsTIrus.h"

#include "VirusEditor.h"
#include "VirusProcessor.h"

#include "juceRmlUi/rmlElemButton.h"
#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlHelper.h"

#include "RmlUi/Core/Element.h"

#include <algorithm>
#include <cmath>

namespace genericVirusUI
{
	void SettingsDspAudioOsTIrus::setupSamplerateButtons(Rml::Element* _template, const std::vector<float>& _samplerates, const std::vector<float>& _preferred, float _current, bool _usePreferred, virus::VirusProcessor& _processor)
	{
		auto* parent = _template->GetParentNode();
		
		for (const float samplerate : _samplerates)
		{
			const auto isPreferred = std::find(_preferred.begin(), _preferred.end(), samplerate) != _preferred.end();
			if(isPreferred != _usePreferred)
				continue;

			auto clone = _template->Clone();
			auto* clonePtr = clone.get();
			clonePtr->SetId("");
			clonePtr->RemoveProperty("display");
			
			auto* button = juceRmlUi::helper::findChildT<juceRmlUi::ElemButton>(clonePtr, "button");
			auto* label = juceRmlUi::helper::findChild(clonePtr, "label");
			
			const auto title = std::to_string(static_cast<int>(std::floor(samplerate + 0.5f))) + " Hz";
			label->SetInnerRML(title);
			
			button->setChecked(std::fabs(samplerate - _current) < 1.0f);
			m_samplerateButtons.emplace_back(samplerate, button);

			juceRmlUi::EventListener::AddClick(clonePtr, [this, &_processor, samplerate]
			{
				_processor.setPreferredDeviceSamplerate(samplerate);
				updateButtons();
			});

			parent->InsertBefore(std::move(clone), _template);
		}
	}
	SettingsDspAudioOsTIrus::SettingsDspAudioOsTIrus(const VirusEditor* _editor, Rml::Element* _root)
		: m_editor(_editor)
	{
		auto& processor = static_cast<virus::VirusProcessor&>(_editor->getProcessor());

		const auto samplerates = processor.getDeviceSupportedSamplerates();

		if(samplerates.size() <= 1)
		{
			juceRmlUi::helper::setVisible(_root, false);
			return;
		}

		const auto current = processor.getPreferredDeviceSamplerate();
		const auto preferred = processor.getDevicePreferredSamplerates();

		// Setup automatic button
		auto* btAutomatic = juceRmlUi::helper::findChild(_root, "btSamplerateAutomatic");
		auto* btAutomaticButton = juceRmlUi::helper::findChildT<juceRmlUi::ElemButton>(btAutomatic, "button");
		
		btAutomaticButton->setChecked(current == 0.0f);
		m_samplerateButtons.emplace_back(0.0f, btAutomaticButton);

		juceRmlUi::EventListener::AddClick(btAutomatic, [this, &processor]
		{
			processor.setPreferredDeviceSamplerate(0.0f);
			updateButtons();
		});

		// Setup official samplerates
		auto* templateOfficial = juceRmlUi::helper::findChild(_root, "btSamplerateOfficial");
		setupSamplerateButtons(templateOfficial, samplerates, preferred, current, true, processor);
		templateOfficial->GetParentNode()->RemoveChild(templateOfficial);

		// Setup undocumented samplerates
		auto* templateUndocumented = juceRmlUi::helper::findChild(_root, "btSamplerateUndocumented");
		setupSamplerateButtons(templateUndocumented, samplerates, preferred, current, false, processor);
		templateUndocumented->GetParentNode()->RemoveChild(templateUndocumented);
	}

	void SettingsDspAudioOsTIrus::updateButtons() const
	{
		auto& processor = static_cast<virus::VirusProcessor&>(m_editor->getProcessor());
		const auto currentSamplerate = processor.getPreferredDeviceSamplerate();

		for (const auto& [samplerate, button] : m_samplerateButtons)
		{
			if(samplerate == 0.0f)
				button->setChecked(currentSamplerate == 0.0f);
			else
				button->setChecked(std::fabs(samplerate - currentSamplerate) < 1.0f);
		}
	}
}

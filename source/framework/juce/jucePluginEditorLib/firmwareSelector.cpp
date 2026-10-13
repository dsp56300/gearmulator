#include "firmwareSelector.h"

#include "pluginEditor.h"
#include "pluginProcessor.h"

#include "juceRmlUi/rmlElemComboBox.h"
#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlInterfaces.h"

namespace jucePluginEditorLib
{
	FirmwareSelector::FirmwareSelector(Editor& _editor, juceRmlUi::ElemComboBox* _comboBox, const std::string& _emptyText)
		: m_editor(_editor)
		, m_comboBox(_comboBox)
	{
		if (!m_comboBox)
			return;

		auto& processor = m_editor.getProcessor();
		const auto& names = processor.getFirmwareNames();

		if (names.empty())
			m_comboBox->addOption(_emptyText);

		for (const auto& name : names)
			m_comboBox->addOption(name);

		juceRmlUi::EventListener::Add(m_comboBox, Rml::EventId::Change, [this, &processor](Rml::Event&)
		{
			const auto index = m_comboBox->getSelectedIndex();

			if (index < 0 || !processor.selectFirmware(static_cast<uint32_t>(index)))
				update();
		});

		m_onFirmwareChanged.set(processor.evFirmwareChanged, [this](uint32_t)
		{
			if (juce::MessageManager::getInstance()->isThisTheMessageThread())
			{
				update();
				return;
			}

			// this object goes away on the message thread only, so the check there is safe
			juce::MessageManager::callAsync([this, alive = std::weak_ptr(m_alive)]
			{
				if (alive.lock())
					update();
			});
		});
	}

	void FirmwareSelector::update() const
	{
		const juceRmlUi::RmlInterfaces::ScopedAccess access(*m_editor.getRmlComponent());
		m_comboBox->setSelectedIndex(m_editor.getProcessor().getFirmwareIndex(), false);
	}
}

#include "settingsMidiMatrix.h"

#include "pluginProcessor.h"

#include "settingsMidi.h"
#include "settingsStyles.h"

namespace jucePluginEditorLib
{
	SettingsMidiMatrix::SettingsMidiMatrix(SettingsMidi& _midi, synthLib::MidiRoutingMatrix::EventType _type, std::string _name)
	: m_midi(_midi), m_eventType(_type), m_name(std::move(_name))
	{
		m_headline.reset(new juce::Label(m_name, m_name));
		m_headline->setJustificationType(juce::Justification::centredTop);

		m_corner.reset(new juce::Label("From/To", "From/To"));

		settings::getStyleHeadline2().apply(m_headline.get());
		settings::getStyle().apply(m_corner.get());

		addAndMakeVisible(m_headline.get());
		addAndMakeVisible(m_corner.get());

		for (size_t i=0; i<CellCount; ++i)
		{
			auto sourceType = Cells[i];

			const auto name = std::string(synthLib::MidiRoutingMatrix::toString(sourceType));

			m_sourceLabels[i].reset(new juce::Label(name, name));
			m_destLabels[i].reset(new juce::Label(name, name));

			addAndMakeVisible(m_sourceLabels[i].get());
			addAndMakeVisible(m_destLabels[i].get());

			settings::getStyle().apply(m_sourceLabels[i].get());
			settings::getStyle().apply(m_destLabels[i].get());

			for (size_t j=0; j<CellCount; ++j)
			{
				auto destType = Cells[j];

				auto button = new juce::ToggleButton();
				button->setButtonText({});
				button->onClick = [this, button, sourceType, destType] { onClicked(button, sourceType, destType); };
				m_cells[i][j].reset(button);

				addAndMakeVisible(button);

				button->setEnabled(i != j);

				if (i == j)
				{
					auto col = button->findColour(juce::ToggleButton::ColourIds::tickColourId);
					col = col.darker(2.0f);
					button->setColour(juce::ToggleButton::ColourIds::tickDisabledColourId, col);
					button->setColour(juce::ToggleButton::ColourIds::tickColourId, col);
				}

				button->setToggleState(m_midi.getProcessor().getMidiRoutingMatrix().enabled(sourceType, destType, m_eventType), juce::dontSendNotification);
			}
		}
	}

	void SettingsMidiMatrix::resized()
	{
		constexpr int count = CellCount;

		constexpr int headlineHeight = 40;

		m_headline->setBounds(0, 0, getWidth(), headlineHeight);

		const int cellWidth = getWidth() / (count + 1);
		const int cellHeight = (getHeight() - headlineHeight) / (count + 1);

		m_corner->setBounds(0, headlineHeight, cellWidth, cellHeight);

		for (int i = 0; i < count; ++i)
		{
			m_sourceLabels[i]->setBounds(cellWidth * (i + 1), headlineHeight, cellWidth, cellHeight);
			m_destLabels[i]->setBounds(0, cellHeight * (i + 1) + headlineHeight, cellWidth, cellHeight);

			for (int j = 0; j < count; ++j)
			{
				m_cells[i][j]->setBounds(cellWidth * (j + 1), cellHeight * (i + 1) + headlineHeight, cellWidth, cellHeight);
			}
		}
		Component::resized();
	}

	void SettingsMidiMatrix::onClicked(const juce::ToggleButton* _button, const synthLib::MidiEventSource _source, const synthLib::MidiEventSource _dest) const
	{
		auto& matrix = m_midi.getProcessor().getMidiRoutingMatrix();
		matrix.setEnabled(_source, _dest, m_eventType, _button->getToggleState());
	}
}

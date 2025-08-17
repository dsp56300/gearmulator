#pragma once

#include <string>

#include "juce_gui_basics/juce_gui_basics.h"

namespace genericUI
{
	class Editor;

	class ControllerLink : juce::Slider::Listener, juce::ComponentListener
	{
	public:
		ControllerLink(ControllerLink&&) = delete;
		ControllerLink(const ControllerLink&) = delete;

		ControllerLink(std::string _source = std::string(), std::string _dest = std::string(), std::string _conditionButton = std::string());
		~ControllerLink() override;

		void create(const Editor& _editor);

		void create(juce::Slider* _source, juce::Slider* _dest, juce::Button* _condition);

		void operator = (const ControllerLink&) = delete;
		void operator = (ControllerLink&&) = delete;

		const std::string& getSourceName() const { return m_sourceName; }
		const std::string& getDestName() const { return m_destName; }
		const std::string& getConditionButtonName() const { return m_conditionButton; }

	private:
		void sliderValueChanged(juce::Slider* _slider) override;
		void sliderDragStarted(juce::Slider*) override;
		void sliderDragEnded(juce::Slider*) override;

		void componentBeingDeleted(juce::Component& component) override;

		const std::string m_sourceName;
		const std::string m_destName;
		const std::string m_conditionButton;

		juce::Slider* m_source = nullptr;
		juce::Slider* m_dest = nullptr;
		juce::Button* m_condition = nullptr;
		double m_lastSourceValue = 0;
		bool m_sourceIsBeingDragged = false;
	};
}

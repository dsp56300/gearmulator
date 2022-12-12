#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace juce
{
	class MouseEvent;
	class Slider;
}

namespace pluginLib
{
	class Parameter;
	class Controller;

	class ParameterBinding
	{
		class MouseListener : public juce::MouseListener
		{
		public:
			MouseListener(pluginLib::Parameter* _param, juce::Slider& _slider);
			void mouseDown(const juce::MouseEvent& event) override;
			void mouseUp(const juce::MouseEvent& event) override;
			void mouseDrag(const juce::MouseEvent& event) override;

		private:
			pluginLib::Parameter *m_param;
			juce::Slider* m_slider;
		};
	public:
		static constexpr uint8_t CurrentPart = 0xff;

		struct BoundParameter
		{
			pluginLib::Parameter* parameter = nullptr;
			juce::Component* component = nullptr;
			uint32_t type = 0xffffffff;
			uint8_t part = CurrentPart;
			uint32_t onChangeListenerId = 0;
		};

		ParameterBinding(Controller& _controller) : m_controller(_controller)
		{
		}
		~ParameterBinding();

		void bind(juce::Slider& _control, uint32_t _param);
		void bind(juce::Slider& _control, uint32_t _param, uint8_t _part);
		void bind(juce::ComboBox &_control, uint32_t _param);
		void bind(juce::ComboBox &_control, uint32_t _param, uint8_t _part);
		void bind(juce::Button &_control, uint32_t _param);
		void bind(juce::Button &_control, uint32_t _param, uint8_t _part);

		void clearBindings();
		void setPart(uint8_t _part);

		void disableBindings();
		void enableBindings();

		const auto& getBindings() const { return m_bindings; }
		juce::Component* getBoundComponent(const pluginLib::Parameter* _parameter);

	private:
		void removeMouseListener(juce::Slider& _slider);

		void addBinding(const BoundParameter& _boundParameter);
		void disableBinding(const BoundParameter& _b);

		Controller& m_controller;

		void bind(const std::vector<BoundParameter>& _bindings, bool _currentPartOnly);

		std::vector<BoundParameter> m_bindings;
		std::vector<BoundParameter> m_disabledBindings;
		std::map<const pluginLib::Parameter*, juce::Component*> m_boundParameters;
		std::map<const juce::Component*, pluginLib::Parameter*> m_boundComponents;
		std::map<juce::Slider*, MouseListener*> m_sliderMouseListeners;
		uint32_t m_nextListenerId = 100000;
		};
}

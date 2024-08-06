#pragma once

#include "juce_gui_basics/juce_gui_basics.h"

#include "event.h"
#include "parameterlistener.h"

namespace juce
{
	class Button;
	class ComboBox;
	class Component;
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
			void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
			void mouseDoubleClick(const juce::MouseEvent& event) override;

		private:
			pluginLib::Parameter *m_param;
			juce::Slider* m_slider;
		};
	public:
		static constexpr uint8_t CurrentPart = 0xff;

		struct BoundParameter
		{
			Parameter* parameter = nullptr;
			juce::Component* component = nullptr;
			uint32_t paramIndex = 0xffffffff;
			uint8_t part = CurrentPart;
			size_t onChangeListenerId = ParameterListener::InvalidListenerId;
		};

		Event<BoundParameter> onBind;
		Event<BoundParameter> onUnbind;

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

		bool bind(juce::Component& _component, uint32_t _param, uint8_t _part);

		void clearBindings();
		void clear();
		void setPart(uint8_t _part);

		void disableBindings();
		void enableBindings();

		const auto& getBindings() const { return m_bindings; }
		juce::Component* getBoundComponent(const pluginLib::Parameter* _parameter) const;
		pluginLib::Parameter* getBoundParameter(const juce::Component* _component) const;

		bool unbind(const Parameter* _param);
		bool unbind(const juce::Component* _component);

	private:
		void removeMouseListener(juce::Slider& _slider);

		void addBinding(const BoundParameter& _boundParameter);
		void disableBinding(BoundParameter& _b);

		Controller& m_controller;

		void bind(const std::vector<BoundParameter>& _bindings, bool _currentPartOnly);

		std::vector<BoundParameter> m_bindings;
		std::vector<BoundParameter> m_disabledBindings;
		std::map<const pluginLib::Parameter*, juce::Component*> m_boundParameters;
		std::map<const juce::Component*, pluginLib::Parameter*> m_boundComponents;
		std::map<juce::Slider*, MouseListener*> m_sliderMouseListeners;
	};
}

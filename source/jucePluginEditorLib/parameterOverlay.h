#pragma once

#include "baseLib/event.h"

#include "RmlUi/Core/Vector2.h"

namespace pluginLib
{
	class Parameter;
}

namespace Rml
{
	class Element;
}

namespace juce
{
	class DrawableImage;
	class Component;
}

namespace jucePluginEditorLib
{
	class ParameterOverlays;

	class ParameterOverlay
	{
	public:
		enum class Type
		{
			Lock,
			Link,

			Count
		};

		static constexpr size_t InvalidListenerId = baseLib::Event<int>::InvalidListenerId;
		static constexpr uint32_t OverlayCount = static_cast<uint32_t>(Type::Count);

		explicit ParameterOverlay(ParameterOverlays& _overlays, Rml::Element* _component);
		~ParameterOverlay();

		void onBind(pluginLib::Parameter* _parameter, Rml::Element* _element);
		void onUnbind(pluginLib::Parameter* _parameter, Rml::Element* _element);

		void refresh()
		{
			updateOverlays();
		}

	private:
		struct OverlayProperties
		{
			uint32_t color = 0xffffffff;
			float scale = 0.2f;
			Rml::Vector2f position = { 0,0 };
		};

		void toggleOverlay(Type _type, bool _enable, float _opacity = 1.0f);

		void updateOverlays();
		void setParameter(pluginLib::Parameter* _parameter);

		ParameterOverlays& m_overlays;
		Rml::Element* const m_component;

		pluginLib::Parameter* m_parameter = nullptr;
		std::unordered_map<Type, Rml::Element*> m_overlayElements;

		size_t m_parameterLockChangedListener = InvalidListenerId;
		size_t m_parameterLinkChangedListener = InvalidListenerId;

//		std::array<juce::DrawableImage*, OverlayCount> m_images{nullptr,nullptr};
	};
}

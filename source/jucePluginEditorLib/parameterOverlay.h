#pragma once

#include "imagePool.h"
#include "../jucePluginLib/parameterbinding.h"

namespace juce
{
	class Component;
}

namespace jucePluginEditorLib
{
	class ParameterOverlays;

	class ParameterOverlay
	{
	public:
		static constexpr size_t InvalidListenerId = pluginLib::Event<int>::InvalidListenerId;

		explicit ParameterOverlay(ParameterOverlays& _overlays, juce::Component* _component);
		~ParameterOverlay();

		void onBind(const pluginLib::ParameterBinding::BoundParameter& _parameter);
		void onUnbind(const pluginLib::ParameterBinding::BoundParameter& _parameter);

	private:
		struct OverlayProperties
		{
			juce::Colour color = juce::Colour(0xffffffff);
			float scale = 0.2f;
			juce::Point<float> position = juce::Point<float>(0,0);;
		};

		OverlayProperties getOverlayProperties() const;

		void toggleOverlay(juce::DrawableImage*& _image, ImagePool::Type _type, bool _enable) const;

		void updateOverlays();
		void setParameter(pluginLib::Parameter* _parameter);

		ParameterOverlays& m_overlays;
		juce::Component* const m_component;
		pluginLib::Parameter* m_parameter = nullptr;

		size_t m_parameterLockChangedListener = InvalidListenerId;

		juce::DrawableImage* m_imageLock = nullptr;
	};
}

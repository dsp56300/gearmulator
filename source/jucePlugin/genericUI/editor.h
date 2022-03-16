#pragma once

#include <string>

#include <juce_audio_processors/juce_audio_processors.h>

#include "uiObject.h"

namespace Virus
{
	class Controller;
}

class VirusParameterBinding;

namespace genericUI
{
	class Editor : public juce::Component
	{
	public:
		explicit Editor(const std::string& _json, VirusParameterBinding& _binding, Virus::Controller& _controller);

		juce::Drawable* getImageDrawable(const std::string& _texture);
		std::unique_ptr<juce::Drawable> createImageDrawable(const std::string& _texture);

		VirusParameterBinding& getParameterBinding() const { return m_parameterBinding; }
		Virus::Controller& getController() const { return m_controller; }

		void registerComponent(const std::string& _name, juce::Component* _component);

		const std::vector<juce::Component*>& findComponents(const std::string& _name, uint32_t _expectedCount = 0) const;

		template<typename T>
		void findComponents(std::vector<T*>& _dst, const std::string& _name, uint32_t _expectedCount = 0) const
		{
			const auto& res = findComponents(_name, _expectedCount);
			for (auto* s : res)
			{
				auto* t = dynamic_cast<T*>(s);
				if(t)
					_dst.push_back(t);
			}
		}

		juce::Component* findComponent(const std::string& _name, bool _mustExist = true) const;

		template<typename T>
		T* findComponentT(const std::string& _name, bool _mustExist = true) const
		{
			juce::Component* c = findComponent(_name, _mustExist);
			return dynamic_cast<T*>(c);
		}

		float getScale() const { return m_scale; }

	private:
		static const char* getResourceByFilename(const std::string& _filename, int& _outDataSize);

		std::map<std::string, std::unique_ptr<juce::Drawable>> m_drawables;
		std::unique_ptr<UiObject> m_rootObject;

		VirusParameterBinding& m_parameterBinding;
		Virus::Controller& m_controller;

		std::map<std::string, std::vector<juce::Component*>> m_componentsByName;

		float m_scale = 1.0f;
	};
}

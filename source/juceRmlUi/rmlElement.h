#pragma once

#include "RmlUi/Core/Element.h"

namespace juceRmlUi
{
	class Element : public Rml::Element
	{
	public:
		explicit Element(Rml::CoreInstance& _coreInstance, const Rml::String& _tag) : Rml::Element(_coreInstance, _tag)
		{
		}

		void OnAttributeChange(const Rml::ElementAttributes& _changedAttributes) override;
		void OnPropertyChange(const Rml::PropertyIdSet& _changedProperties) override;

		virtual void onPropertyChanged(const std::string& _key) {}

		template<typename T> T getProperty(const std::string& _key, const T& _defaultValue)
		{
			if (const Rml::Property* prop = GetProperty(_key))
				return GetAttribute(_key, prop->Get<T>(GetCoreInstance()));
			return GetAttribute(_key, _defaultValue);
		}
	};
}

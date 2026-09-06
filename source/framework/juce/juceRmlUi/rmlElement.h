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
			return getProperty<T>(this, _key, _defaultValue);
		}

		template<typename T> static T getProperty(Rml::Element* _element, const std::string& _key, const T& _defaultValue)
		{
			Rml::Variant* attrib = _element->GetAttribute(_key);
			if (attrib)
				return attrib->Get<T>(_element->GetCoreInstance());
			if (const Rml::Property* prop = _element->GetProperty(_key))
				return _element->GetAttribute(_key, prop->Get<T>(_element->GetCoreInstance()));
			return _defaultValue;
		}
	};
}

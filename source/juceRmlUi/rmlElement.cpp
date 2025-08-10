#include "rmlElement.h"

#include "RmlUi/Core/CoreInstance.h"
#include "RmlUi/Core/StyleSheetSpecification.h"

namespace juceRmlUi
{
	void Element::OnAttributeChange(const Rml::ElementAttributes& _changedAttributes)
	{
		Rml::Element::OnAttributeChange(_changedAttributes);

		for (const auto& [key, value] : _changedAttributes)
			onPropertyChanged(key);
	}

	void Element::OnPropertyChange(const Rml::PropertyIdSet& _changedProperties)
	{
		Rml::Element::OnPropertyChange(_changedProperties);

		for (const auto id : _changedProperties)
		{
			const auto& name = GetCoreInstance().styleSheetSpecification->GetPropertyName(id);

			onPropertyChanged(name);
		}
	}
}

#include "n2xVmMap.h"

#include "n2xController.h"
#include "n2xEditor.h"

namespace n2xJucePlugin
{
	constexpr const char* g_postfix = "Sens";
	constexpr float g_enabledAlpha = 0.5f;

	VmMap::VmMap(Editor& _editor, pluginLib::ParameterBinding& _binding)
	: m_editor(_editor)
	, m_binding(_binding)
	, m_btVmMap(_editor.findComponentT<juce::Button>("VMMAP"))
	{
		const auto& c = _editor.getN2xController();
		const auto& descs = c.getParameterDescriptions().getDescriptions();

		for (const auto& desc : descs)
		{
			uint32_t idx;

			if(c.getParameterDescriptions().getIndexByName(idx, desc.name + g_postfix))
				m_paramNames.insert(desc.name);
		}

		m_btVmMap->onClick = [this]
		{
			toggleVmMap(m_btVmMap->getToggleState());
		};
	}

	void VmMap::toggleVmMap(const bool _enabled)
	{
		if(m_enabled == _enabled)
			return;

		const auto wasEnabled = m_enabled;

		m_enabled = _enabled;

		const auto& controller = m_editor.getN2xController();

		// initial setup, collect all default-bound components. Only executed once and delayed
		// because in our constructor the components might not be bound yet
		if(m_boundComponents.empty())
		{
			for(const auto& name : m_paramNames)
			{
				const auto paramIdxDefault = controller.getParameterIndexByName(name);
				const auto paramDefault = controller.getParameter(paramIdxDefault);

				auto* comp = m_binding.getBoundComponent(paramDefault);

				if(comp)
					m_boundComponents.insert({name, comp});
			}
		}

		for (const auto& paramName : m_paramNames)
		{
			const auto paramIdxDefault = controller.getParameterIndexByName(paramName);
			const auto paramIdxVm = controller.getParameterIndexByName(paramName + g_postfix);

			auto it = m_boundComponents.find(paramName);

			if(it == m_boundComponents.end())
				continue;

			auto* comp = it->second;

			m_binding.unbind(comp);
			m_boundComponents.erase(it);

			m_binding.bind(*comp, _enabled ? paramIdxVm : paramIdxDefault, pluginLib::ParameterBinding::CurrentPart);

			m_boundComponents.insert({paramName, comp});

			comp->setAlpha(_enabled ? g_enabledAlpha : 1.0f);
		}
	}
}

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

		m_enabled = _enabled;

		const auto& controller = m_editor.getN2xController();

		const auto part = controller.getCurrentPart();

		for (const auto& paramName : m_paramNames)
		{
			const auto paramIdxDefault = controller.getParameterIndexByName(paramName);
			const auto paramIdxVm = controller.getParameterIndexByName(paramName + g_postfix);

			const auto* paramDefault = controller.getParameter(paramName, part);
			const auto* paramVm = controller.getParameter(paramName + g_postfix, part);

			auto* comp = m_binding.getBoundComponent(paramDefault);

			if(comp == nullptr)
				comp = m_binding.getBoundComponent(paramVm);

			if(comp != nullptr)
			{
				m_binding.unbind(comp);
			}
			else
			{
				assert(false && "bound component not found");
				return;
			}

			m_binding.unbind(paramDefault);
			m_binding.unbind(paramVm);

			m_binding.bind(*comp, _enabled ? paramIdxVm : paramIdxDefault, pluginLib::ParameterBinding::CurrentPart);

			comp->setAlpha(_enabled ? g_enabledAlpha : 1.0f);
		}
	}
}

#pragma once

#include <string>
#include <vector>

#include "n2xFocusedParameter.h"

#include "baseLib/event.h"

namespace n2xJucePlugin
{
	class Editor;

	class SliderListener;

	class VmMap
	{
	public:
		explicit VmMap(Editor& _editor);

		void setEnabled(bool _enabled);

	private:
		struct VmParam
		{
			~VmParam();

			std::string paramNameBase;
			std::string paramNameVm;

			pluginLib::Parameter* paramBase = nullptr;
			pluginLib::Parameter* paramVm = nullptr;

			Rml::Element* compBase = nullptr;
			Rml::Element* compVm = nullptr;

			pluginLib::ParameterListener parameterListenerBase;
			pluginLib::ParameterListener parameterListenerVm;

			SliderListener* sliderListenerVm = nullptr;
		};

		void toggleVmMap(bool _enabled);
		void onCurrentPartChanged() const;
		void bindAll() const;

		void onVmSliderChanged(const VmParam& _param) const;

		static void onBaseParamChanged(const VmParam& _vmParam, pluginLib::Parameter* _baseParam);
		static void onVmParamChanged(const VmParam& _vmParam, pluginLib::Parameter* _baseParam);

		static void updateVmSlider(const VmParam& _vmParam);

		Editor& m_editor;

		std::vector<std::unique_ptr<VmParam>> m_vmParams;
		Rml::Element* m_btVmMap;
		bool m_enabled = false;
		baseLib::EventListener<uint8_t> m_onCurrentPartChanged;
	};
}

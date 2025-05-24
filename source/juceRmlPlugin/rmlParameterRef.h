#pragma once

#include "jucePluginLib/parameterlistener.h"

#include <string>
#include <cstdint>

#include "RmlUi/Core/DataModelHandle.h"

namespace Rml
{
	class DataModelConstructor;
}

namespace pluginLib
{
	class Parameter;
	class Controller;
}

namespace rmlPlugin
{
	class RmlParameterBinding;

	class RmlParameterRef
	{
	public:
		explicit RmlParameterRef(RmlParameterBinding& _binding, pluginLib::Parameter* _param, uint32_t _paramIdx, Rml::DataModelConstructor& _model);
		RmlParameterRef(const RmlParameterRef&) = delete;
		RmlParameterRef(RmlParameterRef&&) = default;

		~RmlParameterRef() = default;

		void changePart(const pluginLib::Controller& _controller, uint8_t _part);

		RmlParameterRef& operator=(const RmlParameterRef&) = delete;
		RmlParameterRef& operator=(RmlParameterRef&&) = delete;

	private:
		void setDirty();
		void onParameterValueChanged();

		void setParameter(pluginLib::Parameter* _param);

		RmlParameterBinding& m_binding;
		pluginLib::Parameter* m_parameter = nullptr;
		uint32_t m_paramIdx = 0;
		pluginLib::ParameterListener m_listener;
		const std::string m_prefix;
		Rml::DataModelHandle m_handle;
	};
}

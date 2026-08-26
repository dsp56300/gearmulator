#pragma once

#include "baseLib/event.h"

namespace pluginLib
{
	class Parameter;

	class ParameterListener : public baseLib::EventListener<Parameter*>
	{
	public:
		using Base = EventListener<Parameter*>;
		using Base::set;
		using Base::operator=;

		using Callback = Base::MyCallback;

		ParameterListener() = default;
		ParameterListener(Parameter* _p);
		ParameterListener(Parameter* _p, const Callback& _callback);

		void set(Parameter* _parameter, const Callback& _callback);
		void set(Parameter* _parameter);
	};
}

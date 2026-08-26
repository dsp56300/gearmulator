#include "parameterlistener.h"

#include "parameter.h"

pluginLib::ParameterListener::ParameterListener(Parameter* _p) : EventListener(_p->onValueChanged)
{
}

pluginLib::ParameterListener::ParameterListener(Parameter* _p, const Callback& _callback): EventListener(_p->onValueChanged, _callback)
{
}

void pluginLib::ParameterListener::set(Parameter* _parameter, const Callback& _callback)
{
	Base::set(_parameter->onValueChanged, _callback);
}

void pluginLib::ParameterListener::set(Parameter* _parameter)
{
	Base::set(_parameter->onValueChanged);
}

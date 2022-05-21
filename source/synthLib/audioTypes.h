#pragma once

#include <array>

#include "dsp56kEmu/types.h"

namespace synthLib
{
	template<typename T> using TAudioInputsT		= std::array<const T*,4>;
	template<typename T> using TAudioOutputsT		= std::array<T*,12>;

	using TAudioInputs		= TAudioInputsT<float>;
	using TAudioOutputs		= TAudioOutputsT<float>;
	using TAudioInputsInt	= TAudioInputsT<dsp56k::TWord>;
	using TAudioOutputsInt	= TAudioOutputsT<dsp56k::TWord>;
}

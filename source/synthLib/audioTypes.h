#pragma once

#include <array>
#include <cstdint>

namespace synthLib
{
	template<typename T> using TAudioInputsT		= std::array<const T*,4>;
	template<typename T> using TAudioOutputsT		= std::array<T*,12>;

	using TAudioInputs		= TAudioInputsT<float>;
	using TAudioOutputs		= TAudioOutputsT<float>;
	using TAudioInputsInt	= TAudioInputsT<uint32_t>;
	using TAudioOutputsInt	= TAudioOutputsT<uint32_t>;
}

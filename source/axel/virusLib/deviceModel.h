#pragma once

#include <string>

namespace virusLib
{
	enum class DeviceModel
	{
		Invalid = -1,
		A = 0,
		B = 1,
		C = 2,
		ABC = C,
		Snow = 3,
		TI = 4,
		TI2 = 5
	};

	std::string getModelName(DeviceModel _model);

	constexpr bool isTIFamily(const DeviceModel _model)
	{
		return _model == DeviceModel::Snow || _model == DeviceModel::TI || _model == DeviceModel::TI2;
	}

	constexpr bool isABCFamily(const DeviceModel _model)
	{
		return _model == DeviceModel::A || _model == DeviceModel::B || _model == DeviceModel::C;
	}
}

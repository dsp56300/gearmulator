#include "deviceModel.h"

namespace virusLib
{
	std::string getModelName(const DeviceModel _model)
	{
		switch (_model)
		{
		default:
		case DeviceModel::Invalid:	return "<invalid>";
		case DeviceModel::A:		return "A";
		case DeviceModel::B:		return "B";
		case DeviceModel::C:		return "C";
//		case DeviceModel::ABC:		return "A/B/C";
		case DeviceModel::Snow:		return "Snow";
		case DeviceModel::TI:		return "TI";
		case DeviceModel::TI2:		return "TI2";
		}
	}
}

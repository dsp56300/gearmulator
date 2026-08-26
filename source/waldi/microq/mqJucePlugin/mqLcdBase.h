#pragma once

#include <array>
#include <cstdint>

class MqLcdBase
{
public:
	virtual ~MqLcdBase() = default;

	virtual void setText(const std::array<uint8_t, 40>& _text) = 0;
	virtual void setCgRam(std::array<uint8_t, 64>& _data) = 0;
};

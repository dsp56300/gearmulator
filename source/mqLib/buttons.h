#pragma once

#include <array>
#include <cstdint>

namespace mc68k
{
	class Port;
}

namespace mqLib
{
	class Buttons
	{
	public:
		enum class ButtonType
		{
			Inst1,
			Inst2,
			Inst3,
			Inst4,
			Down,
			Left,
			Right,
			Up,

			Global,
			Multi,
			Edit,
			Sound,
			Shift,
			Multimode,
			Peek,
			Play,
			
			Power,

			Count
		};

		enum class Encoders
		{
			Matrix4,
			Matrix3,
			Matrix2,
			Matrix1,
			LcdRight,
			LcdLeft,
			Master,

			Count
		};

		Buttons();

		bool processButtons(mc68k::Port& _gp, mc68k::Port& _e, mc68k::Port& _f);

		void setButton(ButtonType _type, bool _pressed);
		void toggleButton(ButtonType _type);
		void rotate(Encoders _encoder, int _amount);

	private:
		uint8_t processEncoder(Encoders _encoder, bool cycleEncoders);
		uint8_t processStepEncoder(Encoders _encoder, bool cycleEncoders);

		std::array<uint8_t, static_cast<uint32_t>(ButtonType::Count)> m_buttonStates{};
		std::array<int, static_cast<uint32_t>(Encoders::Count)> m_remainingRotations{};
		std::array<uint8_t, static_cast<uint32_t>(Encoders::Count)> m_encoderValues{};
		uint32_t m_writeCounter = 0;
	};
}

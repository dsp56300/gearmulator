#pragma once

#include <cstdint>
#include <optional>

namespace hwLib
{
	class I2c
	{
	public:
		enum class State
		{
			Stop,
			Start,
		};
		enum BitPos : int8_t
		{
			Bit7 = 7,
			Bit6 = 6,
			Bit5 = 5,
			Bit4 = 4,
			Bit3 = 3,
			Bit2 = 2,
			Bit1 = 1,
			Bit0 = 0,
			BitAck = -1,
			BitInvalid = -2,
		};

		I2c() = default;

		virtual ~I2c() = default;

		void masterWrite(bool _sda, bool _scl);
		virtual std::optional<bool> masterRead(bool _scl);
		std::optional<bool> setSdaWrite(bool _write);

		bool sda() const { return m_sda; }
		bool scl() const { return m_scl; }
		uint8_t byte() const { return m_byte; }

	protected:
		virtual void onStateChanged(State _state);
		virtual void onStartCondition();
		virtual void onStopCondition();
		virtual void onByteWritten();
		virtual std::optional<bool> onAck() { return {}; }
		virtual uint8_t onReadByte() { return 0; }

	private:
		void sdaFlip(bool _sda);
		void sclFlip(bool _scl);
		void setState(State _state);

		bool m_sdaWrite = true;	// true = write
		bool m_sda = false;
		bool m_scl = false;
		State m_state = State::Stop;
		int8_t m_nextBit = BitInvalid;
		uint8_t m_byte;
		bool m_ackBit = true;
	};
}

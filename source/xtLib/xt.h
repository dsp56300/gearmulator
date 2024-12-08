#pragma once

#include <memory>
#include <mutex>
#include <vector>

#include <thread>
#include <atomic>

#include "xtButtons.h"
#include "xtLeds.h"
#include "xtTypes.h"

namespace synthLib
{
	struct SMidiEvent;
}

namespace xt
{
	class Hardware;

	class Xt
	{
	public:
		enum class DirtyFlags : uint32_t
		{
			None		= 0,
			Leds		= 0x01,
			Lcd			= 0x02,
		};

		Xt(const std::vector<uint8_t>& _romData, const std::string& _romName);
		~Xt();

		bool isValid() const;

		void process(const float** _inputs, float** _outputs, uint32_t _frames, uint32_t _latency = 0);

		void process(uint32_t _frames, uint32_t _latency = 0);

		TAudioInputs& getAudioInputs() const;
		TAudioOutputs& getAudioOutputs() const;

		void sendMidiEvent(const synthLib::SMidiEvent& _ev) const;
		void receiveMidi(std::vector<uint8_t>& _buffer);

		Hardware* getHardware() const;

		bool isBootCompleted() const;

		DirtyFlags getDirtyFlags();

		void readLCD(std::array<char, 80>& _lcdData) const;

		bool getLedState(LedType _led) const;
		bool getButton(ButtonType _button) const;

	private:
		void internalProcess(uint32_t _frames, uint32_t _latency);

		void processUcThread() const;

		std::unique_ptr<Hardware> m_hw;

		std::mutex m_mutex;

		std::vector<uint8_t> m_midiOutBuffer;

		std::unique_ptr<std::thread> m_ucThread;
		bool m_destroy = false;
		std::atomic<uint32_t> m_dirtyFlags = 0;
	};
}

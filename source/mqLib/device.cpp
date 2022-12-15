#include "device.h"

#include "mqmiditypes.h"

namespace mqLib
{
	enum SysexBytes
	{
		IdWaldorf = 0x3e,
		IdMicroQ = 0x3e
	};

	Device::Device() : m_mq(BootMode::Default)
	{
		while(!m_mq.isBootCompleted())
			m_mq.process(8);
	}

	Device::~Device() = default;

	uint32_t Device::getInternalLatencyMidiToOutput() const
	{
		return 0;
	}

	uint32_t Device::getInternalLatencyInputToOutput() const
	{
		return 0;
	}

	float Device::getSamplerate() const
	{
		return 44100.0f;
	}

	bool Device::isValid() const
	{
		return m_mq.isValid();
	}

	bool Device::getState(std::vector<uint8_t>& _state, synthLib::StateType _type)
	{
		// TODO
		return false;
	}

	bool Device::setState(const std::vector<uint8_t>& _state, synthLib::StateType _type)
	{
		// TODO
		return false;
	}

	uint32_t Device::getChannelCountIn()
	{
		return 2;
	}

	uint32_t Device::getChannelCountOut()
	{
		return 6;
	}

	void Device::readMidiOut(std::vector<synthLib::SMidiEvent>& _midiOut)
	{
		m_mq.receiveMidi(m_midiOutBuffer);
		m_midiOutParser.write(m_midiOutBuffer);
		m_midiOutParser.getEvents(_midiOut);
		m_midiOutBuffer.clear();
	}

	void Device::processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, const size_t _samples)
	{
		const float* inputs[2] = {_inputs[0], _inputs[1]};
		float* outputs[6] = {_outputs[0], _outputs[1], _outputs[2], _outputs[3], _outputs[4], _outputs[5]};

		m_mq.process(inputs, outputs, static_cast<uint32_t>(_samples));
	}

	bool Device::sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>& _response)
	{
		const auto& sysex = _ev.sysex;

		if(sysex.size() >= 5)
		{
			if(sysex[1] == IdWaldorf && sysex[2] == IdMicroQ)
			{
				const auto devId = sysex[3];
				const auto cmd = sysex[4];

				auto createResponse = [devId, cmd]()
				{
					std::vector<uint8_t> response{0xf0, IdWaldorf, IdMicroQ, devId, cmd};
					return response;
				};

				auto sendResponse = [&](std::vector<uint8_t>& _r)
				{
					_r.push_back(0xf7);

					synthLib::SMidiEvent ev;
					std::swap(ev.sysex, _r);

					_response.emplace_back(ev);
				};

				switch (static_cast<SysexCommand>(cmd))
				{
				case SysexCommand::EmuLCD:
					{
						std::array<char, 40> lcdData{};
						m_mq.readLCD(lcdData);

						auto response = createResponse();
						response.insert(response.end(), lcdData.begin(), lcdData.end());

						sendResponse(response);
					}
					return true;
				case SysexCommand::EmuButtons:
					{
						if(sysex.size() > 6)
						{
							const auto button = static_cast<Buttons::ButtonType>(sysex[5]);
							const auto state = sysex[6];
							m_mq.setButton(button, state != 0);
						}
						else
						{
							static_assert(static_cast<uint32_t>(Buttons::ButtonType::Count) < 24, "too many buttons");
							uint32_t buttons = 0;
							for(uint32_t i=0; i<static_cast<uint32_t>(Buttons::ButtonType::Count); ++i)
							{
								if(m_mq.getButton(static_cast<Buttons::ButtonType>(i)))
									buttons |= (1<<i);
							}
							auto response = createResponse();
							response.push_back((buttons>>16) & 0xff);
							response.push_back((buttons>>8) & 0xff);
							response.push_back(buttons & 0xff);
							sendResponse(response);
						}
					}
					return true;
				case SysexCommand::EmuLEDs:
					{
						static_assert(static_cast<uint32_t>(Leds::Led::Count) < 32, "too many LEDs");
						uint32_t leds = 0;
						for(uint32_t i=0; i<static_cast<uint32_t>(Leds::Led::Count); ++i)
						{
							if(m_mq.getLedState(static_cast<Leds::Led>(i)))
								leds |= (1<<i);
						}
						auto response = createResponse();
						response.push_back((leds>>24) & 0xff);
						response.push_back((leds>>16) & 0xff);
						response.push_back((leds>>8) & 0xff);
						response.push_back(leds & 0xff);
						sendResponse(response);
					}
					return true;
				case SysexCommand::EmuRotaries:
					{
						if(sysex.size() > 6)
						{
							const auto encoder = static_cast<Buttons::Encoders>(sysex[5]);
							const auto amount = static_cast<int>(sysex[6]) - 64;
							if(amount)
								m_mq.rotateEncoder(encoder, amount);
						}
						else
						{
							auto response = createResponse();
							for(uint32_t i=0; i<static_cast<uint32_t>(Buttons::Encoders::Count); ++i)
							{
								const auto value = m_mq.getEncoder(static_cast<Buttons::Encoders>(i));
								response.push_back(value);
							}
							sendResponse(response);
						}
					}
					return true;
				default:
					return true;
				}
			}
		}

		m_mq.sendMidiEvent(_ev);
		return true;
	}
}

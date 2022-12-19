#include "device.h"

#include "mqmiditypes.h"

#include <cstring>

namespace mqLib
{
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

		m_mq.process(inputs, outputs, static_cast<uint32_t>(_samples), getExtraLatencySamples());
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
					break;
				}
			}
		}

		m_mq.sendMidiEvent(_ev);
		return true;
	}

	void Device::createSequencerMultiData(std::vector<uint8_t>& _data)
	{
		static_assert(
			(static_cast<uint32_t>(MultiParameter::Inst15) - static_cast<uint32_t>(MultiParameter::Inst0)) == 
			(static_cast<uint32_t>(MultiParameter::Inst1 ) - static_cast<uint32_t>(MultiParameter::Inst0)) * 15, 
			"we need a consequtive offset");

		_data.assign(static_cast<uint32_t>(mqLib::MultiParameter::Count), 0);

		constexpr char name[] = "Emu-Plugin-Multi";
		static_assert(std::size(name) == 17, "wrong name length");
		memcpy(&_data[static_cast<uint32_t>(MultiParameter::Name00)], name, sizeof(name) - 1);

		auto setParam = [&](MultiParameter _param, uint8_t _value)
		{
			_data[static_cast<uint32_t>(_param)] = _value;
		};

		auto setInstParam = [&](const uint8_t _instIndex, const MultiParameter _param, const uint8_t _value)
		{
			auto index = static_cast<uint32_t>(MultiParameter::Inst0) + (static_cast<uint32_t>(MultiParameter::Inst1) - static_cast<uint32_t>(MultiParameter::Inst0)) * _instIndex;
			index += static_cast<uint32_t>(_param) - static_cast<uint32_t>(MultiParameter::Inst0);
			_data[index] = _value;
		};

		setParam(MultiParameter::Volume, 127);						// max volume

		setParam(MultiParameter::ControlW, 120);					// global
		setParam(MultiParameter::ControlX, 120);					// global
		setParam(MultiParameter::ControlY, 120);					// global
		setParam(MultiParameter::ControlZ, 120);					// global

		for(uint8_t i=0; i<16; ++i)
		{
			setInstParam(i, MultiParameter::Inst0SoundBank, 0);	    // bank A
			setInstParam(i, MultiParameter::Inst0SoundBank, i);	    // sound number i
			setInstParam(i, MultiParameter::Inst0MidiChannel, i);	// midi channel i
			setInstParam(i, MultiParameter::Inst0Volume, 127);		// max volume
			setInstParam(i, MultiParameter::Inst0Transpose, 64);	// no transpose
			setInstParam(i, MultiParameter::Inst0Detune, 64);		// no detune
			setInstParam(i, MultiParameter::Inst0Output, 0);		// main out
			setInstParam(i, MultiParameter::Inst0Flags, 3);			// RX = Local+MIDI / TX = off / Engine = Play
			setInstParam(i, MultiParameter::Inst0Pan, 64);			// center
			setInstParam(i, MultiParameter::Inst0Pattern, 0);		// no pattern
			setInstParam(i, MultiParameter::Inst0VeloLow, 0);		// full velocity range
			setInstParam(i, MultiParameter::Inst0VeloHigh, 127);
			setInstParam(i, MultiParameter::Inst0KeyLow, 0);		// full key range
			setInstParam(i, MultiParameter::Inst0KeyHigh, 127);
			setInstParam(i, MultiParameter::Inst0MidiRxFlags, 63);	// enable Pitchbend, Modwheel, Aftertouch, Sustain, Button 1/2, Program Change
		}
	}
}

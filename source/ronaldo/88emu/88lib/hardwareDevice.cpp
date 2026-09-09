#include "hardwareDevice.h"

#include "romloader.h"
#include "sc88.h"
#include "sc8850.h"
#include "sc55mk2.h"
#include "sc88pro.h"
#include "sc88Thread.h"
#include "sc88types.h"

#include <algorithm>
#include <cmath>

namespace emu88Lib
{
	namespace
	{
		constexpr float g_dacScale = 1.0f / static_cast<float>(xpLib::XP::outputFullScale);
		// A press and its release must remain visible to firmware even when a very
		// fast pointer click delivers both edges inside one host audio block.
		constexpr uint64_t g_minimumPanelEdgeSamples = 64;
	}

	HardwareDevice::HardwareDevice(const synthLib::DeviceCreateParams& _params)
		: synthLib::Device(_params)
	{
		if(!isDeviceModelValue(_params.customData))
			return;
		m_model = static_cast<DeviceModel>(_params.customData);

		switch(m_model)
		{
		case DeviceModel::Sc88Pro:
		{
			auto roms = RomLoader::findSc88ProRomSet();
			if(!roms.isValid()) break;
			std::vector<uint8_t> waves;
			waves.reserve(Sc88ProRomSet::WaveSize);
			for(const auto* chip : {&roms.waveA, &roms.waveB, &roms.waveC})
				waves.insert(waves.end(), chip->begin(), chip->end());
			m_sc88Pro = std::make_unique<Sc88Pro>(std::move(roms.firmware), waves);
			break;
		}
		case DeviceModel::Sc8850:
		{
			auto roms = RomLoader::findSc8850RomSet();
			auto waves = RomLoader::findSc8850WaveRomSet();
			if(!roms.isValid() || !waves.isValid()) break;
			m_sc8850 = std::make_unique<Sc8850>(std::move(roms.cpu), std::move(roms.program),
				std::move(roms.data), waves.romA, waves.romB);
			break;
		}
		case DeviceModel::Sc88:
		case DeviceModel::Sc88VL:
		{
			const auto model = m_model == DeviceModel::Sc88VL ? Model::Sc88VL : Model::Sc88;
			auto rom = RomLoader::findROM(model);
			auto waves = RomLoader::findWaveRom();
			if(!rom.isValid() || !waves.isValid() || rom.model() != model) break;
			m_sc88 = std::make_unique<Sc88>(rom.takeData(), waves.takeData(), model);
			break;
		}
		case DeviceModel::Sc55Mk2:
		{
			auto roms = RomLoader::findSc55RomSet();
			if(!roms.isValid()) break;
			m_sc55 = std::make_unique<Sc55Mk2>(std::move(roms));
			m_sc55->setSwitchPosition(Sc55Mk2::SwitchMidi);
			break;
		}
		}

		if(!isValid())
			return;

		m_thread = std::make_unique<Sc88Thread>(
			[this] { return renderBoardSample(); },
			[this](const synthLib::SMidiEvent& _event) { return sendMidiToBoard(_event); },
			[this](std::vector<synthLib::SMidiEvent>& _events) { readMidiOutFromBoard(_events); },
			[this] { beforeWorkerJob(); });
	}

	HardwareDevice::~HardwareDevice()
	{
		m_thread.reset();
	}

	float HardwareDevice::getSamplerate() const
	{
		if(m_sc8850) return static_cast<float>(Sc8850::SampleRate);
		if(m_sc55) return static_cast<float>(Sc55Mk2::SampleRate);
		return static_cast<float>(g_sampleRate);
	}

	uint64_t HardwareDevice::getDspClockHz() const
	{
		if(m_sc8850) return Sc8850::CpuClockHz;
		if(m_sc55) return Sc55Mk2::CpuClockHz;
		return g_cpuClockHz;
	}

	bool HardwareDevice::isValid() const
	{
		return (m_sc88 && m_sc88->isValid()) || (m_sc88Pro && m_sc88Pro->isValid()) ||
		       (m_sc8850 && m_sc8850->isValid()) || (m_sc55 && m_sc55->isValid());
	}

	void HardwareDevice::setPanelButtons(const uint32_t _buttons)
	{
		std::lock_guard lock(m_panelMutex);
		if(!m_pendingPanelCommands.empty() &&
		   m_pendingPanelCommands.back().type == PanelCommandType::Buttons &&
		   m_pendingPanelCommands.back().value == static_cast<int32_t>(_buttons))
			return;
		m_pendingPanelCommands.push_back({PanelCommandType::Buttons, static_cast<int32_t>(_buttons)});
	}

	void HardwareDevice::turnPanelEncoder(const int32_t _detents)
	{
		if(_detents == 0)
			return;
		std::lock_guard lock(m_panelMutex);
		m_pendingPanelCommands.push_back({PanelCommandType::Encoder, _detents});
	}

	HardwareDevice::DisplaySnapshot HardwareDevice::displaySnapshot() const
	{
		std::lock_guard lock(m_displayMutex);
		return m_display;
	}

	bool HardwareDevice::sendMidi(const synthLib::SMidiEvent& _event,
	                              std::vector<synthLib::SMidiEvent>&)
	{
		m_midiIn.push_back(_event);
		return true;
	}

	void HardwareDevice::sendMidiToBoard(const synthLib::SMidiEvent& _event)
	{
		if(m_sc88Pro)
			m_sc88Pro->addMidiEvent(_event, _event.port);
		else if(m_sc8850)
		{
			if(!_event.sysex.empty())
				m_sc8850->usbMidiIn(_event.port, _event.sysex.data(), _event.sysex.size());
			else
			{
				const uint8_t bytes[3] = {_event.a, _event.b, _event.c};
				m_sc8850->usbMidiIn(_event.port, bytes,
					static_cast<size_t>(synthLib::MidiBufferParser::lengthFromStatusByte(_event.a)));
			}
		}
		else if(m_sc88)
			m_sc88->addMidiEvent(_event, _event.port);
		else if(m_sc55)
		{
			// One DIN on this board: both of the SC-88's ports fold onto it.
			if(!_event.sysex.empty())
				m_sc55->sendMidiBytes(_event.sysex.data(), _event.sysex.size(), Sc55Mk2::MidiInA);
			else
			{
				const uint8_t bytes[3] = {_event.a, _event.b, _event.c};
				const auto length = synthLib::MidiBufferParser::lengthFromStatusByte(_event.a);
				if(length <= 0 || length > 3)
					return;
				m_sc55->sendMidiBytes(bytes, static_cast<size_t>(length), Sc55Mk2::MidiInA);
			}
		}
		else
			return;
	}

	void HardwareDevice::readMidiOut(std::vector<synthLib::SMidiEvent>& _midiOut)
	{
		if(_midiOut.empty())
			std::swap(_midiOut, m_midiOut);
		else
		{
			_midiOut.insert(_midiOut.end(), m_midiOut.begin(), m_midiOut.end());
			m_midiOut.clear();
		}
	}

	void HardwareDevice::readMidiOutFromBoard(std::vector<synthLib::SMidiEvent>& _midiOut)
	{
		if(m_sc88Pro)
		{
			for(size_t port = 0; port < m_sc88ProMidiOut.size(); ++port)
			{
				const auto& bytes = m_sc88Pro->serialOut(static_cast<uint8_t>(port));
				auto& offset = m_sc88ProMidiOutOffsets[port];
				if(offset > bytes.size())
				{
					// The firmware cleared its UART capture during a board reset.
					offset = 0;
					m_sc88ProMidiOut[port] = synthLib::MidiBufferParser{synthLib::MidiEventSource::Device};
				}
				for(auto i = offset; i < bytes.size(); ++i)
					m_sc88ProMidiOut[port].write(bytes[i]);
				offset = bytes.size();

				std::vector<synthLib::SMidiEvent> events;
				m_sc88ProMidiOut[port].getEvents(events);
				for(auto& event : events)
				{
					event.port = static_cast<uint8_t>(port);
					_midiOut.emplace_back(std::move(event));
				}
			}
		}
		else if(m_sc8850)
		{
			m_sc8850MidiOutBytes.clear();
			m_sc8850->readMidiOut(m_sc8850MidiOutBytes);
			m_sc8850MidiOut.write(m_sc8850MidiOutBytes);
			m_sc8850MidiOut.getEvents(_midiOut);
		}
		else if(m_sc55)
		{
			m_sc55MidiOutBytes.clear();
			m_sc55->readMidiOut(m_sc55MidiOutBytes);
			m_sc55MidiOut.write(m_sc55MidiOutBytes);
			m_sc55MidiOut.getEvents(_midiOut);
		}
		else if(m_sc88)
		{
			m_sc88MidiOut.clear();
			m_sc88->readMidiOut(m_sc88MidiOut);
			for(auto& event : m_sc88MidiOut)
			{
				_midiOut.emplace_back(std::move(event));
			}
		}
	}

	void HardwareDevice::beforeWorkerJob()
	{
		std::lock_guard lock(m_panelMutex);
		while(!m_pendingPanelCommands.empty())
		{
			m_workerPanelCommands.emplace_back(m_pendingPanelCommands.front());
			m_pendingPanelCommands.pop_front();
		}
	}

	void HardwareDevice::applyDuePanelCommand()
	{
		if(m_workerPanelCommands.empty() || m_renderedSamples < m_nextPanelCommandSample)
			return;
		const auto command = m_workerPanelCommands.front();
		m_workerPanelCommands.pop_front();
		if(command.type == PanelCommandType::Buttons)
		{
			const auto buttons = static_cast<uint32_t>(command.value);
			if(m_sc88Pro) m_sc88Pro->setButtons(buttons);
			else if(m_sc88) m_sc88->setButtons(buttons);
			else if(m_sc8850) m_sc8850->setButtons(buttons);
			else if(m_sc55) m_sc55->setButtons(buttons);
			m_nextPanelCommandSample = m_renderedSamples + g_minimumPanelEdgeSamples;
		}
		else if(m_sc8850)
		{
			m_sc8850->turnEncoder(static_cast<int8_t>(std::clamp(command.value, -64, 63)));
		}
	}

	std::pair<int32_t, int32_t> HardwareDevice::renderBoardSample()
	{
		applyDuePanelCommand();
		std::pair<int32_t, int32_t> result;
		if(m_sc8850) result = m_sc8850->renderSample();
		else if(m_sc88Pro) result = m_sc88Pro->renderSample();
		else if(m_sc88) result = m_sc88->renderSample();
		else if(m_sc55) result = m_sc55->renderSample();
		++m_renderedSamples;
		if((m_renderedSamples & 63u) == 0)
			publishDisplaySnapshot();
		return result;
	}

	void HardwareDevice::publishDisplaySnapshot()
	{
		DisplaySnapshot next;
		if(m_sc88Pro || m_sc88 || m_sc55)
		{
			const auto& lcd = m_sc88Pro ? m_sc88Pro->lcd() : m_sc88 ? m_sc88->lcd() : m_sc55->lcd();
			next.type = DisplaySnapshot::Type::Character;
			std::copy(lcd.getDdRam().begin(), lcd.getDdRam().end(), next.ddRam.begin());
			std::copy(lcd.getCgRam().begin(), lcd.getCgRam().end(), next.cgRam.begin());
			next.displayOn = lcd.isDisplayOn() && (!m_sc88 || m_sc88->lcdEnabled()) &&
			                 (!m_sc55 || m_sc55->lcdEnabled());
			next.width = 209;
			next.height = 76;
			next.leds = m_sc88Pro ? m_sc88Pro->leds()
			          : m_sc88   ? m_sc88->leds()
			          : m_sc55   ? m_sc55->leds() : 0;
		}
		else if(m_sc8850)
		{
			next.type = DisplaySnapshot::Type::Graphic;
			next.width = static_cast<uint16_t>(m_sc8850->lcd().width());
			next.height = static_cast<uint16_t>(m_sc8850->lcd().height());
			next.displayOn = m_sc8850->lcd().isDisplayEnabled();
			next.leds = m_sc8850->leds();
			m_sc8850->lcd().renderMono(next.mono);
		}
		std::lock_guard lock(m_displayMutex);
		next.revision = m_display.revision + 1;
		m_display = std::move(next);
	}

	void HardwareDevice::processAudio(const synthLib::TAudioInputs&,
	                                  const synthLib::TAudioOutputs& _outputs,
	                                  const size_t _samples)
	{
		m_thread->processSamples(static_cast<uint32_t>(_samples), getExtraLatencySamples(),
		                         m_midiIn, m_midiOut);
		for(size_t i = 0; i < _samples; ++i)
		{
			Sc88Thread::SampleFrame frame{};
			m_thread->popSample(frame);
			if(_outputs[0]) _outputs[0][i] = static_cast<float>(frame.first) * g_dacScale;
			if(_outputs[1]) _outputs[1][i] = static_cast<float>(frame.second) * g_dacScale;
		}
	}
}

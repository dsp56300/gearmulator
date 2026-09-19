#include "88lib/hardwareDevice.h"

#include "88lib/rom/romloader.h"
#include "88lib/boards/sc88.h"
#include "88lib/boards/sc8850.h"
#include "88lib/boards/sc8820.h"
#include "88lib/boards/cm32p.h"
#include "88lib/boards/cm32l.h"
#include "88lib/boards/cm64.h"
#include "88lib/boards/miig5.h"
#include "88lib/boards/nu10b.h"
#include "hardwareLib/lcdfonts.h"
#include "88lib/boards/sc55Board.h"
#include "88lib/boards/sc88pro.h"
#include "88lib/boards/sc88types.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace emu88Lib
{
	namespace
	{
		constexpr float g_dacScale = 1.0f / static_cast<float>(xpLib::XP::outputFullScale);
		// A press and its release must remain visible to firmware even when a very
		// fast pointer click delivers both edges inside one host audio block.
		constexpr uint64_t g_minimumPanelEdgeSamples = 64;
		// Longer than any supported board's power-on intro.
		constexpr float g_fastBootSeconds = 10.0f;

		using Screen = HardwareDevice::DisplaySnapshot::Screen;

		// A character panel as a dot grid: 5x8 glyphs on a 6x9 pitch, the low 16 codes taken
		// from the controller's CGRAM and the rest from the shared font. The controllers differ
		// in how a cell is addressed, so the caller supplies the two lookups.
		template<typename CharacterFn, typename CgFn>
		void renderCharacterGrid(Screen& _screen, const unsigned _columns, const unsigned _lines,
		                         const bool _displayOn, CharacterFn&& _character, CgFn&& _cgCharacter)
		{
			_screen.type = HardwareDevice::DisplaySnapshot::Type::Graphic;
			_screen.width = static_cast<uint16_t>(_columns * 6);
			_screen.height = static_cast<uint16_t>(_lines * 9);
			_screen.displayOn = _displayOn;
			_screen.mono.assign(static_cast<size_t>(_screen.width) * _screen.height, 0);
			for(unsigned line = 0; line < _lines; ++line)
				for(unsigned column = 0; column < _columns; ++column)
				{
					const auto character = _character(line, column);
					const auto custom = _cgCharacter(character & 7);
					const auto* glyph = character < 16 ? custom.data() : hwLib::getCharacterData(character);
					for(unsigned y = 0; y < 8; ++y)
						for(unsigned x = 0; x < 5; ++x)
							_screen.mono[(line * 9 + y) * _screen.width + column * 6 + x] = (glyph[y] >> (4 - x)) & 1;
				}
		}

		// The CM-32P's HD44780, 16 columns over two lines.
		void renderCm32pDisplay(Screen& _screen, const hwLib::Hd44780& _lcd)
		{
			renderCharacterGrid(_screen, 16, 2, _lcd.isDisplayOn(),
				[&](const unsigned _line, const unsigned _column) { return _lcd.getVisibleCharacter(_line, _column); },
				[&](const unsigned _index) { return _lcd.getCgCharacter(_index); });
		}

		// The CM-32L's SED1200, one strip of 20.
		void renderCm32lDisplay(Screen& _screen, const hwLib::Sed1200& _lcd)
		{
			renderCharacterGrid(_screen, 20, 1, _lcd.isDisplayOn(),
				[&](unsigned, const unsigned _column) { return _lcd.getVisibleCharacter(_column); },
				[&](const unsigned _index) { return _lcd.getCgCharacter(_index); });
		}
	}

	HardwareDevice::HardwareDevice(const synthLib::DeviceCreateParams& _params, const BootOptions& _boot,
	                               const std::vector<uint8_t>& _pcmCard)
		: synthLib::Device(_params)
	{
		if(!isDeviceModelValue(_params.customData))
			return;
		m_model = static_cast<DeviceModel>(_params.customData);
		m_dacBits = getDacBits(m_model);
		m_boardGain = getBoardOutputGain(m_model);
		const bool factoryReset = _boot.factoryReset && !_boot.initialPanelButtons;

		switch(m_model)
		{
		case DeviceModel::Sc88Pro:
		case DeviceModel::VeGsPro:
		{
			// The VE-GS Pro is the SC-88Pro board without its panel sub-MCU; the
			// board finds that out from the control ROM's vector table.
			auto roms = RomLoader::findSc88ProRomSet(RomLoader::toRomDevice(m_model));
			if(!roms.isValid()) break;
			std::vector<uint8_t> waves;
			waves.reserve(Sc88ProRomSet::WaveSize);
			for(const auto* chip : {&roms.waveA, &roms.waveB, &roms.waveC})
				waves.insert(waves.end(), chip->begin(), chip->end());
			m_sc88Pro = std::make_unique<Sc88Pro>(std::move(roms.firmware), waves, factoryReset);
			break;
		}
		case DeviceModel::Cm32p:
			m_cm32p = std::make_unique<Cm32p>(RomLoader::findCm32pRomSet(), _pcmCard);
			break;
		case DeviceModel::Cm32l:
			m_cm32l = std::make_unique<Cm32l>(RomLoader::findCm32lRomSet());
			break;
		case DeviceModel::Cm64:
			m_cm64 = std::make_unique<Cm64>(RomLoader::findCm32lRomSet(), RomLoader::findCm32pRomSet(), _pcmCard);
			break;
		case DeviceModel::Sc8820:
		{
			const auto inventory = RomLoader::scan();
			std::vector<uint8_t> cpu, program, wave0, wave1;
			if(!inventory.read(cpu, RomDevice::Sc8820, RomSlot::Internal) ||
				!inventory.read(program, RomDevice::Sc8820, RomSlot::Program) ||
				!inventory.read(wave0, RomDevice::Sc8820, RomSlot::Wave, 0) ||
				!inventory.read(wave1, RomDevice::Sc8820, RomSlot::Wave, 1)) break;
			m_sc8820 = std::make_unique<Sc8820>(cpu, program, std::move(wave0), std::move(wave1));
			break;
		}
		case DeviceModel::Sc8850:
		{
			auto roms = RomLoader::findSc8850RomSet();
			auto waves = RomLoader::findSc8850WaveRomSet();
			if(!roms.isValid() || !waves.isValid()) break;
			m_sc8850 = std::make_unique<Sc8850>(std::move(roms.cpu), std::move(roms.program),
				std::move(roms.data), waves.romA, waves.romB, factoryReset);
			break;
		}
		case DeviceModel::Sc88:
		case DeviceModel::Sc88VL:
		case DeviceModel::Xpgs:
		{
			const auto model = m_model == DeviceModel::Sc88VL ? Model::Sc88VL : m_model == DeviceModel::Xpgs ? Model::Xpgs : Model::Sc88;
			auto rom = RomLoader::findROM(model);
			if(!rom.isValid() || rom.model() != model) break;
			auto waveData = m_model == DeviceModel::Xpgs
				? RomLoader::findXpgsWaveRom() : RomLoader::findWaveRom().takeData();
			if(waveData.empty()) break;
			m_sc88 = std::make_unique<Sc88>(rom.takeData(), std::move(waveData), model, factoryReset);
			break;
		}
		case DeviceModel::Sc55Mk2:
		case DeviceModel::Sc55Mk1:
		case DeviceModel::Sc55St:
		case DeviceModel::Cm300:
		case DeviceModel::Scc1a:
		case DeviceModel::Scb55:
		case DeviceModel::Rlp3237:
		case DeviceModel::Sc155:
		case DeviceModel::Sc155Mk2:
		{
			auto roms = RomLoader::findSc55RomSet(m_model);
			if(!roms.isValid()) break;
			m_sc55 = std::make_unique<Sc55Board>(std::move(roms), factoryReset);
			m_sc55->setSwitchPosition(Sc55Board::SwitchMidi);
			break;
		}
		case DeviceModel::Nu10b:
		{
			const auto inventory = RomLoader::scan();
			std::vector<uint8_t> cpu, program;
			std::array<std::vector<uint8_t>, WaveRom::ChipCount> chips;
			if(!inventory.read(cpu, RomDevice::Nu10b, RomSlot::Internal) ||
				!inventory.read(program, RomDevice::Nu10b, RomSlot::Program)) break;
			bool waves = true;
			for(uint8_t chip = 0; chip < chips.size(); ++chip)
				waves = waves && inventory.read(chips[chip], RomDevice::Nu10b, RomSlot::Wave, chip);
			if(!waves) break;
			// Four 2 MiB chips wired like the SC-88's, so the same de-scramble applies.
			m_nu10b = std::make_unique<Nu10b>(cpu, program, WaveRom(chips).takeData(), factoryReset);
			break;
		}
		case DeviceModel::Miig5:
		{
			const auto inventory = RomLoader::scan();
			std::vector<uint8_t> cpu, program, wave;
			if(!inventory.read(cpu, RomDevice::Miig5, RomSlot::Internal) ||
				!inventory.read(program, RomDevice::Miig5, RomSlot::Program) ||
				!inventory.read(wave, RomDevice::Miig5, RomSlot::Wave)) break;
			m_miig5 = std::make_unique<Miig5>(cpu, program, std::move(wave), factoryReset);
			break;
		}
		}

		if(!isValid())
			return;

		if(m_sc55)
		{
			// Pace host bursts before the board's finite serial arrival buffer.
			// Retain events here so transport jumps can cancel obsolete traffic.
			m_sc55MidiIn = std::make_unique<synthLib::MidiRateLimiter>(
				[this](const uint8_t _byte) { m_sc55->sendMidiByte(_byte); });
			m_sc55MidiIn->setSamplerate(static_cast<float>(m_sc55->sampleRate()));
			m_sc55MidiIn->setDefaultRateLimit();
			m_sc55MidiIn->setPreserveEventOrder(true);
			m_sc55MidiIn->setResetPause(0.05f);
		}

		if(_boot.initialPanelButtons)
		{
			if(m_sc88Pro) m_sc88Pro->setButtons(_boot.initialPanelButtons);
			else if(m_sc88) m_sc88->setButtons(_boot.initialPanelButtons);
			else if(m_sc8850) m_sc8850->setButtons(_boot.initialPanelButtons);
			else if(m_sc55) m_sc55->setButtons(_boot.initialPanelButtons);
			else if(m_cm32l) m_cm32l->setButtons(_boot.initialPanelButtons);
			else if(m_cm64) m_cm64->setButtons(_boot.initialPanelButtons);
		}

		if(_boot.fastBoot && !_boot.initialPanelButtons)
		{
			// What the board sends out meanwhile is dropped; its display is published so the panel
			// starts on the screen the board is now showing.
			for(auto samples = static_cast<uint64_t>(g_fastBootSeconds * dacSamplerate()); samples > 0; --samples)
				renderBoardFrame();
			std::vector<synthLib::SMidiEvent> discarded;
			readMidiOutFromBoard(discarded);
			publishDisplaySnapshot();
		}
	}

	HardwareDevice::~HardwareDevice() = default;

	bool HardwareDevice::isPcmCardImage(const std::vector<uint8_t>& _image)
	{
		return !Cm32p::decodeCard(_image).empty();
	}

	float HardwareDevice::getSamplerate() const
	{
		return dacSamplerate() * static_cast<float>(m_analogOutput.oversampling());
	}

	void HardwareDevice::getSupportedSamplerates(std::vector<float>& _dst) const
	{
		_dst.push_back(dacSamplerate() * static_cast<float>(getAnalogOversampling(m_selectedAnalogModel)));
	}

	bool HardwareDevice::setSamplerate(const float _samplerate)
	{
		if(!isSamplerateSupported(_samplerate))
			return false;
		if(m_analogOutput.model() != m_selectedAnalogModel)
			activateAnalogModel(m_selectedAnalogModel);
		return true;
	}

	void HardwareDevice::setAnalogOutputMode(const AnalogOutputMode _mode)
	{
		m_selectedAnalogModel = resolveAnalogModel(_mode, m_model);
		if(m_selectedAnalogModel != m_analogOutput.model() &&
		   getAnalogOversampling(m_selectedAnalogModel) == m_analogOutput.oversampling())
			activateAnalogModel(m_selectedAnalogModel);
	}

	void HardwareDevice::activateAnalogModel(const AnalogModel _model)
	{
		m_analogOutput.setModel(_model, dacSamplerate());
		m_holdPhase = 0;
	}

	float HardwareDevice::dacSamplerate() const
	{
		if(m_sc8850) return static_cast<float>(Sc8850::SampleRate);
		if(m_sc8820) return static_cast<float>(Sc8820::SampleRate);
		if(m_cm32p) return static_cast<float>(Cm32p::SampleRate);
		if(m_cm32l) return static_cast<float>(Cm32l::SampleRate);
		if(m_cm64) return static_cast<float>(Cm64::SampleRate);
		if(m_sc55) return static_cast<float>(m_sc55->sampleRate());
		if(m_nu10b) return static_cast<float>(Nu10b::SampleRate);
		if(m_miig5) return static_cast<float>(Miig5::SampleRate);
		return static_cast<float>(g_sampleRate);
	}

	uint64_t HardwareDevice::getDspClockHz() const
	{
		if(m_sc8850) return Sc8850::CpuClockHz;
		if(m_sc8820) return Sc8820::CpuClockHz;
		if(m_cm32p) return Cm32p::CpuStateRate * 3;
		if(m_cm32l) return Cm32l::CpuClock;
		if(m_cm64) return Cm32l::CpuClock;
		if(m_sc55) return m_sc55->cpuClockHz();
		if(m_nu10b) return Nu10b::CpuClockHz;
		if(m_miig5) return Miig5::CpuClockHz;
		return g_cpuClockHz;
	}

	bool HardwareDevice::isValid() const
	{
		return (m_sc88 && m_sc88->isValid()) || (m_sc88Pro && m_sc88Pro->isValid()) ||
		       (m_sc8850 && m_sc8850->isValid()) || (m_sc8820 && m_sc8820->isValid()) || (m_cm32p && m_cm32p->isValid()) ||
		       (m_cm32l && m_cm32l->isValid()) || (m_cm64 && m_cm64->isValid()) ||
		       (m_sc55 && m_sc55->isValid()) || (m_nu10b && m_nu10b->isValid()) || (m_miig5 && m_miig5->isValid());
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
		else if(m_sc8820)
			m_sc8820->addMidiEvent(_event, _event.port);
		else if(m_cm32p)
			m_cm32p->addMidiEvent(_event, _event.port);
		else if(m_cm32l)
			m_cm32l->addMidiEvent(_event, _event.port);
		else if(m_cm64)
			m_cm64->addMidiEvent(_event, _event.port);
		else if(m_sc88)
			m_sc88->addMidiEvent(_event, _event.port);
		else if(m_sc55MidiIn)
			m_sc55MidiIn->write(synthLib::SMidiEvent(_event));
		else if(m_nu10b)
			m_nu10b->addMidiEvent(_event);
		else if(m_miig5)
			m_miig5->addMidiEvent(_event);
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
			m_sc88Pro->readMidiOut(_midiOut);
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
			m_sc8850->readMidiOut(_midiOut);
		else if(m_sc8820)
			m_sc8820->readMidiOut(_midiOut);
		else if(m_cm32p)
			m_cm32p->readMidiOut(_midiOut);
		else if(m_cm32l)
			m_cm32l->readMidiOut(_midiOut);
		else if(m_cm64)
			m_cm64->readMidiOut(_midiOut);
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
		else if(m_nu10b)
			m_nu10b->readMidiOut(_midiOut);
		else if(m_miig5)
			m_miig5->readMidiOut(_midiOut);
	}

	void HardwareDevice::collectPanelCommands()
	{
		std::unique_lock lock(m_panelMutex, std::try_to_lock);
		if(!lock.owns_lock())
			return; // Leave pending input for the next audio block.
		while(!m_pendingPanelCommands.empty())
		{
			m_panelCommands.emplace_back(m_pendingPanelCommands.front());
			m_pendingPanelCommands.pop_front();
		}
	}

	void HardwareDevice::applyDuePanelCommand()
	{
		if(m_panelCommands.empty() || m_renderedSamples < m_nextPanelCommandSample)
			return;
		const auto command = m_panelCommands.front();
		m_panelCommands.pop_front();
		if(command.type == PanelCommandType::Buttons)
		{
			const auto buttons = static_cast<uint32_t>(command.value);
			if(m_sc88Pro) m_sc88Pro->setButtons(buttons);
			else if(m_sc88) m_sc88->setButtons(buttons);
			else if(m_sc8850) m_sc8850->setButtons(buttons);
			else if(m_sc55) m_sc55->setButtons(buttons);
			else if(m_cm32l) m_cm32l->setButtons(buttons);
			else if(m_cm64) m_cm64->setButtons(buttons);
			m_nextPanelCommandSample = m_renderedSamples + g_minimumPanelEdgeSamples;
		}
		else if(m_sc8850)
		{
			m_sc8850->turnEncoder(static_cast<int8_t>(std::clamp(command.value, -64, 63)));
		}
	}

	void HardwareDevice::renderBoardFrame()
	{
		if(m_sc55MidiIn)
			m_sc55MidiIn->processSample();
		applyDuePanelCommand();
		m_heldFrame = {};
		m_heldFrameB = {};
		if(m_sc8850) m_heldFrame = m_sc8850->renderSample();
		else if(m_sc8820) m_heldFrame = m_sc8820->renderSample();
		else if(m_cm32p) m_heldFrame = m_cm32p->renderSample();
		else if(m_cm32l) m_heldFrame = m_cm32l->renderSample();
		else if(m_cm64)
		{
			// The LA board's line output and the PCM board's, as they arrive at the mixer.
			const auto frames = m_cm64->renderFrames();
			m_heldFrame = frames.la;
			m_heldFrameB = frames.pcm;
		}
		else if(m_sc88Pro) m_heldFrame = m_sc88Pro->renderSample();
		else if(m_sc88) m_heldFrame = m_sc88->renderSample();
		else if(m_sc55) m_heldFrame = m_sc55->renderSample();
		else if(m_nu10b) m_heldFrame = m_nu10b->renderSample();
		else if(m_miig5) m_heldFrame = m_miig5->renderSample();
		++m_renderedSamples;
	}

	void HardwareDevice::publishDisplaySnapshot()
	{
		DisplaySnapshot next;
		auto& first = next.screens[0];
		if(deviceHasLcd(m_model) && (m_sc88Pro || m_sc88 || (m_sc55 && m_sc55->hasDisplay())))
		{
			const auto& lcd = m_sc88Pro ? m_sc88Pro->lcd() : m_sc88 ? m_sc88->lcd() : m_sc55->lcd();
			first.type = DisplaySnapshot::Type::Character;
			std::copy(lcd.getDdRam().begin(), lcd.getDdRam().end(), first.ddRam.begin());
			std::copy(lcd.getCgRam().begin(), lcd.getCgRam().end(), first.cgRam.begin());
			first.powered = (!m_sc88Pro || m_sc88Pro->lcdEnabled()) && (!m_sc88 || m_sc88->lcdEnabled()) &&
			                (!m_sc55 || m_sc55->lcdEnabled());
			first.displayOn = lcd.isDisplayOn() && first.powered;
			first.width = 209;
			first.height = 76;
			next.leds = m_sc88Pro ? m_sc88Pro->leds()
			          : m_sc88   ? m_sc88->leds()
			          : m_sc55   ? m_sc55->leds() : 0;
		}
		else if(m_cm32p)
		{
			renderCm32pDisplay(first, m_cm32p->lcd());
			next.leds = m_cm32p->leds();
		}
		else if(m_cm32l)
		{
			renderCm32lDisplay(first, m_cm32l->lcd());
			next.leds = m_cm32l->leds();
		}
		else if(m_cm64)
		{
			// Two boards, two service displays: the PCM half's above the LA half's, as the
			// bezel has them.
			renderCm32pDisplay(first, m_cm64->pcm().lcd());
			renderCm32lDisplay(next.screens[1], m_cm64->la().lcd());
			next.leds = m_cm64->leds();
		}
		else if(m_sc8850)
		{
			first.type = DisplaySnapshot::Type::Graphic;
			first.width = static_cast<uint16_t>(m_sc8850->lcd().width());
			first.height = static_cast<uint16_t>(m_sc8850->lcd().height());
			first.displayOn = m_sc8850->lcd().isDisplayEnabled();
			next.leds = m_sc8850->leds();
			m_sc8850->lcd().renderMono(first.mono);
		}
		std::unique_lock lock(m_displayMutex, std::try_to_lock);
		if(!lock.owns_lock())
			return; // The UI can keep its previous snapshot if it is busy.
		next.revision = m_display.revision + 1;
		m_display = std::move(next);
	}

	void HardwareDevice::processAudio(const synthLib::TAudioInputs&,
	                                  const synthLib::TAudioOutputs& _outputs,
	                                  const size_t _samples)
	{
		collectPanelCommands();
		std::stable_sort(m_midiIn.begin(), m_midiIn.end(), [](const auto& a, const auto& b) { return a.offset < b.offset; });
		size_t next = 0;
		for(size_t i = 0; i < _samples; ++i)
		{
			while(next < m_midiIn.size() && m_midiIn[next].offset <= i)
			{
				const auto& event = m_midiIn[next++];
				if(event.type == synthLib::MidiEventType::TransportDiscontinuity)
					handleTransportDiscontinuity(event.transportGeneration);
				else if(!synthLib::isTransportBound(event) || event.transportGeneration >= m_transportGeneration)
				{
					sendMidiToBoard(event);
					trackMidiActivity(event);
				}
			}
			// With an analogue model each DAC frame is held for several output samples.
			if(m_holdPhase == 0)
			{
				if(isValid()) renderBoardFrame();
				else m_heldFrame = m_heldFrameB = {};
				const auto firstOutput = m_midiOut.size();
				readMidiOutFromBoard(m_midiOut);
				for(auto j = firstOutput; j < m_midiOut.size(); ++j)
					m_midiOut[j].offset = static_cast<uint32_t>(i);
			}
			writeOutputSample(_outputs, i);
		}
		m_midiIn.erase(m_midiIn.begin(), m_midiIn.begin() + static_cast<ptrdiff_t>(next));
		for(auto& event : m_midiIn) event.offset -= static_cast<uint32_t>(_samples);
		if(_samples && isValid()) publishDisplaySnapshot();
	}

	void HardwareDevice::writeOutputSample(const synthLib::TAudioOutputs& _outputs, const size_t _index)
	{
		const auto word = [this](const int32_t _v)
		{
			return static_cast<float>(synthLib::quantiseDacWord(_v, m_dacBits)) * g_dacScale;
		};
		// The board's amplifiers are linear and sit after the filters, so scaling here rather
		// than at the end of the chain comes to the same thing - and keeps the level right when
		// there is no chain at all.
		auto left = word(m_heldFrame.first) * m_boardGain.a;
		auto right = word(m_heldFrame.second) * m_boardGain.a;
		// The CM-64 is two boards that meet at one mixer, so its halves are summed here rather
		// than on the board: each passes its own circuit first.
		if(m_cm64)
			m_analogOutput.processSplit(left, right, word(m_heldFrameB.first) * m_boardGain.b,
			                            word(m_heldFrameB.second) * m_boardGain.b);
		else
			m_analogOutput.process(left, right);
		if(_outputs[0]) _outputs[0][_index] = left;
		if(_outputs[1]) _outputs[1][_index] = right;
		if(++m_holdPhase == m_analogOutput.oversampling())
			m_holdPhase = 0;
	}

	void HardwareDevice::handleTransportDiscontinuity(uint32_t generation)
	{
		m_transportGeneration = std::max(m_transportGeneration, generation);
		if(m_sc55MidiIn) m_sc55MidiIn->transportDiscontinuity(m_transportGeneration);
		if(m_sc88) m_sc88->transportDiscontinuity(m_transportGeneration);
		if(m_sc8820) m_sc8820->transportDiscontinuity(m_transportGeneration);
		if(m_cm32p) m_cm32p->transportDiscontinuity(m_transportGeneration);
		if(m_cm32l) m_cm32l->transportDiscontinuity(m_transportGeneration);
		if(m_cm64) m_cm64->transportDiscontinuity(m_transportGeneration);
		if(m_nu10b) m_nu10b->transportDiscontinuity(m_transportGeneration);
		if(m_miig5) m_miig5->transportDiscontinuity(m_transportGeneration);
		silenceActiveChannels();
	}

	void HardwareDevice::silenceActiveChannels()
	{
		for(int port = 3; port >= 0; --port)
		{
			for(int channel = 15; channel >= 0; --channel)
			{
				const auto channelBit = static_cast<uint8_t>(port * 16 + channel);
				if((m_activeChannels & (uint64_t{1} << channelBit)) == 0)
					continue;
				synthLib::SMidiEvent event(synthLib::MidiEventSource::Internal,
					static_cast<uint8_t>(synthLib::M_CONTROLCHANGE | channel), synthLib::MC_ALLSOUNDOFF, 0);
				event.port = static_cast<uint8_t>(port);
				event.transportGeneration = m_transportGeneration;
				sendMidiToBoard(event);
			}
		}
		m_activeChannels = 0;
	}

	void HardwareDevice::trackMidiActivity(const synthLib::SMidiEvent& _event)
	{
		if(!synthLib::isTransportBound(_event) || _event.a < 0x80 || _event.a >= 0xf0)
			return;
		const auto status = _event.a & 0xf0;
		if(_event.port >= 4)
			return;
		const auto channelBit = static_cast<uint8_t>(_event.port * 16 + (_event.a & 0x0f));
		const auto channelMask = uint64_t{1} << channelBit;
		if(status == synthLib::M_NOTEON && _event.c != 0)
			m_activeChannels |= channelMask;
		else if(status == synthLib::M_CONTROLCHANGE &&
		        _event.b == synthLib::MC_ALLSOUNDOFF)
			m_activeChannels &= ~channelMask;
	}
}

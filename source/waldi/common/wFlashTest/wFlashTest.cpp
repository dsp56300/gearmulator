// The microQ and the XT keep their sounds in flash, the microQ its global parameters too, as journals of changes. This
// test stores singles through MIDI until the firmware compacted the journal, which erases sectors, then boots again
// from the flash image the firmware wrote and compares what it reads with what it read before the reboot. That runs
// the firmware's own erase and program sequences against hwLib::Am29f.
//
// usage: mqFlashTest|xtFlashTest <ROM image or OS update .mid> [folder to save the flash images to]
//
// An OS update leaves the flash data area empty, the first boot then formats it. Both machines define the Musashi
// callbacks, so they cannot share an executable, FLASHTEST_MQ or FLASHTEST_XT selects one.

#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "baseLib/filesystem.h"
#include "baseLib/os.h"

#include "synthLib/midiTypes.h"

#ifdef FLASHTEST_MQ
#include "mqLib/microq.h"
#include "mqLib/mqhardware.h"
#include "mqLib/mqstate.h"
#endif

#ifdef FLASHTEST_XT
#include "xtLib/xt.h"
#include "xtLib/xtHardware.h"
#include "xtLib/xtState.h"
#include "xtLib/xtUc.h"
#endif

namespace
{
	using SysEx = synthLib::SysexBuffer;

	constexpr uint32_t g_blockSize = 64;
	constexpr uint32_t g_blocksPerSecond = 44100 / g_blockSize;	// close enough for the 40 kHz of the XT

	// the XT compacts after about 40, the microQ image of OS 2.23 after about 280
	constexpr uint32_t g_stores = 300;

	constexpr uint8_t g_globalRequest = 0x04;
	constexpr uint8_t g_globalDump = 0x14;
	constexpr uint8_t g_globalParameterChange = 0x24;

#ifdef FLASHTEST_MQ
	struct Machine
	{
		using Device = mqLib::MicroQ;

		static constexpr uint8_t Id = mqLib::IdMicroQ;
		static constexpr uint8_t SingleRequest = static_cast<uint8_t>(mqLib::SysexCommand::SingleRequest);
		static constexpr uint8_t SingleDump = static_cast<uint8_t>(mqLib::SysexCommand::SingleDump);
		// OS 2.23 does not answer a request for the banks at $40
		static constexpr uint8_t FirstBank = static_cast<uint8_t>(mqLib::MidiBufferNum::DeprecatedSingleBankA);
		static constexpr uint8_t LastBank = static_cast<uint8_t>(mqLib::MidiBufferNum::DeprecatedSingleBankC);
		static constexpr uint8_t LastProgram = 99;
		static constexpr uint8_t GlobalTune = static_cast<uint8_t>(mqLib::GlobalParameter::Tuning);
		static constexpr bool GlobalsInFlash = true;

		static std::unique_ptr<Device> create(const std::vector<uint8_t>& _rom, const std::string& _name)
		{
			return std::make_unique<Device>(mqLib::BootMode::Default, _rom, _name);
		}

		static std::vector<uint8_t> flash(Device& _device)
		{
			const auto& f = _device.getHardware()->getUC().getRomRuntimeData();
			return {f.begin(), f.end()};
		}

		static bool rename(SysEx& _single, const std::string& _name)
		{
			return mqLib::State::setSingleName(_single, _name) && mqLib::State::updateChecksum(_single);
		}
	};
#endif

#ifdef FLASHTEST_XT
	struct Machine
	{
		using Device = xt::Xt;

		static constexpr uint8_t Id = xt::IdMw2;
		static constexpr uint8_t SingleRequest = static_cast<uint8_t>(xt::SysexCommand::SingleRequest);
		static constexpr uint8_t SingleDump = static_cast<uint8_t>(xt::SysexCommand::SingleDump);
		static constexpr uint8_t FirstBank = static_cast<uint8_t>(xt::LocationH::SingleBankA);
		static constexpr uint8_t LastBank = static_cast<uint8_t>(xt::LocationH::SingleBankB);
		static constexpr uint8_t LastProgram = 127;
		static constexpr uint8_t GlobalTune = static_cast<uint8_t>(xt::GlobalParameter::MasterTune);
		static constexpr bool GlobalsInFlash = false;	// a change through MIDI is not written within two minutes

		static std::unique_ptr<Device> create(const std::vector<uint8_t>& _rom, const std::string& _name)
		{
			return std::make_unique<Device>(_rom, _name);
		}

		static std::vector<uint8_t> flash(Device& _device)
		{
			const auto& f = _device.getHardware()->getUC().getRomRuntimeData();
			return {f.begin(), f.end()};
		}

		static bool rename(SysEx& _single, const std::string& _name)
		{
			return xt::State::setSingleName(_single, _name) && xt::State::updateChecksum(_single, xt::IdxSingleChecksumStart);
		}
	};
#endif

	void check(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	void checkEqual(const SysEx& _expected, const SysEx& _actual, const std::string& _what)
	{
		if(_expected == _actual)
			return;

		if(_expected.size() != _actual.size())
			throw std::runtime_error(_what + ": size " + std::to_string(_actual.size()) + " instead of " + std::to_string(_expected.size()));

		size_t i = 0;
		while(_expected[i] == _actual[i])
			++i;

		throw std::runtime_error(_what + ": byte " + std::to_string(i) + " is " + std::to_string(_actual[i]) + " instead of " + std::to_string(_expected[i]));
	}

	size_t hash(const std::vector<uint8_t>& _data)
	{
		return std::hash<std::string_view>()(std::string_view(reinterpret_cast<const char*>(_data.data()), _data.size()));
	}

	// changed bytes, as ranges that are merged when less than 4 KB apart
	void printChanges(const std::vector<uint8_t>& _before, const std::vector<uint8_t>& _after)
	{
		size_t count = 0;
		size_t begin = 0;
		size_t end = 0;

		auto print = [&]
		{
			if(end > begin)
				std::cout << "  $" << std::hex << begin << "-$" << (end - 1) << std::dec << '\n';
		};

		for(size_t i=0; i<_before.size(); ++i)
		{
			if(_before[i] == _after[i])
				continue;
			++count;
			if(end <= begin || i - end >= 4096)
			{
				print();
				begin = i;
			}
			end = i + 1;
		}
		print();

		std::cout << "  " << count << " bytes changed, flash hash " << std::hex << hash(_after) << std::dec << std::endl;
	}

	template<typename M> class Session
	{
	public:
		Session(const std::vector<uint8_t>& _rom, const std::string& _name) : m_device(M::create(_rom, _name))
		{
		}

		// processing returns at once until the firmware started the DSP, so this timeout is wall clock time
		void boot()
		{
			const auto end = std::chrono::steady_clock::now() + std::chrono::minutes(5);

			while(m_device->isValid() && !m_device->isBootCompleted() && std::chrono::steady_clock::now() < end)
				process(1);

			check(m_device->isBootCompleted(), "the firmware did not boot within five minutes");

			waitFlashIdle();
		}

		void process(const uint32_t _blocks)
		{
			for(uint32_t b=0; b<_blocks; ++b)
			{
				m_device->process(g_blockSize);
				m_device->receiveMidi(m_midiOut);

				for(const auto byte : m_midiOut)
				{
					if(byte >= 0xf8)	// real time messages may sit in the middle of a sysex
						continue;
					if(byte == 0xf0)
						m_sysex.clear();
					m_sysex.push_back(byte);
					if(byte == 0xf7)
					{
						m_received.push_back(m_sysex);
						m_sysex.clear();
					}
				}
			}
		}

		void send(const SysEx& _sysex) const
		{
			synthLib::SMidiEvent ev(synthLib::MidiEventSource::Host);
			ev.sysex = _sysex;
			m_device->sendMidiEvent(ev);
		}

		// the firmware ignores requests for a while after it booted, the request is repeated until it answers
		SysEx request(const SysEx& _request, const std::function<bool(const SysEx&)>& _isAnswer)
		{
			for(uint32_t attempt=0; attempt<30; ++attempt)
			{
				send(_request);

				for(uint32_t i=0; i<g_blocksPerSecond * 2; ++i)
				{
					process(1);

					for(const auto& s : m_received)
					{
						if(s.size() > 5 && s[2] == M::Id && _isAnswer(s))
							return s;
					}
					m_received.clear();
				}
			}
			throw std::runtime_error("no answer to a request for command " + std::to_string(_request[4]));
		}

		SysEx requestSingle(const uint8_t _bank, const uint8_t _program)
		{
			return request({0xf0, wLib::IdWaldorf, M::Id, wLib::IdDeviceOmni, M::SingleRequest, _bank, _program, 0xf7}, [&](const SysEx& _s)
			{
				return _s.size() > 7 && _s[4] == M::SingleDump && _s[5] == _bank && _s[6] == _program;
			});
		}

		SysEx requestGlobals()
		{
			return request({0xf0, wLib::IdWaldorf, M::Id, wLib::IdDeviceOmni, g_globalRequest, 0xf7}, [](const SysEx& _s)
			{
				return _s[4] == g_globalDump;
			});
		}

		// returns the seconds until the flash changed
		uint32_t waitFlashChange(const uint32_t _timeoutSeconds)
		{
			const auto before = flash();

			for(uint32_t i=0; i<_timeoutSeconds * 2; ++i)
			{
				process(g_blocksPerSecond / 2);

				if(flash() != before)
					return i / 2;
			}
			throw std::runtime_error("the flash did not change within " + std::to_string(_timeoutSeconds) + " seconds");
		}

		// runs until the flash did not change for five seconds
		void waitFlashIdle()
		{
			auto last = flash();
			uint32_t idle = 0;

			for(uint32_t i=0; i<240; ++i)
			{
				process(g_blocksPerSecond / 2);

				auto f = flash();

				if(f != last)
				{
					idle = 0;
					last = std::move(f);
				}
				else if(++idle == 10)
				{
					return;
				}
			}
			throw std::runtime_error("the flash is still changing after two minutes");
		}

		std::vector<uint8_t> flash() const { return M::flash(*m_device); }

	private:
		std::unique_ptr<typename M::Device> m_device;
		std::vector<uint8_t> m_midiOut;
		SysEx m_sysex;
		std::vector<SysEx> m_received;
	};

	template<typename M> void run(const std::string& _romFile, const std::string& _saveFolder)
	{
		auto save = [&](const std::string& _name, const std::vector<uint8_t>& _flash)
		{
			if(!_saveFolder.empty())
				baseLib::filesystem::writeFile(_saveFolder + '/' + _name + ".bin", _flash);
		};

		std::vector<uint8_t> rom;
		check(baseLib::filesystem::readFile(rom, _romFile), "unable to read " + _romFile);

		// program 0 of the first bank receives most of the stores. The whole first bank and the last single of the last
		// bank are compared after the reboot
		std::vector<std::pair<uint8_t, uint8_t>> slots;
		for(uint32_t p=0; p<=M::LastProgram; ++p)
			slots.emplace_back(M::FirstBank, static_cast<uint8_t>(p));
		slots.emplace_back(M::LastBank, M::LastProgram);

		std::vector<SysEx> singles;
		SysEx globals;
		SysEx first;
		SysEx last;
		std::vector<uint8_t> flashChanged;
		{
			Session<M> s(rom, _romFile);
			s.boot();

			std::cout << "booted " << _romFile << std::endl;

			const auto flashBooted = s.flash();
			save("booted", flashBooted);

			// stores a single under another program number, with a new name. The answer to the next request paces the
			// stores
			auto store = [&](const uint8_t _bank, const uint8_t _source, const uint8_t _target)
			{
				auto single = s.requestSingle(_bank, _source);
				single[6] = _target;
				check(M::rename(single, "Flash Test " + std::to_string(_source)), "unexpected single dump size " + std::to_string(single.size()));
				s.send(single);
				return single;
			};

			last = store(M::LastBank, M::LastProgram, M::LastProgram);
			std::cout << "store written after " << s.waitFlashChange(60) << " s" << std::endl;

			// the firmware journals the changes to a single. Copying other singles to program 0 fills the journal until
			// the firmware compacts it, which erases sectors
			for(uint32_t i=0; i<g_stores; ++i)
				first = store(M::FirstBank, static_cast<uint8_t>(1 + i % M::LastProgram), 0);

			s.waitFlashIdle();

			std::cout << g_stores + 1 << " stores" << std::endl;

			// global parameters have a journal of their own
			if(M::GlobalsInFlash)
			{
				const uint8_t tune = s.requestGlobals()[wLib::IdxBuffer + M::GlobalTune] ^ 1;
				s.send({0xf0, wLib::IdWaldorf, M::Id, wLib::IdDeviceOmni, g_globalParameterChange, 0, M::GlobalTune, tune, 0xf7});
				std::cout << "global parameter change written after " << s.waitFlashChange(120) << " s" << std::endl;
				s.waitFlashIdle();
			}

			flashChanged = s.flash();
			save("changed", flashChanged);
			printChanges(flashBooted, flashChanged);

			for(const auto& [bank, program] : slots)
				singles.push_back(s.requestSingle(bank, program));
			if(M::GlobalsInFlash)
				globals = s.requestGlobals();
		}

		Session<M> s(flashChanged, "flash");
		s.boot();

		for(size_t i=0; i<slots.size(); ++i)
		{
			checkEqual(singles[i], s.requestSingle(slots[i].first, slots[i].second),
				"bank " + std::to_string(slots[i].first) + " program " + std::to_string(slots[i].second) + " after the reboot");
		}
		if(M::GlobalsInFlash)
			checkEqual(globals, s.requestGlobals(), "the global parameters after the reboot");

		const auto flashRebooted = s.flash();
		save("rebooted", flashRebooted);

		std::cout << "reboot\n";
		printChanges(flashChanged, flashRebooted);

		// checked last, so that a failure here does not hide the result of the reboot. The microQ OS 2.23 fails it once
		// it compacted its journal: from then on a stored single takes effect when the next one arrives, and the last
		// one never does
		checkEqual(first, singles.front(), "the last store to program 0 of the first bank");
		checkEqual(last, singles.back(), "the store to the last program of the last bank");
	}
}

int main(const int _argc, char* _argv[])
{
	baseLib::disableErrorDialogs();

	if(_argc != 2 && _argc != 3)
	{
		std::cout << "usage: " << _argv[0] << " <ROM image or OS update .mid> [folder to save the flash images to]" << std::endl;
		return 2;
	}

	try
	{
		run<Machine>(_argv[1], _argc == 3 ? _argv[2] : "");
		std::cout << "PASSED" << std::endl;
		return 0;
	}
	catch(const std::exception& _e)
	{
		std::cout << "FAILED: " << _e.what() << std::endl;
		return 1;
	}
}

#include "device.h"
#include "plugin.h"
#include "resamplerInOut.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>

namespace
{
	bool g_countAllocations = false;
	size_t g_allocations = 0;

	void require(bool _condition, const char* _message)
	{
		if(_condition)
			return;
		std::fprintf(stderr, "dynamicSamplerate: %s\n", _message);
		std::exit(1);
	}

	class ClockDevice final : public synthLib::Device
	{
	public:
		ClockDevice() : Device({}) {}
		float rate = 32000;
		size_t samples = 0;
		float getSamplerate() const override { return rate; }
		void getDynamicSamplerates(std::vector<float>& _dst) const override { _dst = {32000, 48000}; }
		bool isValid() const override { return true; }
		bool getState(std::vector<uint8_t>&, synthLib::StateType) override { return true; }
		bool setState(const std::vector<uint8_t>&, synthLib::StateType) override { rate = 48000; return true; }
		uint32_t getChannelCountIn() override { return 2; }
		uint32_t getChannelCountOut() override { return 2; }
		bool setDspClockPercent(uint32_t) override { return false; }
		uint32_t getDspClockPercent() const override { return 100; }
		uint64_t getDspClockHz() const override { return 8000000; }
		uint32_t getInternalLatencyInputToOutput() const override { return 32; }
	protected:
		void readMidiOut(std::vector<synthLib::SMidiEvent>&) override {}
		bool sendMidi(const synthLib::SMidiEvent&, std::vector<synthLib::SMidiEvent>&) override { return true; }
		void processAudio(const synthLib::TAudioInputs& _ins, const synthLib::TAudioOutputs& _outs, size_t _count) override
		{
			samples += _count;
			for(size_t c = 0; c < 2; ++c)
				std::copy_n(_ins[c], _count, _outs[c]);
		}
	};

	void test(const synthLib::Resampler::Mode _mode, float _host)
	{
		float in[256]{}, left[256]{}, right[256]{};
		synthLib::TAudioInputs ins{};
		synthLib::TAudioOutputs outs{};
		ins[0] = ins[1] = in;
		outs[0] = left; outs[1] = right;
		synthLib::ResamplerInOut resampler(2, 2);
		resampler.setResamplerMode(_mode);
		resampler.prepareDeviceSamplerates({32000, 48000});
		resampler.setSamplerates(_host, 32000);
		std::vector<synthLib::SMidiEvent> midi, midiOut;
		size_t received = 0;
		uint32_t lastOffset = 0;
		auto render = [&](const synthLib::TAudioInputs& _ins, const synthLib::TAudioOutputs& _outs,
			size_t _count, const auto& _midi, auto&)
		{
			received += _midi.size();
			if(!_midi.empty()) lastOffset = _midi.back().offset;
			for(size_t c = 0; c < 2; ++c)
				std::copy_n(_ins[c], _count, _outs[c]);
		};

		// Returning to a used rate must not replay its old signal. A switch
		// itself must not allocate/rebuild filters, for any conversion mode.
		for(int pass = 0; pass < 8; ++pass)
		{
			g_allocations = 0;
			g_countAllocations = true;
			resampler.setDeviceSamplerate(pass % 2 ? 48000 : 32000);
			g_countAllocations = false;
			require(g_allocations == 0, "prepared rate switch allocated");
			std::fill_n(in, 256, 0.0f);
			for(int block = 0; block < 8; ++block)
			{
				resampler.process(ins, outs, {}, midiOut, 256, render);
				for(float sample : left)
					require(std::isfinite(sample) && std::abs(sample) < 1e-7f, "cached rate replayed old audio");
			}
			std::fill_n(in, 256, 0.25f);
			for(int block = 0; block < 16; ++block)
				resampler.process(ins, outs, {}, midiOut, 256, render);
			require(std::abs(left[255] - 0.25f) < 0.005f, "conversion lost steady signal");
		}

		// Tiny host blocks leave MIDI pending in some resamplers. Switch
		// immediately after each event, including into the equal-rate bypass.
		for(int i = 0; i < 200; ++i)
		{
			midi.assign(1, synthLib::SMidiEvent{});
			resampler.process(ins, outs, midi, midiOut, 1, render);
			resampler.setDeviceSamplerate(i % 2 ? 32000 : 48000);
		}
		for(int i = 0; i < 8; ++i)
			resampler.process(ins, outs, {}, midiOut, 256, render);
		require(received == 200, "rate switch lost or duplicated queued MIDI");
		const float oldRate = _host == 32000 ? 48000 : 32000;
		const float newRate = oldRate == 32000 ? 48000 : 32000;
		resampler.setDeviceSamplerate(oldRate);
		midi.front().offset = 17;
		resampler.process(ins, outs, midi, midiOut, 0, render);
		resampler.setDeviceSamplerate(newRate);
		for(int i = 0; i < 8; ++i)
			resampler.process(ins, outs, {}, midiOut, 256, render);
		const auto expectedOffset = static_cast<uint32_t>(std::floor(std::floor(17 * oldRate / _host) * newRate / oldRate));
		require(received == 201 && lastOffset == expectedOffset, "pending MIDI offset did not follow the new rate");

		ClockDevice device;
		synthLib::Plugin plugin(&device, [](auto* _d) { return _d; });
		plugin.setMidiClockEnabled(false);
		plugin.setResamplerMode(_mode);
		plugin.setHostSamplerate(_host, 0);
		plugin.setBlockSize(256);
		for(float rate : {32000.0f, 48000.0f, 32000.0f, 48000.0f})
		{
			device.rate = rate;
			for(int i = 0; i < 8; ++i)
				plugin.process(ins, outs, 256, 120, 0, false, false);
			const auto start = device.samples;
			for(int i = 0; i < 100; ++i)
				plugin.process(ins, outs, 256, 120, 0, false, false);
			const double expected = 25600.0 * rate / _host;
			require(std::abs(static_cast<double>(device.samples - start) - expected) < 4,
				"framework did not follow the device clock");
		}
		device.rate = 32000;
		plugin.process(ins, outs, 256, 120, 0, false, false);
		require(plugin.setState({1, synthLib::StateTypeGlobal}), "state restore failed");
		for(int i = 0; i < 8; ++i)
			plugin.process(ins, outs, 256, 120, 0, false, false);
		const auto start = device.samples;
		for(int i = 0; i < 100; ++i)
			plugin.process(ins, outs, 256, 120, 0, false, false);
		require(std::abs(static_cast<double>(device.samples - start) - 25600.0 * 48000 / _host) < 4,
			"state-restored clock did not reach resampler");
		std::printf("PASS mode=%d host=%.0f\n", static_cast<int>(_mode), _host);
	}
}

void* operator new(std::size_t _size)
{
	if(g_countAllocations) ++g_allocations;
	if(void* p = std::malloc(std::max<std::size_t>(_size, 1))) return p;
	throw std::bad_alloc();
}
void operator delete(void* _p) noexcept { std::free(_p); }
void* operator new[](std::size_t _size) { return ::operator new(_size); }
void operator delete[](void* _p) noexcept { std::free(_p); }

int main()
{
	for(auto mode : {synthLib::Resampler::Mode::Legacy, synthLib::Resampler::Mode::MameHq,
		synthLib::Resampler::Mode::MameLofi})
		for(float host : {32000.0f, 44100.0f, 48000.0f, 96000.0f})
			test(mode, host);
}

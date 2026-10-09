#include "device.h"
#include "plugin.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <vector>

// The latency a plugin reports has to match the delay its output really has, in every resampler mode (EMU-103: the
// resampler's own delay was missing, up to 5 ms with Mame HQ). A device answers a note-on with a click at exactly the
// device sample the note reaches it at, or passes its input straight through, delayed by the extra latency the plugin
// asks for, like the emulated devices do. The click is found in the host-rate output and compared with the latency.
namespace
{
	class ClickDevice final : public synthLib::Device
	{
	public:
		ClickDevice(const float _rate, const bool _passThrough) : Device({}), m_rate(_rate), m_passThrough(_passThrough) {}

		float getSamplerate() const override { return m_rate; }
		bool isValid() const override { return true; }
		bool getState(std::vector<uint8_t>&, synthLib::StateType) override { return true; }
		bool setState(const std::vector<uint8_t>&, synthLib::StateType) override { return true; }
		uint32_t getChannelCountIn() override { return 2; }
		uint32_t getChannelCountOut() override { return 2; }
		bool setDspClockPercent(uint32_t) override { return false; }
		uint32_t getDspClockPercent() const override { return 100; }
		uint64_t getDspClockHz() const override { return 1; }

	protected:
		void readMidiOut(std::vector<synthLib::SMidiEvent>&) override {}

		bool sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>&) override
		{
			if((_ev.a & 0xf0) == synthLib::M_NOTEON)
				m_pending.push_back(_ev.offset);
			return true;
		}

		void processAudio(const synthLib::TAudioInputs& _ins, const synthLib::TAudioOutputs& _outs, const size_t _count) override
		{
			for(size_t i = 0; i < _count; ++i)
			{
				bool hit = false;
				for(const auto offset : m_pending)
					hit |= offset == i;
				m_delay.push_back(m_passThrough ? _ins[0][i] : (hit ? 1.0f : 0.0f));

				float out = 0.0f;
				if(m_delay.size() > getExtraLatencySamples())
				{
					out = m_delay.front();
					m_delay.pop_front();
				}
				_outs[0][i] = _outs[1][i] = out;
			}
			m_pending.clear();
		}

	private:
		const float m_rate;
		const bool m_passThrough;
		std::vector<uint32_t> m_pending;
		std::deque<float> m_delay;
	};

	// host samples from the event to the click's peak, minus the latency the plugin reports
	double error(const float _deviceRate, const float _hostRate, const synthLib::Resampler::Mode _mode, const uint32_t _at, const bool _viaInput)
	{
		constexpr uint32_t blockSize = 256;
		constexpr uint32_t warmupBlocks = 16;

		ClickDevice device(_deviceRate, _viaInput);
		synthLib::Plugin plugin(&device, [](synthLib::Device* _d) { return _d; });
		plugin.setMidiClockEnabled(false);
		plugin.setResamplerMode(_mode);
		plugin.setHostSamplerate(_hostRate, _deviceRate);
		plugin.setBlockSize(blockSize);

		std::vector<float> in(blockSize), left(blockSize), right(blockSize), recorded;
		synthLib::TAudioInputs ins{};
		synthLib::TAudioOutputs outs{};
		ins[0] = ins[1] = in.data();
		outs[0] = left.data();
		outs[1] = right.data();

		const auto eventBlock = warmupBlocks + _at / blockSize;
		const auto blockCount = eventBlock + 1 + static_cast<uint32_t>(_hostRate * 0.05f) / blockSize;
		for(uint32_t b = 0; b < blockCount; ++b)
		{
			std::fill(in.begin(), in.end(), 0.0f);
			if(b == eventBlock)
			{
				if(_viaInput)
					in[_at % blockSize] = 1.0f;
				else
					plugin.addMidiEvent(synthLib::SMidiEvent(synthLib::MidiEventSource::Host, synthLib::M_NOTEON, 60, 100, _at % blockSize));
			}
			plugin.process(ins, outs, blockSize, 120.0f, 0.0f, false, false);
			recorded.insert(recorded.end(), left.begin(), left.end());
		}

		size_t peak = 0;
		for(size_t i = 1; i < recorded.size(); ++i)
			if(std::fabs(recorded[i]) > std::fabs(recorded[peak]))
				peak = i;

		const auto reported = _viaInput ? plugin.getLatencyInputToOutput() : plugin.getLatencyMidiToOutput();
		return static_cast<double>(peak) - (static_cast<double>(warmupBlocks) * blockSize + _at) - reported;
	}
}

int main()
{
	const synthLib::Resampler::Mode modes[] = {synthLib::Resampler::Mode::Legacy, synthLib::Resampler::Mode::MameHq, synthLib::Resampler::Mode::MameLofi};
	int failures = 0;

	for(const auto device : {32000.0f, 46875.0f, 88200.0f})
	{
		for(const auto host : {44100.0f, 96000.0f})
		{
			for(const auto mode : modes)
			{
				for(const bool viaInput : {false, true})
				{
					// averaged over a few positions in the block: an event can only land on a device sample
					double sum = 0;
					const uint32_t positions[] = {0, 100, 255, 263};
					for(const auto at : positions)
						sum += error(device, host, mode, at, viaInput);
					const auto average = sum / static_cast<double>(std::size(positions));

					// a tenth of a millisecond; the unreported delay was 0.3 to 7 ms
					const bool ok = std::fabs(average) < host * 0.0001f;
					failures += ok ? 0 : 1;
					std::printf("%s device %.1f host %.0f mode %d %s: measured - reported = %.2f samples\n", ok ? "ok  " : "FAIL",
						device, host, static_cast<int>(mode), viaInput ? "input to output" : "MIDI to output", average);
				}
			}
		}
	}

	std::printf(failures ? "FAILED (%d)\n" : "PASSED\n", failures);
	return failures ? 1 : 0;
}

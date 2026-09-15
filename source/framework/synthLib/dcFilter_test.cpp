#include "dcFilter.h"

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <initializer_list>

namespace
{
	constexpr double g_pi = 3.14159265358979323846;
	constexpr float g_samplerate = 48000.0f;

	void expect(const bool _condition)
	{
		if(!_condition)
			std::abort();
	}

	// the gain of a sine through the filter, as RMS over a whole number of periods once the start has settled
	double measureGain(synthLib::DcFilter& _filter, const double _frequency)
	{
		const auto second = static_cast<size_t>(g_samplerate);
		double sum = 0.0;

		for(size_t i=0; i<3 * second; ++i)
		{
			const auto in = std::sin(2.0 * g_pi * _frequency * static_cast<double>(i) / g_samplerate);
			const auto out = static_cast<double>(_filter.process(static_cast<float>(in)));

			if(i >= 2 * second)
				sum += out * out;
		}

		return std::sqrt(2.0 * sum / static_cast<double>(second));
	}

	// the analog first order high-pass the filter is the bilinear transform of, with the corner prewarped
	double expectedGain(const double _cutoff, const double _frequency)
	{
		const auto ratio = std::tan(g_pi * _cutoff / g_samplerate) / std::tan(g_pi * _frequency / g_samplerate);
		return 1.0 / std::sqrt(1.0 + ratio * ratio);
	}

	void testDefaultPassesEverything()
	{
		synthLib::DcFilter filter;

		for(int i=0; i<1000; ++i)
		{
			const auto in = static_cast<float>(i % 7) * 0.1f - 0.3f;
			expect(filter.process(in) == in);
		}
	}

	void testRemovesDc()
	{
		synthLib::DcFilter filter(10.0f, g_samplerate);

		auto out = 1.0f;
		for(int i=0; i<static_cast<int>(g_samplerate); ++i)
			out = filter.process(0.25f);

		expect(std::abs(out) < 1e-6f);
	}

	// below, at and above the corner, which is -3 dB
	void testResponse()
	{
		constexpr double cutoff = 100.0;
		synthLib::DcFilter filter(static_cast<float>(cutoff), g_samplerate);

		for(const auto frequency : {25.0, 100.0, 1000.0, 10000.0})
		{
			filter.reset();
			expect(std::abs(measureGain(filter, frequency) - expectedGain(cutoff, frequency)) < 0.002);
		}
	}
}

int main()
{
	testDefaultPassesEverything();
	testRemovesDc();
	testResponse();
	return 0;
}

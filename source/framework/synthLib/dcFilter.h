#pragma once

#include <cmath>

namespace synthLib
{
	/*	A first order high-pass that removes DC, the digital counterpart of an output's coupling capacitor: a series
		capacitor C driven through R1 and loaded by R2 to ground has its corner at 1 / (2 pi (R1 + R2) C).

		It is the bilinear transform with the corner prewarped, written as the input minus a trapezoidal one pole
		low-pass that tracks the DC. So the corner lands exactly where it is set and DC is rejected completely, a
		cutoff of zero - which is what a default constructed one has - passes everything unchanged, and the cutoff can
		be moved while running without a jump. The cutoff has to stay below half the samplerate.

		The low-pass runs in double. With a corner of a few Hz its steps are tiny next to the DC it tracks, and a float
		state stops moving once they drop below its resolution: 0.25 of DC through a 10 Hz corner left 1e-5 behind.

		After a step to silence the state decays toward zero, so run it with denormals flushed, as Plugin::process does.
	*/
	class DcFilter
	{
	public:
		DcFilter() = default;

		DcFilter(const float _cutoffHz, const float _samplerate)
		{
			setCutoff(_cutoffHz, _samplerate);
		}

		void setCutoff(const float _cutoffHz, const float _samplerate)
		{
			constexpr double pi = 3.14159265358979323846;
			const auto k = std::tan(pi * static_cast<double>(_cutoffHz) / static_cast<double>(_samplerate));
			m_g = k / (1.0 + k);
		}

		float process(const float _in)
		{
			const auto in = static_cast<double>(_in);
			const auto v = (in - m_state) * m_g;
			const auto lowpass = v + m_state;
			m_state = lowpass + v;
			return static_cast<float>(in - lowpass);
		}

		void reset()
		{
			m_state = 0.0;
		}

	private:
		double m_g = 0.0;
		double m_state = 0.0;
	};
}

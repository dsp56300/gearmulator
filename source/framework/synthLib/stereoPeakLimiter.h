#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace synthLib
{
	// Stereo-linked sample-peak limiter: immediate attack and 100 ms release.
	// It adds no latency; it does not estimate inter-sample peaks.
	class StereoPeakLimiter
	{
	public:
		static constexpr float Ceiling = 0.98f;

		void prepare(double sampleRate)
		{
			m_release = std::exp(-1.0 / (0.1 * std::max(1.0, sampleRate)));
			m_gain = 1;
		}

		void process(float* left, float* right, size_t count, bool enabled)
		{
			if(!enabled) { m_gain = 1; return; }
			for(size_t i = 0; i < count; ++i)
			{
				if(!std::isfinite(left[i])) left[i] = 0;
				if(!std::isfinite(right[i])) right[i] = 0;
				const auto peak = std::max(std::abs(left[i]), std::abs(right[i]));
				const double target = peak > Ceiling ? Ceiling / static_cast<double>(peak) : 1;
				m_gain = std::min(target, 1 - (1 - m_gain) * m_release);
				if(m_gain > 0.999999 && target == 1) m_gain = 1;
				left[i] *= static_cast<float>(m_gain);
				right[i] *= static_cast<float>(m_gain);
			}
		}

	private:
		double m_gain = 1;
		double m_release = std::exp(-1.0 / 4410.0);
	};
}

#include "synthLib/stereoPeakLimiter.h"
#include "baseLib/os.h"

#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace
{
	void check(bool value, const char* message)
	{
		if(!value) throw std::runtime_error(message);
	}
}

int main()
{
	baseLib::disableErrorDialogs();

	try
	{
		using synthLib::StereoPeakLimiter;
		StereoPeakLimiter limiter;
		limiter.prepare(48000);
		std::array<float, 4> left{0, 0.25f, -0.5f, 0.98f}, right{0.1f, -0.2f, 0.3f, -0.9f};
		const auto originalLeft = left, originalRight = right;
		limiter.process(left.data(), right.data(), left.size(), true);
		check(left == originalLeft && right == originalRight, "Below-ceiling audio changed");

		std::vector<float> source(48000);
		for(size_t i = 0; i < source.size(); ++i) source[i] = 3.5f * std::sin(static_cast<float>(i) * 0.17f);
		auto fullLeft = source, fullRight = source;
		for(auto& value : fullRight) value *= -0.25f;
		auto splitLeft = fullLeft, splitRight = fullRight;
		limiter.prepare(48000);
		limiter.process(fullLeft.data(), fullRight.data(), fullLeft.size(), true);
		StereoPeakLimiter split;
		split.prepare(48000);
		for(size_t offset = 0; offset < source.size(); offset += 127)
			split.process(splitLeft.data() + offset, splitRight.data() + offset, std::min(size_t{127}, source.size() - offset), true);
		check(fullLeft == splitLeft && fullRight == splitRight, "Block boundaries change output");
		for(size_t i = 0; i < source.size(); ++i)
		{
			check(std::abs(fullLeft[i]) <= StereoPeakLimiter::Ceiling + 1e-7f, "Output exceeds ceiling");
			check(fullRight[i] == -0.25f * fullLeft[i], "Stereo balance changed");
		}

		float l = 4, r = -2;
		limiter.process(&l, &r, 1, false);
		check(l == 4 && r == -2, "Bypass modifies overloaded audio");
		l = r = 0.5f;
		limiter.process(&l, &r, 1, true);
		check(l == 0.5f && r == 0.5f, "Bypass retains attenuation");

		std::array<float, 2> recovered{};
		for(size_t rateIndex = 0; rateIndex < 2; ++rateIndex)
		{
			const int rate = rateIndex == 0 ? 44100 : 96000;
			limiter.prepare(rate);
			l = r = 4;
			limiter.process(&l, &r, 1, true);
			float previous = 0;
			for(int i = 0; i < rate; ++i)
			{
				l = r = 0.5f;
				limiter.process(&l, &r, 1, true);
				check(l >= previous && l <= 0.5f, "Release is not monotonic");
				previous = l;
			}
			recovered[rateIndex] = l;
		}
		check(recovered[0] > 0.499f && std::abs(recovered[0] - recovered[1]) < 1e-6f, "Release depends on sample rate");
		l = std::numeric_limits<float>::infinity();
		r = std::numeric_limits<float>::quiet_NaN();
		limiter.process(&l, &r, 1, true);
		check(l == 0 && r == 0, "Non-finite audio escapes limiter");
		std::cout << "Output limiter checks passed\n";
		return 0;
	}
	catch(const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}

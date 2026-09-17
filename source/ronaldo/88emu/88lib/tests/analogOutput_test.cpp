#include "88lib/analog/analogOutput.h"
#include "common/test_util.hpp"

#include <cmath>
#include <complex>
#include <cstdio>

namespace
{
	using namespace emu88Lib;

	constexpr double g_pi = 3.14159265358979323846;
	constexpr double g_dacRate = 32000.0;

	// The word rate each modelled board clocks its DAC at.
	double dacRate(const AnalogModel _model)
	{
		if(_model == AnalogModel::Sc55Mk1 || _model == AnalogModel::Scc1)
			return 64000.0;
		if(_model == AnalogModel::Sc55Mk2)
			return 66207.0;
		return g_dacRate;
	}

	// The AK4324 interpolates before converting: no hold, no images of the frame rate.
	bool interpolates(const AnalogModel _model)
	{
		return _model == AnalogModel::Sc8850 || _model == AnalogModel::Sc8820;
	}

	using Complex = std::complex<double>;

	Complex sallenKey(const Complex& _s, const double _r1, const double _r2, const double _c1, const double _c2)
	{
		return 1.0 / (_s * _s * _r1 * _r2 * _c1 * _c2 + _s * _c2 * (_r1 + _r2) + 1.0);
	}

	Complex pole(const Complex& _s, const double _r, const double _c)
	{
		return 1.0 / (1.0 + _s * _r * _c);
	}

	// The SC-88Pro's R110 / C150 into the multiple-feedback stage, solved by elimination from
	// the summing node backwards, independently of the polynomial the library factors.
	Complex sc88ProFilter(const Complex& _s)
	{
		constexpr double g1 = 1.0 / 6.8e3, g2 = 1.0 / 6.8e3, gf = 1.0 / 13.6e3, gg = 1.0 / 6.8e3;
		constexpr double cin = 2.7e-9, cf = 390e-12, cg = 2.2e-9;
		const Complex v2 = 1.0;
		const Complex vout = -v2 * gg / (_s * cf);
		const Complex v1 = (v2 * (g2 + gf + gg + _s * cg) - gf * vout) / g2;
		const Complex vin = (v1 * (g1 + g2 + _s * cin) - g2 * v2) / g1;
		return vout / vin;
	}

	Complex multipleFeedback(const Complex& _s, const double _r1, const double _rf, const double _rg, const double _cf, const double _cg, const double _gn = 0.0)
	{
		return 1.0 / (_s * _s * _rg * _rf * _cf * _cg + _s * _rg * _rf * _cf * (1.0 / _r1 + 1.0 / _rf + 1.0 / _rg + _gn) + 1.0);
	}

	Complex poleAt(const Complex& _s, const double _frequency)
	{
		return 1.0 / (1.0 + _s / (2.0 * g_pi * _frequency));
	}

	// Continuous-time reference: the DAC's zero-order hold followed by the modelled sections of
	// the circuit, without the DC blockers.
	double referenceGainDb(const AnalogModel _model, const double _frequency)
	{
		const Complex s(0.0, 2.0 * g_pi * _frequency);
		Complex circuit = 1.0;
		switch(_model)
		{
		case AnalogModel::Cm32l:
			// The two multiple-feedback sections; the first is fed by the three sample-and-hold
			// resistors in parallel, which is what damps it. Then the VCA's I/V amplifier and
			// IC22a's feedback capacitor; the DC blockers are excluded as everywhere here.
			circuit = multipleFeedback(s, 1.0 / (1.0 / 6.8e3 + 1.0 / 6.8e3 + 1.0 / 10e3), 6.8e3, 6.8e3, 220e-12, 5.6e-9) *
				multipleFeedback(s, 10e3, 10e3, 10e3, 220e-12, 5.6e-9) *
				pole(s, 4.7e3, 100e-12) * pole(s, 15e3, 220e-12);
			break;
		case AnalogModel::Cm32p:
			circuit = sallenKey(s, 10e3, 10e3, 5.6e-9, 220e-12) * sallenKey(s, 10e3, 10e3, 1.8e-9, 1.2e-9) *
				pole(s, 100e3, 22e-12) * pole(s, 4.7e3 * 6.8e3 / 11.5e3, 1e-9);
			break;
		case AnalogModel::Sc88:
			circuit = pole(s, 4.7e3, 100e-12) * pole(s, 22e3, 100e-12) * pole(s, 12e3, 120e-12) * pole(s, 1e3, 390e-12);
			break;
		case AnalogModel::Sc88Vl:
			circuit = multipleFeedback(s, 12e3, 22e3, 12e3, 180e-12, 820e-12) * pole(s, 1.47e3, 1e-9);
			break;
		case AnalogModel::Sc88Pro:
			circuit = pole(s, 8.2e3, 330e-12) * sc88ProFilter(s) * pole(s, 6.8e3, 330e-12) * pole(s, 220.0, 2.2e-9);
			break;
		case AnalogModel::Sc55Mk1:
			circuit = pole(s, 22e3, 100e-12) * pole(s, 18e3, 82e-12) * pole(s, 2e3, 1.39e-9);
			break;
		case AnalogModel::Sc55Mk2:
			circuit = pole(s, 22e3, 100e-12) * pole(s, 12e3, 120e-12) * pole(s, 2e3, 1e-9);
			break;
		case AnalogModel::Scc1:
			circuit = pole(s, 4.7e3 * 2.2e3 / 6.9e3, 10e-9) * pole(s, 15e3, 330e-12) * pole(s, 47e3, 68e-12) * pole(s, 1.68e3, 1e-9);
			break;
		case AnalogModel::G800:
			circuit = pole(s, 5.6e3, 220e-12) * pole(s, 22e3, 100e-12) * pole(s, 12e3, 100e-12);
			break;
		case AnalogModel::Sc8850:
			circuit = pole(s, 22e3, 100e-12) * multipleFeedback(s, 10e3, 18e3, 10e3 * 18e3 / 28e3, 390e-12, 2.2e-9) *
				pole(s, 10e3, 33e-12) * pole(s, 570.0, 1e-9);
			break;
		case AnalogModel::Sc8820:
			circuit = poleAt(s, 100e3) * multipleFeedback(s, 10e3, 18e3, 10e3 * 18e3 / 28e3, 390e-12, 2.2e-9, 1.0 / 100e3) *
				pole(s, 22e3, 33e-12) * pole(s, 570.0, 1e-9);
			break;
		default:
			break;
		}
		const double x = g_pi * _frequency / dacRate(_model);
		const double hold = interpolates(_model) ? 1.0 : std::sin(x) / x;
		return 20.0 * std::log10(hold * std::abs(circuit));
	}

	// Steady-state level at _probe of a full-scale DAC sine at _tone, held and filtered by the model.
	double measureGainDb(const AnalogModel _model, const double _tone, const double _probe)
	{
		const double rate = dacRate(_model);
		AnalogOutput analog;
		analog.setModel(_model, static_cast<float>(rate));
		const auto oversampling = analog.oversampling();
		const double outputRate = rate * oversampling;
		// One second to settle the DC blocker, one second measured: whole periods of every probe.
		const auto settle = static_cast<size_t>(rate);
		const auto measured = static_cast<size_t>(rate);

		Complex sum;
		for(size_t frame = 0; frame < settle + measured; ++frame)
		{
			const auto dac = static_cast<float>(std::sin(2.0 * g_pi * _tone * static_cast<double>(frame) / rate));
			for(uint32_t hold = 0; hold < oversampling; ++hold)
			{
				float left = dac;
				float right = dac;
				analog.process(left, right);
				if(frame < settle)
					continue;
				const auto t = static_cast<double>((frame - settle) * oversampling + hold) / outputRate;
				sum += static_cast<double>(left) * std::polar(1.0, -2.0 * g_pi * _probe * t);
			}
		}
		return 20.0 * std::log10(2.0 * std::abs(sum) / static_cast<double>(measured * oversampling));
	}

	void checkNear(const char* _what, const double _frequency, const double _actual, const double _expected,
		const double _tolerance)
	{
		std::printf("%-8s %7.0f Hz: %8.3f dB, reference %8.3f dB\n", _what, _frequency, _actual, _expected);
		CHECK(std::fabs(_actual - _expected) <= _tolerance);
	}

	// A frame held for several output samples differs from the board's continuous hold by the
	// sinc of the output rate: the model sits above the circuit by this much, mostly beyond the
	// audio band. The expected level accounts for it so the tolerance covers only the sections.
	double heldFrameDb(const AnalogModel _model, const double _frequency)
	{
		if(interpolates(_model))
			return 0.0;
		const double x = g_pi * _frequency / (dacRate(_model) * getAnalogOversampling(_model));
		return 20.0 * std::log10(std::sin(x) / x);
	}

	void checkResponse(const AnalogModel _model)
	{
		std::printf("%s\n", getAnalogModelName(_model));

		// Passband, including the hold's droop and any peaking of the circuit. An interpolating
		// DAC's transition band starts at 0.4535 of the frame rate, so its probes stop at 14 kHz.
		for(const double tone : {1000.0, 5000.0, 10000.0, 12000.0, 14000.0, 15000.0, 15500.0})
		{
			if(interpolates(_model) && tone > 14000.0)
				break;
			checkNear("tone", tone, measureGainDb(_model, tone, tone), referenceGainDb(_model, tone) - heldFrameDb(_model, tone), 0.1);
		}

		// The first images of the frame rate, relative to a full-scale tone: those of a hold at
		// the circuit's level (the first-order sections are matched at 20 kHz, so the 64 kHz
		// boards' images at 60 kHz stray a little further), those of an interpolating DAC at
		// least 70 dB down once past its transition band.
		for(const double tone : {4000.0, 10000.0, 12000.0, 14000.0, 15000.0})
		{
			if(interpolates(_model) && tone > 14000.0)
				break;
			const double image = dacRate(_model) - tone;
			const double actual = measureGainDb(_model, tone, image);
			if(interpolates(_model))
			{
				std::printf("%-8s %7.0f Hz: %8.3f dB\n", "image", image, actual);
				CHECK(actual < -70.0);
			}
			else
				checkNear("image", image, actual, referenceGainDb(_model, image) - heldFrameDb(_model, image), image > 32000.0 ? 0.4 : 0.25);
		}
	}

	// DC is blocked by the coupling capacitors.
	void checkDcBlocked(const AnalogModel _model, const double _seconds)
	{
		AnalogOutput analog;
		analog.setModel(_model, static_cast<float>(dacRate(_model)));
		const auto samples = static_cast<size_t>(_seconds * dacRate(_model) * analog.oversampling());
		float left = 0.0f;
		float right = 0.0f;
		for(size_t i = 0; i < samples; ++i)
		{
			left = 0.5f;
			right = 0.5f;
			analog.process(left, right);
		}
		std::printf("%-8s DC after %.0f s: %.4f\n", getAnalogModelName(_model), _seconds, left);
		CHECK(std::fabs(left) < 0.01f);
		CHECK(std::fabs(right) < 0.01f);

		analog.reset();
		left = right = 0.0f;
		analog.process(left, right);
		CHECK_EQ(left, 0.0f);
	}
}

int main()
{
	CHECK(resolveAnalogModel(AnalogOutputMode::Off, DeviceModel::Cm32p) == AnalogModel::None);
	CHECK(resolveAnalogModel(AnalogOutputMode::Auto, DeviceModel::Cm32p) == AnalogModel::Cm32p);
	CHECK(resolveAnalogModel(AnalogOutputMode::Auto, DeviceModel::Sc88) == AnalogModel::Sc88);
	CHECK(resolveAnalogModel(AnalogOutputMode::Auto, DeviceModel::Sc88VL) == AnalogModel::Sc88Vl);
	CHECK(resolveAnalogModel(AnalogOutputMode::Auto, DeviceModel::Sc88Pro) == AnalogModel::Sc88Pro);
	CHECK(resolveAnalogModel(AnalogOutputMode::Auto, DeviceModel::Sc8850) == AnalogModel::Sc8850);
	CHECK(resolveAnalogModel(AnalogOutputMode::Auto, DeviceModel::Sc8820) == AnalogModel::Sc8820);
	CHECK(resolveAnalogModel(AnalogOutputMode::Auto, DeviceModel::Cm300) == AnalogModel::Sc55Mk1);
	CHECK(resolveAnalogModel(AnalogOutputMode::Auto, DeviceModel::Sc155) == AnalogModel::Sc55Mk1);
	CHECK(resolveAnalogModel(AnalogOutputMode::Scc1, DeviceModel::Cm300) == AnalogModel::Scc1);
	CHECK(resolveAnalogModel(AnalogOutputMode::Auto, DeviceModel::Xpgs) == AnalogModel::G800);
	CHECK(resolveAnalogModel(AnalogOutputMode::Auto, DeviceModel::VeGsPro) == AnalogModel::Sc88Pro);
	CHECK(resolveAnalogModel(AnalogOutputMode::Auto, DeviceModel::Scb55) == AnalogModel::Sc55Mk2);
	CHECK(resolveAnalogModel(AnalogOutputMode::Auto, DeviceModel::Sc55Mk1) == AnalogModel::Sc55Mk1);
	CHECK(resolveAnalogModel(AnalogOutputMode::Auto, DeviceModel::Sc55Mk2) == AnalogModel::Sc55Mk2);
	CHECK(resolveAnalogModel(AnalogOutputMode::Auto, DeviceModel::Sc155Mk2) == AnalogModel::Sc55Mk2);
	CHECK(resolveAnalogModel(AnalogOutputMode::Cm32p, DeviceModel::Sc55Mk2) == AnalogModel::Cm32p);
	CHECK(resolveAnalogModel(AnalogOutputMode::Sc88, DeviceModel::Cm32p) == AnalogModel::Sc88);

	// DAC word widths: the surplus low bits of the 24-bit interface word are dropped, never
	// rounded, and a 24-bit DAC keeps every bit.
	CHECK_EQ(getDacBits(DeviceModel::Cm32p), 16);
	CHECK_EQ(getDacBits(DeviceModel::Sc55Mk1), 16);
	CHECK_EQ(getDacBits(DeviceModel::Cm300), 16);
	CHECK_EQ(getDacBits(DeviceModel::Sc55Mk2), 18);
	CHECK_EQ(getDacBits(DeviceModel::Sc88Pro), 18);
	CHECK_EQ(getDacBits(DeviceModel::Sc8850), 24);
	CHECK_EQ(synthLib::quantiseDacWord(0x3FFFF, 16), 0x3FF00);
	CHECK_EQ(synthLib::quantiseDacWord(-1, 16), -256);
	CHECK_EQ(synthLib::quantiseDacWord(-257, 16), -512);
	CHECK_EQ(synthLib::quantiseDacWord(0x3FFFF, 18), 0x3FFC0);
	CHECK_EQ(synthLib::quantiseDacWord(0x3FFFF, 24), 0x3FFFF);
	CHECK_EQ(synthLib::quantiseDacWord((1 << 23) - 1, 16), (1 << 23) - 256);

	AnalogOutput analog;
	analog.setModel(AnalogModel::None, static_cast<float>(g_dacRate));
	CHECK_EQ(analog.oversampling(), 1u);
	float left = 0.25f;
	float right = -0.5f;
	analog.process(left, right);
	CHECK_EQ(left, 0.25f);
	CHECK_EQ(right, -0.5f);

	checkResponse(AnalogModel::Cm32l);
	checkResponse(AnalogModel::Cm32p);
	checkResponse(AnalogModel::Sc88);
	checkResponse(AnalogModel::Sc88Vl);
	checkResponse(AnalogModel::Sc88Pro);
	checkResponse(AnalogModel::Sc55Mk1);
	checkResponse(AnalogModel::Scc1);
	checkResponse(AnalogModel::G800);
	checkResponse(AnalogModel::Sc8850);
	checkResponse(AnalogModel::Sc8820);
	checkResponse(AnalogModel::Sc55Mk2);

	checkDcBlocked(AnalogModel::Cm32l, 1.0);
	checkDcBlocked(AnalogModel::Cm32p, 1.0);		// C58A into 11.5k: 1.4 Hz
	checkDcBlocked(AnalogModel::Sc88, 4.0);			// C147 into 10.7k: 0.32 Hz
	checkDcBlocked(AnalogModel::Sc88Vl, 4.0);		// C46 into 10.7k: 0.32 Hz
	checkDcBlocked(AnalogModel::Sc88Pro, 2.0);		// C192 into 7.1k: 0.68 Hz
	checkDcBlocked(AnalogModel::Sc55Mk1, 1.0);		// C44 into 12k: 1.3 Hz
	checkDcBlocked(AnalogModel::Scc1, 1.0);			// C23 into 6.9k: 2.3 Hz
	checkDcBlocked(AnalogModel::G800, 1.0);			// C3 into 12k: 1.3 Hz
	checkDcBlocked(AnalogModel::Sc8850, 3.0);		// C18 into 9.1k: 0.53 Hz
	checkDcBlocked(AnalogModel::Sc8820, 1.0);		// C22 into 9.1k and C23 into 10k: 1.7 Hz each
	checkDcBlocked(AnalogModel::Sc55Mk2, 4.0);		// C43 into 12k: 0.28 Hz

	return test::finish("analog output");
}

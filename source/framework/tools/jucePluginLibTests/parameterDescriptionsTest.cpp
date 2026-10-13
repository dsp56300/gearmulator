#include "jucePluginLibTests.h"

#include <cstdlib>
#include <iostream>

#include "jucePluginLib/parameterdescriptions.h"

#include "juce_core/juce_core.h"

namespace
{
	// texts "v<value>" and back, the plugin's own
	class TestFormatter : public pluginLib::ValueFormatter
	{
	public:
		std::string toText(const int _value) const override
		{
			return "v" + std::to_string(_value);
		}

		int toValue(const std::string& _text) const override
		{
			return std::atoi(_text.c_str() + 1);
		}
	};

	// a named list, a format and a formatter, which gets the whole toText object
	constexpr auto g_json = R"({
		"parameterdescriptiondefaults":
		{
			"isPublic":true, "isBipolar":false, "isBool":false, "isDiscrete":false, "class":"", "step":0
		},
		"parameterdescriptions":
		[
			{"page":0, "index":0, "name":"Named", "min":0, "max":2, "toText":"names"},
			{"page":0, "index":1, "name":"Format", "min":-8, "max":8, "toText":{"format":"%+.1f dB", "scale":0.5}},
			{"page":0, "index":2, "name":"Formatter", "min":0, "max":16383, "toText":{"formatter":"v", "centre":8192}}
		],
		"valuelists":
		{
			"names": ["Low", "Mid", "High"]
		},
		"controllerMap":
		[
			{"nrpn":"1234", "param":"Formatter"},
			{"nrpn":"7f", "param":"Named"},
			{"cc":"7", "param":"Named"}
		]
	})";

	const pluginLib::Description& find(const pluginLib::ParameterDescriptions& _descs, const std::string& _name)
	{
		for(const auto& d : _descs.getDescriptions())
		{
			if(d.name == _name)
				return d;
		}
		throw std::runtime_error("no parameter " + _name);
	}
}

void testParameterDescriptions()
{
	std::cout << "Testing parameter descriptions..." << std::endl;

	int calls = 0;
	const pluginLib::ParameterDescriptions::FormatterFactory factory = [&](const juce::var& _toText, const int _min,
		const int _max) -> std::shared_ptr<const pluginLib::ValueFormatter>
	{
		++calls;
		TEST_ASSERT(_min == 0 && _max == 16383);
		TEST_ASSERT(static_cast<int>(_toText["centre"]) == 8192);
		if(_toText["formatter"].toString() != "v")
			return nullptr;
		return std::make_shared<TestFormatter>();
	};

	const pluginLib::ParameterDescriptions descs(g_json, factory);
	TEST_ASSERT(descs.isValid());
	TEST_ASSERT(calls == 1);

	// the other two forms as before; a list's index is value - min(0, min), as Parameter::getText has it
	const auto& named = find(descs, "Named").valueList;
	TEST_ASSERT(named.valueToText(1) == "Mid");
	TEST_ASSERT(named.textToValue("High") == 2);

	const auto& format = find(descs, "Format").valueList;
	TEST_ASSERT(format.valueToText(4 + 8) == "+2.0 dB");
	TEST_ASSERT(format.textToValue("+2.0 dB") == 4 + 8);

	// the formatter's texts and values, the index clamped to the range
	const auto& formatter = find(descs, "Formatter");
	TEST_ASSERT(formatter.toText == "v");
	TEST_ASSERT(formatter.valueList.valueToText(100) == "v100");
	TEST_ASSERT(formatter.valueList.valueToText(20000) == "v16383");
	TEST_ASSERT(formatter.valueList.textToValue("v100") == 100);
	TEST_ASSERT(formatter.valueList.textToValue("v99999") == 16383);
	TEST_ASSERT(formatter.valueList.textToValue("v-5") == 0);

	// controllerMap: an entry can have an NRPN only, in hex, with all of its 14 bits (EMU-97)
	const auto& cm = descs.getControllerMap();
	uint32_t formatterIndex = 0, namedIndex = 0;
	TEST_ASSERT(descs.getIndexByName(formatterIndex, "Formatter") && descs.getIndexByName(namedIndex, "Named"));
	TEST_ASSERT(cm.getParameters(0x1234 >> 7, 0x1234 & 0x7f) == std::vector<uint32_t>{formatterIndex});
	TEST_ASSERT(cm.getParameters(0x00, 0x7f) == std::vector<uint32_t>{namedIndex});
	TEST_ASSERT(cm.getParameters(0x00, 0x34).empty());	// what an 8 bit cut of 0x1234 would have registered
	TEST_ASSERT(cm.getParameters(synthLib::SMidiEvent(synthLib::MidiEventSource::Host, synthLib::M_CONTROLCHANGE, 7, 0)) == std::vector<uint32_t>{namedIndex});

	// the decoder: CC 99/98 select, CC 6/38 enter, per channel; an RPN or the null NRPN ends the selection
	pluginLib::NrpnDecoder decoder;
	auto cc = [&](const uint8_t _channel, const uint8_t _controller, const uint8_t _value)
	{
		return decoder.process(synthLib::SMidiEvent(synthLib::MidiEventSource::Host, static_cast<uint8_t>(synthLib::M_CONTROLCHANGE + _channel), _controller, _value));
	};
	TEST_ASSERT(!cc(0, synthLib::MC_DATAENTRYMSB, 5));
	TEST_ASSERT(!cc(0, synthLib::MC_NRPNMSB, 0x24));
	TEST_ASSERT(!cc(0, synthLib::MC_NRPNLSB, 0x34));
	auto e = cc(0, synthLib::MC_DATAENTRYMSB, 0x10);
	TEST_ASSERT(e && e->nrpnMsb == 0x24 && e->nrpnLsb == 0x34 && e->msb == 0x10 && !e->lsb);
	e = cc(0, synthLib::MC_DATAENTRYLSB, 0x05);
	TEST_ASSERT(e && e->msb == 0x10 && e->lsb == 0x05);
	TEST_ASSERT(!cc(1, synthLib::MC_DATAENTRYMSB, 1));
	TEST_ASSERT(!cc(0, synthLib::MC_MODULATION, 1));
	cc(0, synthLib::MC_RPNMSB, 0);
	TEST_ASSERT(!cc(0, synthLib::MC_DATAENTRYMSB, 1));
	cc(0, synthLib::MC_NRPNMSB, 0x24);
	TEST_ASSERT(cc(0, synthLib::MC_DATAENTRYMSB, 1));
	cc(0, synthLib::MC_NRPNMSB, 0x7f);
	cc(0, synthLib::MC_NRPNLSB, 0x7f);
	TEST_ASSERT(!cc(0, synthLib::MC_DATAENTRYMSB, 1));

	// a formatter the plugin does not know is an error, which asserts in a debug build, not tested here

	// an inline toText list is an error, not a crash. Errors assert in a debug build, so only a release build checks it
#ifdef NDEBUG
	const pluginLib::ParameterDescriptions inlineList(R"({
		"parameterdescriptions": [{"name":"Inline", "min":0, "max":1, "toText":["A", "B"]}],
		"valuelists": {}
	})");
	TEST_ASSERT(inlineList.getErrors().find("an inline list is not supported") != std::string::npos);
#endif
}

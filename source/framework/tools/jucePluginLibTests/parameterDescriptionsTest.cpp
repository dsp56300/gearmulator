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
		}
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

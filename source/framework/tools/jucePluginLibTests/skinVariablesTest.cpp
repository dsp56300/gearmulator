#include "jucePluginLibTests.h"

#include <iostream>
#include <vector>

#include "baseLib/binarystream.h"

#include "jucePluginLib/skinVariables.h"

namespace
{
	using SkinVariables = pluginLib::SkinVariables;
	using Scope = SkinVariables::Scope;

	void testScopePrecedence()
	{
		std::cout << "Testing skin variable scopes..." << std::endl;

		SkinVariables v;

		TEST_ASSERT(v.get("nothing") == nullptr);

		v.set("a", int64_t(1), Scope::Global);

		// only a global exists, so a read without a scope has to find it
		TEST_ASSERT(v.get("a") != nullptr);
		TEST_ASSERT(std::get<int64_t>(*v.get("a")) == 1);

		v.set("a", int64_t(2), Scope::Instance);

		// with both present the instance wins unless a scope is named
		TEST_ASSERT(std::get<int64_t>(*v.get("a")) == 2);
		TEST_ASSERT(std::get<int64_t>(*v.get("a", Scope::Instance)) == 2);
		TEST_ASSERT(std::get<int64_t>(*v.get("a", Scope::Global)) == 1);

		// and the global reappears once the instance value is gone
		TEST_ASSERT(v.remove("a", Scope::Instance));
		TEST_ASSERT(std::get<int64_t>(*v.get("a")) == 1);
		TEST_ASSERT(!v.remove("a", Scope::Instance));

		std::cout << "  scope tests passed" << std::endl;
	}

	void testChangeEvent()
	{
		std::cout << "Testing skin variable change event..." << std::endl;

		SkinVariables v;

		std::vector<std::pair<std::string, Scope>> changes;

		const auto id = v.evChanged.addListener([&changes](const std::string& _name, const Scope _scope)
		{
			changes.emplace_back(_name, _scope);
		});

		v.set("x", int64_t(1), Scope::Instance);
		TEST_ASSERT(changes.size() == 1);
		TEST_ASSERT(changes.back().first == "x" && changes.back().second == Scope::Instance);

		// setting the same value again is not a change
		v.set("x", int64_t(1), Scope::Instance);
		TEST_ASSERT(changes.size() == 1);

		// the same name in the other scope is
		v.set("x", int64_t(1), Scope::Global);
		TEST_ASSERT(changes.size() == 2);
		TEST_ASSERT(changes.back().second == Scope::Global);

		v.remove("x", Scope::Global);
		TEST_ASSERT(changes.size() == 3);

		v.evChanged.removeListener(id);

		std::cout << "  change event tests passed" << std::endl;
	}

	void testStatePersistence()
	{
		std::cout << "Testing skin variable serialisation..." << std::endl;

		// a string that looks like a number must not come back as one, and vice versa
		SkinVariables a;
		a.set("count", int64_t(-42), Scope::Instance);
		a.set("text", std::string("7"), Scope::Instance);
		a.set("global", int64_t(9), Scope::Global);

		baseLib::BinaryStream out;
		a.saveChunkData(out);

		std::vector<uint8_t> buffer;
		out.toVector(buffer);

		baseLib::BinaryStream in(buffer);
		baseLib::ChunkReader cr(in);

		SkinVariables b;
		b.loadChunkData(cr);
		cr.read();

		TEST_ASSERT(std::get<int64_t>(*b.get("count", Scope::Instance)) == -42);
		TEST_ASSERT(std::get<std::string>(*b.get("text", Scope::Instance)) == "7");

		// the global scope belongs to the config file, not to the plugin state
		TEST_ASSERT(b.get("global", Scope::Global) == nullptr);

		std::cout << "  serialisation tests passed" << std::endl;
	}

	void testValueEncoding()
	{
		std::cout << "Testing skin variable value encoding..." << std::endl;

		TEST_ASSERT(std::get<int64_t>(SkinVariables::fromString(SkinVariables::toString(int64_t(0)))) == 0);
		TEST_ASSERT(std::get<int64_t>(SkinVariables::fromString(SkinVariables::toString(int64_t(-1)))) == -1);
		TEST_ASSERT(std::get<std::string>(SkinVariables::fromString(SkinVariables::toString(std::string("")))).empty());
		TEST_ASSERT(std::get<std::string>(SkinVariables::fromString(SkinVariables::toString(std::string("123")))) == "123");
		TEST_ASSERT(std::get<std::string>(SkinVariables::fromString(SkinVariables::toString(std::string("i5")))) == "i5");

		std::cout << "  encoding tests passed" << std::endl;
	}
}

void testSkinVariables()
{
	testScopePrecedence();
	testChangeEvent();
	testStatePersistence();
	testValueEncoding();
}

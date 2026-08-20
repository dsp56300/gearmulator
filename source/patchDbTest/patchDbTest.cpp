#include <iostream>
#include <sstream>
#include <stdexcept>

#include "jucePluginLib/patchdb/db.h"
#include "jucePluginLib/patchdb/search.h"
#include "jucePluginLib/filetype.h"

#include <juce_core/juce_core.h>

using namespace pluginLib::patchDB;

// Custom assertion that works in both Debug and Release builds
#define TEST_ASSERT(condition) \
	do { \
		if (!(condition)) { \
			std::ostringstream oss; \
			oss << "Test assertion failed: " << #condition \
			    << " at " << __FILE__ << ":" << __LINE__; \
			throw std::runtime_error(oss.str()); \
		} \
	} while (0)

namespace
{
	// DB::~DB() asserts that a derived class has already stopped the loader thread by the time
	// its own destructor body runs, see the comment at that assert in db.cpp.
	class TestDb : public DB
	{
	public:
		explicit TestDb(const juce::File& _dir) : DB(_dir) {}
		~TestDb() override { stopLoaderThread(); }

		// pure virtuals unrelated to search handling, never exercised by these tests
		bool requestPatchForPart(Data&, uint32_t, uint64_t) override { return false; }
		bool loadRomData(DataList&, uint32_t, uint32_t) override { return false; }
		PatchPtr initializePatch(Data&&, const std::string&) override { return {}; }
		Data applyModifications(const PatchPtr&, const pluginLib::FileType&, pluginLib::ExportType) const override { return {}; }
		void processDirty(const Dirty&) const override {}
	};

	juce::File createTempDir(const std::string& _name)
	{
		auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
			.getChildFile(juce::String("patchDbTest_") + _name + "_" + juce::String(juce::Time::getMillisecondCounterHiRes()));
		dir.createDirectory();
		return dir;
	}

	SearchRequest makeRequest()
	{
		SearchRequest req;
		req.name = "test";	// non-empty so SearchRequest::isValid() would pass, though DB::search() doesn't require it
		return req;
	}

	// Mirrors the fix in jucePluginEditorLib::patchManagerRml::ListModel: cancel any in-flight
	// search from the destructor, so tearing down the owning UI (e.g. closing the patch manager
	// panel mid-search) can't leave an orphaned entry in DB::m_searches forever. ListModel itself
	// pulls in RmlUi/Editor/PatchManagerUiRml and can't be instantiated from this lightweight
	// console test, so this stands in for it - same lifecycle contract, no GUI dependencies.
	class ListModelLike
	{
	public:
		explicit ListModelLike(DB& _db) : m_db(_db) {}
		~ListModelLike() { cancelSearch(); }

		void setContent(SearchRequest&& _request)
		{
			cancelSearch();
			m_handle = m_db.search(std::move(_request));
		}

		void cancelSearch()
		{
			if (m_handle == g_invalidSearchHandle)
				return;
			m_db.cancelSearch(m_handle);
			m_handle = g_invalidSearchHandle;
		}

		SearchHandle getSearchHandle() const { return m_handle; }

	private:
		DB& m_db;
		SearchHandle m_handle = g_invalidSearchHandle;
	};
}

void testCancelSearchRemovesHandle()
{
	std::cout << "Testing DB::cancelSearch() removes the search handle..." << std::endl;

	const auto dir = createTempDir("cancel");
	TestDb db(dir);

	const auto handle = db.search(makeRequest());
	TEST_ASSERT(db.getSearch(handle) != nullptr);

	db.cancelSearch(handle);
	TEST_ASSERT(db.getSearch(handle) == nullptr);

	dir.deleteRecursively();
}

void testUncancelledSearchStaysRegisteredForever()
{
	std::cout << "Testing an uncancelled search handle stays registered indefinitely..." << std::endl;

	const auto dir = createTempDir("uncancelled");
	TestDb db(dir);

	// DB::search()/DB::cancelSearch() have no automatic cleanup or timeout: a handle that is
	// never cancelled stays in DB::m_searches forever, regardless of whether the search itself
	// has already completed. This is exactly the leak a ListModel that never cancels its search
	// on destruction would cause.
	const auto handle = db.search(makeRequest());
	TEST_ASSERT(db.getSearch(handle) != nullptr);
	TEST_ASSERT(db.getSearch(handle) != nullptr);

	dir.deleteRecursively();
}

void testListModelLikeCancelsSearchOnDestroy()
{
	std::cout << "Testing a ListModel-like owner cancels its search when destroyed..." << std::endl;

	const auto dir = createTempDir("listmodel");
	TestDb db(dir);

	SearchHandle handle = g_invalidSearchHandle;
	{
		ListModelLike model(db);
		model.setContent(makeRequest());
		handle = model.getSearchHandle();
		TEST_ASSERT(handle != g_invalidSearchHandle);
		TEST_ASSERT(db.getSearch(handle) != nullptr);
	}
	// model is destroyed here - its destructor must have cancelled the search
	TEST_ASSERT(db.getSearch(handle) == nullptr);

	dir.deleteRecursively();
}

int main()
{
	try
	{
		std::cout << "Running Patch DB Unit Tests..." << std::endl;
		std::cout << std::endl;

		testCancelSearchRemovesHandle();
		testUncancelledSearchStaysRegisteredForever();
		testListModelLikeCancelsSearchOnDestroy();

		std::cout << std::endl;
		std::cout << "All tests passed successfully!" << std::endl;
		return 0;
	}
	catch (const std::exception& e)
	{
		std::cerr << "Test failed with exception: " << e.what() << std::endl;
		return 1;
	}
	catch (...)
	{
		std::cerr << "Test failed with unknown exception" << std::endl;
		return 1;
	}
}

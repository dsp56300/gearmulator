#pragma once

#include <exception>
#include <iostream>
#include <utility>

namespace virusLibTest
{
	class TestRunner
	{
	public:
		template<typename Test>
		void run(const char* const _name, Test&& _test)
		{
			++m_total;
			bool passed = false;
			try
			{
				passed = std::forward<Test>(_test)();
			}
			catch(const std::exception& e)
			{
				std::cerr << "      exception: " << e.what() << '\n';
			}
			catch(...)
			{
				std::cerr << "      unknown exception\n";
			}

			if(passed)
				++m_passed;
			else
				++m_failed;

			std::cout << (passed ? "PASS  " : "FAIL  ") << _name << '\n';
		}

		bool allPassed() const { return m_failed == 0; }

		void printSummary() const
		{
			std::cout << '\n' << m_total << " tests, " << m_passed << " passed, " << m_failed << " failed\n";
		}

	private:
		unsigned int m_total = 0;
		unsigned int m_passed = 0;
		unsigned int m_failed = 0;
	};
}

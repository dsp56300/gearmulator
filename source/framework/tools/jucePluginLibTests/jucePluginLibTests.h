#pragma once

#include <sstream>
#include <stdexcept>

// works in Debug and Release alike, unlike assert()
#define TEST_ASSERT(condition)																		do {																								if (!(condition)) {																					std::ostringstream oss;																			oss << "Test assertion failed: " << #condition													    << " at " << __FILE__ << ":" << __LINE__;													throw std::runtime_error(oss.str());														}																							} while (0)

void testSkinVariables();

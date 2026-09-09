#pragma once
#include <cstdio>
#include <string>

namespace test {
inline int g_failures = 0;
inline int g_checks = 0;

#define CHECK(cond)                                                                        \
  do {                                                                                     \
    ++test::g_checks;                                                                      \
    if (!(cond)) {                                                                         \
      ++test::g_failures;                                                                  \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                          \
    }                                                                                      \
  } while (0)

#define CHECK_EQ(a, b)                                                                     \
  do {                                                                                     \
    ++test::g_checks;                                                                      \
    auto _va = (a); auto _vb = (b);                                                        \
    if (!(_va == _vb)) {                                                                   \
      ++test::g_failures;                                                                  \
      std::printf("FAIL %s:%d: %s == %s  (got %s vs %s)\n", __FILE__, __LINE__, #a, #b,    \
                  test::to_str(_va).c_str(), test::to_str(_vb).c_str());                   \
    }                                                                                      \
  } while (0)

inline std::string to_str(const std::string& s) { return "\"" + s + "\""; }
inline std::string to_str(const char* s) { return std::string("\"") + s + "\""; }
template <class T>
inline std::string to_str(T v) {
  if constexpr (std::is_enum_v<T>) return std::to_string(long(v));
  else if constexpr (std::is_same_v<T, bool>) return v ? "true" : "false";
  else { char buf[32]; std::snprintf(buf, sizeof buf, "%lld (0x%llX)", (long long)v, (unsigned long long)v); return buf; }
}

inline int finish(const char* name) {
  std::printf("%s: %d checks, %d failures\n", name, g_checks, g_failures);
  return g_failures ? 1 : 0;
}
}  // namespace test

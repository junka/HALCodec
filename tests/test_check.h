#ifndef TESTS_TEST_CHECK_H_
#define TESTS_TEST_CHECK_H_

#include <cstdio>
#include <sstream>
#include <string>

// Minimal assertion harness. The project has no test dependency and these
// cases are plain functions over bitstreams, so failures are collected rather
// than thrown: every CHECK runs, and the process exits non-zero if any of them
// failed. ctest then reports one target per area.
namespace haltest {

inline int& failures() {
    static int n = 0;
    return n;
}

inline bool check(bool ok, const char* file, int line, const std::string& what) {
    if (!ok) {
        std::printf("FAIL %s:%d  %s\n", file, line, what.c_str());
        // Flush now: a suite that crashes later would otherwise take the only
        // record of what failed down with it.
        std::fflush(stdout);
        ++failures();
    }
    return ok;
}

// Formats a value for the failure message only; the comparison itself is a
// plain ==, so the types just need an ostream operator.
template <class T>
std::string show(const T& v) {
    std::ostringstream os;
    os << v;
    return os.str();
}

template <class A, class B>
bool checkEq(const A& actual, const B& expected, const char* file, int line,
             const char* expr) {
    if (actual == expected) {
        return true;
    }
    return check(false, file, line,
                 std::string(expr) + "  actual=" + show(actual) +
                     " expected=" + show(expected));
}

inline int finish(const char* suite) {
    if (failures() != 0) {
        std::printf("%s: %d check(s) failed\n", suite, failures());
        return 1;
    }
    std::printf("%s: all checks passed\n", suite);
    return 0;
}

} // namespace haltest

#define CHECK(cond) (haltest::check((cond), __FILE__, __LINE__, #cond))
#define CHECK_EQ(actual, expected) \
    (haltest::checkEq((actual), (expected), __FILE__, __LINE__, #actual))
#define CHECK_NE(actual, unexpected) \
    (haltest::check((actual) != (unexpected), __FILE__, __LINE__, \
                    #actual " != " #unexpected))

#endif // TESTS_TEST_CHECK_H_

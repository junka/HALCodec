#ifndef TESTS_TEST_CHECK_H_
#define TESTS_TEST_CHECK_H_

#include <cstdio>
#include <sstream>
#include <string>
#include <type_traits>

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

// Detects whether T can be streamed to std::ostringstream. CHECK_EQ formats
// both operands only for the failure message, but the template is instantiated
// unconditionally at the call site, so a non-streamable type (e.g. a
// unique_ptr compared against nullptr) must not hard-error the build — it
// should fall back to a placeholder instead.
template <class T, class = void>
struct is_streamable : std::false_type {};

template <class T>
struct is_streamable<
    T, std::void_t<decltype(std::declval<std::ostringstream&>() <<
                            std::declval<const T&>())>> : std::true_type {};

// Formats a value for the failure message only; the comparison itself is a
// plain ==, so the types just need an ostream operator. When they don't have
// one, report a placeholder so the assertion still compiles and the real
// diagnostic is the expression text + the bool result.
template <class T>
std::string show(const T& v) {
    if constexpr (is_streamable<T>::value) {
        std::ostringstream os;
        os << v;
        return os.str();
    } else {
        (void)v;
        return "<non-streamable>";
    }
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

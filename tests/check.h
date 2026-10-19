#pragma once
// Tiny test registry: TEST(name) { CHECK(...); CHECK_EQ(a, b); }
#include <cstdint>
#include <cstdio>
#include <type_traits>
#include <vector>

namespace ens_test {

struct Case {
    const char* name;
    void (*fn)();
};

inline std::vector<Case>& registry() {
    static std::vector<Case> r;
    return r;
}

// Number of failed checks in the test currently running (reset by the runner).
inline int& failures() {
    static int n = 0;
    return n;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

// Sign-safe equality across integer types (no -Wsign-compare noise).
template <class A, class B>
bool equal(const A& a, const B& b) {
    if constexpr (std::is_integral_v<A> && std::is_integral_v<B>) {
        if constexpr (std::is_signed_v<A> == std::is_signed_v<B>) {
            return a == b;
        } else if constexpr (std::is_signed_v<A>) {
            return a >= 0 && static_cast<uint64_t>(a) == static_cast<uint64_t>(b);
        } else {
            return b >= 0 && static_cast<uint64_t>(a) == static_cast<uint64_t>(b);
        }
    } else {
        return a == b;
    }
}

template <class T>
void print_value(const T& v) {
    if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) {
        const long long x = static_cast<long long>(v);
        if (x < 0) std::printf("-0x%llX", 0ULL - static_cast<unsigned long long>(x));
        else std::printf("0x%llX", static_cast<unsigned long long>(x));
    } else if constexpr (std::is_integral_v<T>) {
        std::printf("0x%llX", static_cast<unsigned long long>(v));
    } else {
        std::printf("<value>");
    }
}

template <class A, class B>
void check_eq(const A& a, const B& b, const char* sa, const char* sb, const char* file,
              int line) {
    if (equal(a, b)) return;
    ++failures();
    std::printf("  FAIL %s:%d: CHECK_EQ(%s, %s)\n    left  = ", file, line, sa, sb);
    print_value(a);
    std::printf("\n    right = ");
    print_value(b);
    std::printf("\n");
}

}  // namespace ens_test

#define ENS_CONCAT2(a, b) a##b
#define ENS_CONCAT(a, b) ENS_CONCAT2(a, b)

#define TEST(name)                                                              \
    static void name();                                                         \
    static ::ens_test::Registrar ENS_CONCAT(ens_reg_, name)(#name, &name);      \
    static void name()

#define CHECK(cond)                                                             \
    do {                                                                        \
        if (!(cond)) {                                                          \
            ++::ens_test::failures();                                           \
            std::printf("  FAIL %s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
        }                                                                       \
    } while (0)

#define CHECK_EQ(a, b) ::ens_test::check_eq((a), (b), #a, #b, __FILE__, __LINE__)

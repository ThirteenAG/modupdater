#pragma once
#include <format>
#include <functional>
#include <string>
#include <vector>

// Minimal test framework: TEST_CASE registers a test, CHECK reports and continues, REQUIRE stops the test
namespace test
{
    struct Case
    {
        const char* name;
        void (*function)();
    };

    std::vector<Case>& Cases();
    void Report(const char* file, int line, const std::string& message, bool fatal);
    void Note(const std::string& message);

    struct Registrar
    {
        Registrar(const char* name, void (*function)()) { Cases().push_back({ name, function }); }
    };

    struct AbortTest {};

    template<typename A, typename B>
    std::string Describe(const A& a, const B& b)
    {
        if constexpr (std::is_arithmetic_v<A> && std::is_arithmetic_v<B>)
            return std::format(" ({} vs {})", a, b);
        else if constexpr (std::is_convertible_v<A, std::string_view> && std::is_convertible_v<B, std::string_view>)
            return std::format(" (\"{}\" vs \"{}\")", std::string_view(a), std::string_view(b));
        else
            return {};
    }
}

#define TEST_CASE(name) \
    static void name(); \
    static test::Registrar name##_registrar(#name, &name); \
    static void name()

#define CHECK(expr) \
    do { if (!(expr)) test::Report(__FILE__, __LINE__, #expr, false); } while (0)

#define REQUIRE(expr) \
    do { if (!(expr)) test::Report(__FILE__, __LINE__, #expr, true); } while (0)

#define CHECK_EQ(a, b) \
    do { auto&& va_ = (a); auto&& vb_ = (b); if (!(va_ == vb_)) test::Report(__FILE__, __LINE__, std::string(#a " == " #b) + test::Describe(va_, vb_), false); } while (0)

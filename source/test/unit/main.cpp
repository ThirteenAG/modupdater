#include <windows.h>
#include "test.h"
#include <chrono>
#include <cstdio>
#include <exception>
#include "log.h"

// UnitTests.exe [filter...]  runs all tests whose name contains one of the filters
namespace test
{
    namespace
    {
        int failures = 0;
        bool currentFailed = false;
    }

    std::vector<Case>& Cases()
    {
        static std::vector<Case> cases;
        return cases;
    }

    void Report(const char* file, int line, const std::string& message, bool fatal)
    {
        auto name = strrchr(file, '\\');
        printf("    FAILED %s:%d: %s\n", name ? name + 1 : file, line, message.c_str());
        currentFailed = true;
        if (fatal)
            throw AbortTest{};
    }

    void Note(const std::string& message)
    {
        printf("    %s\n", message.c_str());
    }
}

int main(int argc, char** argv)
{
    SetConsoleOutputCP(CP_UTF8);
    mu::LogSetFile(std::filesystem::temp_directory_path() / L"modupdater-tests" / L"UnitTests.log");

    int passed = 0, failed = 0, skipped = 0;
    auto start = std::chrono::steady_clock::now();

    for (auto& c : test::Cases())
    {
        if (argc > 1)
        {
            bool match = false;
            for (int i = 1; i < argc; i++)
                match = match || strstr(c.name, argv[i]) != nullptr;
            if (!match)
            {
                skipped++;
                continue;
            }
        }

        printf("%s\n", c.name);
        fflush(stdout);
        test::currentFailed = false;
        auto caseStart = std::chrono::steady_clock::now();
        try
        {
            c.function();
        }
        catch (const test::AbortTest&)
        {
        }
        catch (const std::exception& e)
        {
            test::Report(__FILE__, __LINE__, std::string("exception: ") + e.what(), false);
        }
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - caseStart).count();
        printf("    %s (%lld ms)\n", test::currentFailed ? "FAIL" : "ok", static_cast<long long>(ms));
        (test::currentFailed ? failed : passed)++;
    }

    auto seconds = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() / 1000.0;
    printf("\n%d passed, %d failed, %d skipped in %.1f s\n", passed, failed, skipped, seconds);
    return failed;
}

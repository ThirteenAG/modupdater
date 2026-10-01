#include <windows.h>
#include <shellapi.h>
#include "test.h"
#include <chrono>
#include <cstdio>
#include <exception>
#include <string>
#include "log.h"

// UnitTests.exe [filter...]  runs all tests whose name contains one of the filters.
// Online_ tests need the internet and only run when a filter selects them: UnitTests.exe Online
namespace test
{
    namespace
    {
        int failures = 0;
        bool currentFailed = false;

        // Modes for tests that need a second process (a copy of this executable in another folder),
        // -1 when the arguments are test filters
        int RunHelperMode()
        {
            int count = 0;
            LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &count);
            if (!argv)
                return -1;
            std::vector<std::wstring> args(argv, argv + count);
            LocalFree(argv);

            // the library did its work (pending files) while the process started
            if (args.size() >= 2 && args[1] == L"--startup")
                return 0;

            // --hold <file> <ms>: keeps a file open without delete sharing, like a game keeps its archives open,
            // and creates <file>.held once it is open
            if (args.size() >= 4 && args[1] == L"--hold")
            {
                HANDLE file = CreateFileW(args[2].c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
                if (file == INVALID_HANDLE_VALUE)
                    return 1;
                CloseHandle(CreateFileW((args[2] + L".held").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr));
                Sleep(static_cast<DWORD>(_wtoi(args[3].c_str())));
                CloseHandle(file);
                return 0;
            }
            return -1;
        }
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
    if (int code = test::RunHelperMode(); code >= 0)
        return code;

    SetConsoleOutputCP(CP_UTF8);
    mu::LogSetFile(std::filesystem::temp_directory_path() / L"modupdater-tests" / L"UnitTests.log");

    int passed = 0, failed = 0, skipped = 0;
    auto start = std::chrono::steady_clock::now();

    for (auto& c : test::Cases())
    {
        bool match = argc <= 1 && strncmp(c.name, "Online_", 7) != 0;
        for (int i = 1; i < argc; i++)
            match = match || strstr(c.name, argv[i]) != nullptr;
        if (!match)
        {
            skipped++;
            continue;
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

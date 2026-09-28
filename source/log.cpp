#include "stdafx.h"
#include "log.h"
#include "string_funcs.h"

namespace mu
{
    namespace
    {
        std::mutex logMutex;
        HANDLE logFile = INVALID_HANDLE_VALUE;
        std::filesystem::path logFilePath;
        bool logToConsole = false;

        void WriteToStdout(const std::wstring& line)
        {
            HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
            if (out == NULL || out == INVALID_HANDLE_VALUE)
                return;

            DWORD mode = 0, written = 0;
            if (GetConsoleMode(out, &mode))
            {
                WriteConsoleW(out, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
            }
            else
            {
                // redirected to a file or pipe
                auto utf8 = toString(line);
                WriteFile(out, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
            }
        }
    }

    std::wstring FormatArgs(std::wstring_view format, std::wformat_args args)
    {
        try
        {
            return std::vformat(format, args);
        }
        catch (const std::exception&)
        {
            return std::wstring(format);
        }
    }

    void LogSetFile(const std::filesystem::path& path, bool append)
    {
        std::lock_guard<std::mutex> lock(logMutex);

        if (logFile != INVALID_HANDLE_VALUE)
        {
            CloseHandle(logFile);
            logFile = INVALID_HANDLE_VALUE;
        }
        logFilePath.clear();

        if (path.empty())
            return;

        std::error_code ec;
        if (path.has_parent_path())
            std::filesystem::create_directories(path.parent_path(), ec);

        logFile = CreateFileW(path.c_str(), append ? FILE_APPEND_DATA : GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, append ? OPEN_ALWAYS : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

        if (logFile != INVALID_HANDLE_VALUE)
            logFilePath = path;
    }

    void LogCloseFile()
    {
        LogSetFile({});
    }

    std::filesystem::path LogGetFile()
    {
        std::lock_guard<std::mutex> lock(logMutex);
        return logFilePath;
    }

    void LogSetConsole(bool enabled)
    {
        std::lock_guard<std::mutex> lock(logMutex);
        logToConsole = enabled;
    }

    void LogWrite(std::wstring_view message)
    {
        SYSTEMTIME st;
        GetLocalTime(&st);
        auto line = std::format(L"{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03} [{:5}] {}\r\n",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, GetCurrentThreadId(), message);

        std::lock_guard<std::mutex> lock(logMutex);

        OutputDebugStringW((L"[modupdater] " + std::wstring(message) + L"\n").c_str());

        if (logFile != INVALID_HANDLE_VALUE)
        {
            auto utf8 = toString(line);
            DWORD written = 0;
            WriteFile(logFile, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
        }

        if (logToConsole)
            WriteToStdout(std::wstring(message) + L"\n");
    }
}

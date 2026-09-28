#pragma once
#include <filesystem>
#include <format>
#include <string>
#include <string_view>

// Thread-safe logger. Messages always go to the debugger (OutputDebugString),
// and additionally to a UTF-8 log file and/or the console when enabled.
namespace mu
{
    void LogSetFile(const std::filesystem::path& path, bool append = false);
    void LogCloseFile();
    std::filesystem::path LogGetFile();
    void LogSetConsole(bool enabled);
    void LogWrite(std::wstring_view message);

    // std::vformat is only instantiated in log.cpp, keeps the objects of the static library small
    std::wstring FormatArgs(std::wstring_view format, std::wformat_args args);

    // std::format with the format string checked at compile time
    template<typename... Args>
    std::wstring Format(std::wformat_string<Args...> format, Args&&... args)
    {
        return FormatArgs(format.get(), std::make_wformat_args(args...));
    }

    template<typename... Args>
    void Log(std::wformat_string<Args...> format, Args&&... args)
    {
        LogWrite(FormatArgs(format.get(), std::make_wformat_args(args...)));
    }
}

//-----------------------------------------------------------------------------
// Copyright (c) 2022 Quentin Quadrat <lecrapouille@gmail.com>
// https://github.com/Lecrapouille/zipper distributed under MIT License.
// Based on https://github.com/sebastiandev/zipper/tree/v2.x.y distributed under
// MIT License. Copyright (c) 2015 -- 2022 Sebastian <devsebas@gmail.com>
//-----------------------------------------------------------------------------

#include "Timestamp.hpp"
#include <ctime>
#include <chrono>
#if !defined(USE_WINDOWS)
#  include <sys/stat.h>
#endif

using namespace zipper;

// -----------------------------------------------------------------------------
Timestamp::Timestamp()
{
    std::time_t now = std::time(nullptr);
    timestamp = *std::localtime(&now);
}

// -----------------------------------------------------------------------------
Timestamp::Timestamp(const std::string& filepath)
{
    // Set default
    std::time_t now = std::time(nullptr);
    timestamp = *std::localtime(&now);

#if defined(USE_WINDOWS)

    // Implementation based on Ian Boyd's
    // https://stackoverflow.com/questions/20370920/convert-current-time-from-windows-to-unix-timestamp-in-c-or-c
    HANDLE hFile1;
    FILETIME filetime;
    hFile1 = CreateFileA(filepath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (hFile1 == INVALID_HANDLE_VALUE)
    {
        return;
    }

    if (!GetFileTime(hFile1, &filetime, nullptr, nullptr))
    {
        CloseHandle(hFile1);
        return;
    }
    const int64_t UNIX_TIME_START = 0x019DB1DED53E8000; //January 1, 1970 (start of Unix epoch) in "ticks"
    const int64_t TICKS_PER_SECOND = 10000000; //a tick is 100ns

    // Copy the low and high parts of FILETIME into a LARGE_INTEGER
    // This is so we can access the full 64-bits as an Int64 without causing an
    // alignment fault.
    LARGE_INTEGER li;
    li.LowPart = filetime.dwLowDateTime;
    li.HighPart = filetime.dwHighDateTime;

    //Convert ticks since 1/1/1970 into seconds
    time_t time_s = (li.QuadPart - UNIX_TIME_START) / TICKS_PER_SECOND;

    timestamp = *std::localtime(&time_s);
    CloseHandle(hFile1);

#else // !USE_WINDOWS

    struct stat buf;
    if (stat(filepath.c_str(), &buf) != 0)
    {
        return;
    }

#  if defined(__APPLE__)
    auto timet = static_cast<time_t>(buf.st_mtimespec.tv_sec);
#  else
    auto timet = static_cast<time_t>(buf.st_mtim.tv_sec);
#  endif

    timestamp = *std::localtime(&timet);

#endif // USE_WINDOWS
}

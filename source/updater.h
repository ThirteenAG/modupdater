#pragma once
#include <windows.h>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>
#include "extract.h"

struct FileUpdateInfo
{
    std::wstring wszFullFilePath;
    std::wstring wszFileName;
    std::wstring wszDownloadURL;
    std::wstring wszDownloadName;
    std::string  szPassword;
    int32_t nRemoteFileUpdatedHoursAgo = 0;
    int32_t nLocaFileUpdatedHoursAgo = 0;
    uint64_t nFileSize = 0;
};

namespace mu::updater
{
    // A local file that can be updated
    struct Candidate
    {
        std::filesystem::path path;
        std::wstring url;
        std::wstring devUrl;
        FILETIME lastWriteTime = {};
        std::wstring machine;       // "x86", "x64" or empty
        bool alwaysUpdate = false;
        std::string password;
    };

    // Asks the servers about every candidate and returns the files that have updates
    std::vector<FileUpdateInfo> CheckForUpdates(const std::vector<Candidate>& candidates, const std::string& token);

    struct Status
    {
        std::wstring text;
        int percent = 0;            // 0..100
    };

    struct ApplyResult
    {
        bool cancelled = false;
        int filesWritten = 0;
        int filesInUse = 0;
        std::vector<std::wstring> errors;

        bool ok() const { return !cancelled && errors.empty(); }
    };

    // Downloads and installs one update. 'status' is called with progress updates and returns
    // false to cancel.
    ApplyResult ApplyUpdate(const FileUpdateInfo& update, IniMode iniMode, const std::string& token, const std::function<bool(const Status&)>& status);

    int32_t HoursSinceFileTime(const FILETIME& time);
}

void InitModupdater();

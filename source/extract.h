#pragma once
#include "archive.h"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace mu
{
    // How existing .ini files are treated (same order as the updater's radio buttons / MU_INI_*)
    enum class IniMode
    {
        Merge = 0,   // replace all and keep settings
        Replace = 1, // replace all and discard settings
        Skip = 2,    // don't replace
    };

    struct ExtractOptions
    {
        IniMode iniMode = IniMode::Merge;
        std::string password;
        bool checkDiskSpace = true;
    };

    struct ExtractProgress
    {
        uint64_t done = 0;          // uncompressed bytes processed
        uint64_t total = 0;
        std::wstring currentFile;   // entry name
    };

    struct ExtractReport
    {
        int written = 0;            // files written (including merged ini files)
        int merged = 0;             // ini files that kept the existing settings
        int skipped = 0;            // existing ini files that were left alone
        int inUse = 0;              // files that were in use; the old copies are removed on next launch
        bool cancelled = false;
        std::vector<std::wstring> errors;

        bool ok() const { return !cancelled && errors.empty(); }
    };

    // Return false to cancel
    using ExtractCallback = std::function<bool(const ExtractProgress&)>;

    // Validates all entries (nothing is written if any of them would end up outside 'root'),
    // checks the free disk space and extracts the archive. Every file is written to a temp
    // file next to its target first and then moved into place.
    bool ExtractArchive(zip::Reader& reader, const std::filesystem::path& root, const ExtractOptions& options, const ExtractCallback& callback, ExtractReport& report);

    // Keeps the values of 'oldIni' in 'newIni' (keys and sections that only exist in the
    // old file are kept as well). 'newIni' is rewritten in place, keeping its layout and comments.
    bool MergeIniFiles(const std::filesystem::path& oldIni, const std::filesystem::path& newIni);
}

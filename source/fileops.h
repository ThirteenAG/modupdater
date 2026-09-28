#pragma once
#include <windows.h>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mu
{
    // Suffix used for files that were in use and had to be moved out of the way.
    // They are removed on the next launch (see CleanupLeftovers).
    constexpr wchar_t kDeleteOnNextLaunchSuffix[] = L".deleteonnextlaunch";
    // Suffix of temporary files created while a new file is written next to its target.
    constexpr wchar_t kTempFileSuffix[] = L".mu-tmp";

    std::wstring Win32ErrorMessage(DWORD error);

    // Joins an archive entry name (forward or back slashes) to root. Returns nullopt and sets
    // 'error' if the name is absolute, contains '..', a drive or stream separator, reserved
    // device names or invalid characters, i.e. anything that could escape root (zip slip).
    std::optional<std::filesystem::path> SafeJoin(const std::filesystem::path& root, std::wstring_view entryName, std::wstring* error = nullptr);

    // Lexical, case-insensitive check that 'child' is located inside 'root'
    bool IsPathInside(const std::filesystem::path& root, const std::filesystem::path& child);

    // Returns true if a file can be created in the folder (or in its closest existing parent,
    // when 'createIfMissing' is false). Does not leave anything behind.
    bool TestWriteAccess(const std::filesystem::path& folder, bool createIfMissing = false);

    // Free space available to the caller on the volume of 'path' (or its closest existing parent)
    std::optional<uint64_t> GetFreeDiskSpace(const std::filesystem::path& path);

    enum class ReplaceResult
    {
        Replaced,       // target now has the new content
        ReplacedInUse,  // target was in use; old file was renamed to *.deleteonnextlaunch
        Failed,
    };

    // Moves 'source' over 'target'. Clears the read-only attribute and, when the target is in use
    // (e.g. a loaded dll), renames it to target.deleteonnextlaunch first. 'source' must be on the
    // same volume (it is normally a temp file next to the target).
    ReplaceResult ReplaceFileWith(const std::filesystem::path& target, const std::filesystem::path& source, std::wstring* error = nullptr);

    // Sends a file to the recycle bin (or deletes it where there is no recycle bin)
    bool MoveToRecycleBin(const std::filesystem::path& file);

    // Recursive file enumeration that does not follow junctions/symlinks
    void FindFilesRecursively(const std::filesystem::path& directory, const std::function<void(const std::filesystem::path&, const WIN32_FIND_DATAW&)>& callback, bool recursive = true);

    // Removes *.deleteonnextlaunch and stale temp files left by previous runs
    void CleanupLeftovers(const std::filesystem::path& directory);

    // Machine type of a PE image on disk (IMAGE_FILE_MACHINE_*), 0 if unknown
    WORD GetImageMachine(const std::filesystem::path& file);

    // Machine type of a loaded module
    WORD GetModuleMachine(HMODULE module);

    std::filesystem::path GetModuleFilePath(HMODULE module);

    // Returns true if a process whose executable lives inside 'folder' is currently running.
    // 'exeNames' (optional) restricts the check to these file names.
    bool IsProcessRunningFrom(const std::filesystem::path& folder, const std::vector<std::wstring>& exeNames, std::wstring* runningExe = nullptr);
}

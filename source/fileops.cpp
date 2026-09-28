#include "stdafx.h"
#include "fileops.h"
#include "log.h"
#include "string_funcs.h"
#include <shellapi.h>
#include <tlhelp32.h>

namespace mu
{
    std::wstring Win32ErrorMessage(DWORD error)
    {
        wchar_t* buffer = nullptr;
        DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, error, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);

        std::wstring message;
        if (length && buffer)
        {
            message.assign(buffer, length);
            while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' ' || message.back() == L'.'))
                message.pop_back();
        }
        if (buffer)
            LocalFree(buffer);

        if (message.empty())
            message = L"Unknown error";

        return Format(L"{} (error {})", message, error);
    }

    namespace
    {
        bool IsReservedDeviceName(std::wstring_view component)
        {
            // "CON", "con.txt", "COM1.ini" etc. are devices on Windows regardless of the extension
            auto base = component.substr(0, component.find(L'.'));
            while (!base.empty() && base.back() == L' ')
                base.remove_suffix(1);

            static constexpr const wchar_t* reserved[] = { L"CON", L"PRN", L"AUX", L"NUL", L"CONIN$", L"CONOUT$" };
            for (auto name : reserved)
            {
                if (iequals(base, name))
                    return true;
            }

            if (base.size() == 4 && (starts_with(base, L"COM", false) || starts_with(base, L"LPT", false)))
            {
                wchar_t c = base[3];
                if ((c >= L'0' && c <= L'9') || c == L'¹' || c == L'²' || c == L'³')
                    return true;
            }
            return false;
        }

        std::filesystem::path ClosestExistingDirectory(std::filesystem::path path)
        {
            std::error_code ec;
            path = path.lexically_normal();
            while (!path.empty())
            {
                if (std::filesystem::is_directory(path, ec))
                    return path;
                auto parent = path.parent_path();
                if (parent == path)
                    break;
                path = parent;
            }
            return {};
        }
    }

    bool IsPathInside(const std::filesystem::path& root, const std::filesystem::path& child)
    {
        auto normalize = [](const std::filesystem::path& p)
        {
            auto s = toLowerWStr(p.lexically_normal().make_preferred().wstring());
            if (!s.empty() && s.back() != L'\\')
                s += L'\\';
            return s;
        };

        auto rootStr = normalize(root);
        auto childStr = normalize(child);
        return !rootStr.empty() && childStr.size() > rootStr.size() && childStr.starts_with(rootStr);
    }

    std::optional<std::filesystem::path> SafeJoin(const std::filesystem::path& root, std::wstring_view entryName, std::wstring* error)
    {
        auto fail = [&](std::wstring_view reason) -> std::optional<std::filesystem::path>
        {
            if (error)
                *error = Format(L"unsafe path '{}' ({})", entryName, reason);
            return std::nullopt;
        };

        if (entryName.empty())
            return fail(L"empty name");

        std::wstring name(entryName);
        std::replace(name.begin(), name.end(), L'/', L'\\');

        if (name.front() == L'\\')
            return fail(L"absolute path");
        if (name.find(L':') != std::wstring::npos)
            return fail(L"drive or stream separator");

        std::filesystem::path result = root;
        bool hasComponents = false;
        size_t start = 0;
        while (start <= name.size())
        {
            size_t end = name.find(L'\\', start);
            if (end == std::wstring::npos)
                end = name.size();

            std::wstring_view component(name.data() + start, end - start);
            start = end + 1;

            if (component.empty() || component == L".")
                continue;
            if (component.find_first_not_of(L". ") == std::wstring_view::npos)
                return fail(L"parent directory reference");
            for (wchar_t c : component)
            {
                if (c < 32 || c == L'<' || c == L'>' || c == L'"' || c == L'|' || c == L'?' || c == L'*')
                    return fail(L"invalid character");
            }
            if (IsReservedDeviceName(component))
                return fail(L"reserved device name");

            result /= std::wstring(component);
            hasComponents = true;
        }

        if (!hasComponents)
            return fail(L"empty name");
        if (!IsPathInside(root, result))
            return fail(L"outside of the target folder");

        return result;
    }

    bool TestWriteAccess(const std::filesystem::path& folder, bool createIfMissing)
    {
        if (folder.empty())
            return false;

        std::error_code ec;
        std::filesystem::path dir = folder;
        if (!std::filesystem::is_directory(dir, ec))
        {
            if (createIfMissing)
            {
                std::filesystem::create_directories(dir, ec);
                if (ec)
                    return false;
            }
            else
            {
                dir = ClosestExistingDirectory(dir);
                if (dir.empty())
                    return false;
            }
        }

        auto testFile = dir / Format(L".modupdater_writetest_{}_{}.tmp", GetCurrentProcessId(), GetTickCount64());
        HANDLE hFile = CreateFileW(testFile.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_HIDDEN | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        if (hFile != INVALID_HANDLE_VALUE)
        {
            CloseHandle(hFile);
            return true;
        }
        return false;
    }

    std::optional<uint64_t> GetFreeDiskSpace(const std::filesystem::path& path)
    {
        auto dir = ClosestExistingDirectory(path);
        if (dir.empty())
            return std::nullopt;

        ULARGE_INTEGER freeToCaller{};
        auto dirStr = dir.wstring();
        if (!dirStr.ends_with(L'\\'))
            dirStr += L'\\';
        if (!GetDiskFreeSpaceExW(dirStr.c_str(), &freeToCaller, nullptr, nullptr))
            return std::nullopt;
        return freeToCaller.QuadPart;
    }

    ReplaceResult ReplaceFileWith(const std::filesystem::path& target, const std::filesystem::path& source, std::wstring* error)
    {
        DWORD attrs = GetFileAttributesW(target.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES)
        {
            if (attrs & FILE_ATTRIBUTE_DIRECTORY)
            {
                if (error)
                    *error = L"a folder with the same name already exists";
                return ReplaceResult::Failed;
            }
            if (attrs & FILE_ATTRIBUTE_READONLY)
                SetFileAttributesW(target.c_str(), attrs & ~FILE_ATTRIBUTE_READONLY);
        }

        if (MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return ReplaceResult::Replaced;

        DWORD err = GetLastError();
        if (err == ERROR_ACCESS_DENIED || err == ERROR_SHARING_VIOLATION || err == ERROR_LOCK_VIOLATION || err == ERROR_USER_MAPPED_FILE)
        {
            // The target is in use (for example a plugin loaded by the game). Windows allows renaming
            // a loaded module, so move it out of the way and put the new file in its place.
            for (int i = 0; i < 16; i++)
            {
                std::filesystem::path backup = target;
                backup += (i == 0) ? std::wstring(kDeleteOnNextLaunchSuffix) : Format(L".{}{}", i, kDeleteOnNextLaunchSuffix);

                if (GetFileAttributesW(backup.c_str()) != INVALID_FILE_ATTRIBUTES)
                {
                    SetFileAttributesW(backup.c_str(), FILE_ATTRIBUTE_NORMAL);
                    if (!DeleteFileW(backup.c_str()))
                        continue; // a previous backup that is still in use, try the next name
                }

                if (!MoveFileExW(target.c_str(), backup.c_str(), 0))
                {
                    err = GetLastError();
                    break;
                }

                if (MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH))
                {
                    Log(L"{} is in use, the old file was renamed to {}", target.filename().wstring(), backup.filename().wstring());
                    return ReplaceResult::ReplacedInUse;
                }

                err = GetLastError();
                MoveFileExW(backup.c_str(), target.c_str(), 0); // roll back
                break;
            }
        }

        if (error)
            *error = Win32ErrorMessage(err);
        return ReplaceResult::Failed;
    }

    bool MoveToRecycleBin(const std::filesystem::path& file)
    {
        if (GetFileAttributesW(file.c_str()) == INVALID_FILE_ATTRIBUTES)
            return true;

        std::wstring from = file.wstring();
        from.append(2, L'\0'); // double null terminated list

        SHFILEOPSTRUCTW fileOp = {};
        fileOp.wFunc = FO_DELETE;
        fileOp.pFrom = from.c_str();
        fileOp.fFlags = FOF_ALLOWUNDO | FOF_NOERRORUI | FOF_NOCONFIRMATION | FOF_SILENT;
        if (SHFileOperationW(&fileOp) == 0 && !fileOp.fAnyOperationsAborted)
            return true;

        // no recycle bin (network drive etc.) or shell failure
        SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_NORMAL);
        return DeleteFileW(file.c_str()) != FALSE;
    }

    void FindFilesRecursively(const std::filesystem::path& directory, const std::function<void(const std::filesystem::path&, const WIN32_FIND_DATAW&)>& callback, bool recursive)
    {
        WIN32_FIND_DATAW fd;
        auto pattern = directory / L"*";
        HANDLE search = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
        if (search == INVALID_HANDLE_VALUE)
            return;

        std::vector<std::filesystem::path> directories;
        do
        {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L".."))
                    continue;
                // do not follow junctions and symlinks, they can point back up the tree
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                    directories.push_back(directory / fd.cFileName);
            }
            else
            {
                callback(directory / fd.cFileName, fd);
            }
        } while (FindNextFileW(search, &fd));
        FindClose(search);

        if (recursive)
        {
            for (auto& dir : directories)
                FindFilesRecursively(dir, callback, true);
        }
    }

    void CleanupLeftovers(const std::filesystem::path& directory)
    {
        FindFilesRecursively(directory, [](const std::filesystem::path& file, const WIN32_FIND_DATAW&)
        {
            auto name = file.filename().wstring();
            if (ends_with(name, kDeleteOnNextLaunchSuffix, false))
            {
                if (MoveToRecycleBin(file))
                    Log(L"Deleted {}", file.wstring());
            }
            else if (ends_with(name, kTempFileSuffix, false) || ends_with(name, L".modupdater", false) ||
                (starts_with(name, L".modupdater_", false) && ends_with(name, L".tmp", false)))
            {
                SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_NORMAL);
                if (DeleteFileW(file.c_str()))
                    Log(L"Deleted leftover {}", file.wstring());
            }
        });
    }

    WORD GetImageMachine(const std::filesystem::path& file)
    {
        HANDLE hFile = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE)
            return 0;

        WORD machine = 0;
        IMAGE_DOS_HEADER dos{};
        DWORD read = 0;
        if (ReadFile(hFile, &dos, sizeof(dos), &read, nullptr) && read == sizeof(dos) && dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew > 0)
        {
            LARGE_INTEGER pos;
            pos.QuadPart = dos.e_lfanew;
            DWORD signature = 0;
            IMAGE_FILE_HEADER header{};
            if (SetFilePointerEx(hFile, pos, nullptr, FILE_BEGIN) &&
                ReadFile(hFile, &signature, sizeof(signature), &read, nullptr) && read == sizeof(signature) && signature == IMAGE_NT_SIGNATURE &&
                ReadFile(hFile, &header, sizeof(header), &read, nullptr) && read == sizeof(header))
            {
                machine = header.Machine;
            }
        }
        CloseHandle(hFile);
        return machine;
    }

    WORD GetModuleMachine(HMODULE module)
    {
        if (!module)
            return 0;
        auto base = reinterpret_cast<const uint8_t*>(module);
        auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
            return 0;
        auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE)
            return 0;
        return nt->FileHeader.Machine;
    }

    std::filesystem::path GetModuleFilePath(HMODULE module)
    {
        std::wstring buffer(MAX_PATH, L'\0');
        for (int i = 0; i < 8; i++)
        {
            DWORD length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (length == 0)
                return {};
            if (length < buffer.size())
            {
                buffer.resize(length);
                return buffer;
            }
            buffer.resize(buffer.size() * 2);
        }
        return {};
    }

    bool IsProcessRunningFrom(const std::filesystem::path& folder, const std::vector<std::wstring>& exeNames, std::wstring* runningExe)
    {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
            return false;

        bool found = false;
        PROCESSENTRY32W entry = { sizeof(entry) };
        for (BOOL ok = Process32FirstW(snapshot, &entry); ok && !found; ok = Process32NextW(snapshot, &entry))
        {
            if (entry.th32ProcessID == GetCurrentProcessId())
                continue;

            if (!exeNames.empty() && std::none_of(exeNames.begin(), exeNames.end(), [&](const std::wstring& n) { return iequals(n, entry.szExeFile); }))
                continue;

            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
            if (!process)
                continue;

            wchar_t imagePath[MAX_PATH * 4];
            DWORD size = static_cast<DWORD>(std::size(imagePath));
            if (QueryFullProcessImageNameW(process, 0, imagePath, &size) && IsPathInside(folder, imagePath))
            {
                found = true;
                if (runningExe)
                    *runningExe = entry.szExeFile;
            }
            CloseHandle(process);
        }
        CloseHandle(snapshot);
        return found;
    }
}

#include "stdafx.h"
#include "extract.h"
#include "fileops.h"
#include "inimerge.h"
#include "log.h"
#include "string_funcs.h"

namespace mu
{
    namespace
    {
        bool ReadWholeFile(const std::filesystem::path& file, std::string& content)
        {
            std::ifstream in(file, std::ios::binary);
            if (!in)
                return false;
            content.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            return !in.bad();
        }
    }

    bool MergeIniFiles(const std::filesystem::path& oldIni, const std::filesystem::path& newIni)
    {
        try
        {
            std::string oldText, newText;
            if (!ReadWholeFile(oldIni, oldText) || !ReadWholeFile(newIni, newText))
            {
                Log(L"Cannot merge {}: {}", oldIni.wstring(), Win32ErrorMessage(GetLastError()));
                return false;
            }
            if (oldText.find('=') == std::string::npos)
                return false; // no settings

            auto merged = MergeIniText(oldText, newText);
            if (merged == newText)
                return true;

            // a failed write leaves the new file as it is
            auto temp = newIni;
            temp += kTempFileSuffix;
            {
                std::ofstream out(temp, std::ios::binary | std::ios::trunc);
                out.write(merged.data(), static_cast<std::streamsize>(merged.size()));
                out.close();
                if (out && MoveFileExW(temp.c_str(), newIni.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                    return true;
            }
            Log(L"Cannot merge {}: {}", oldIni.wstring(), Win32ErrorMessage(GetLastError()));
            DeleteFileW(temp.c_str());
            return false;
        }
        catch (const std::exception& e)
        {
            Log(L"Cannot merge {}: {}", oldIni.wstring(), toWString(e.what()));
            return false;
        }
    }

    bool ExtractArchive(zip::Reader& reader, const std::filesystem::path& root, const ExtractOptions& options, const ExtractCallback& callback, ExtractReport& report)
    {
        struct Item
        {
            const zip::Entry* entry;
            std::filesystem::path target;
        };

        std::vector<Item> items;
        uint64_t total = 0, needed = 0, largest = 0;

        for (auto& entry : reader.Entries())
        {
            std::wstring error;
            auto target = SafeJoin(root, entry.name, &error);
            if (!target)
            {
                report.errors.push_back(L"The archive contains an " + error + L", nothing was installed.");
                return false;
            }

            if (entry.encrypted && options.password.empty())
            {
                report.errors.push_back(L"The archive is password protected.");
                return false;
            }

            items.push_back({ &entry, *target });
            if (!entry.isDirectory)
            {
                total += entry.uncompressedSize;
                largest = std::max(largest, entry.uncompressedSize);

                WIN32_FILE_ATTRIBUTE_DATA data;
                uint64_t existing = 0;
                if (GetFileAttributesExW(target->c_str(), GetFileExInfoStandard, &data))
                    existing = (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
                if (entry.uncompressedSize > existing)
                    needed += entry.uncompressedSize - existing;
            }
        }

        if (options.checkDiskSpace)
        {
            needed += largest + 1024 * 1024; // temp copy of the file being written
            if (auto free = GetFreeDiskSpace(root); free && *free < needed)
            {
                report.errors.push_back(Format(L"Not enough disk space on {}. Required: {}, available: {}.",
                    root.root_path().wstring(), formatBytesW(needed), formatBytesW(*free)));
                return false;
            }
        }

        ExtractProgress progress;
        progress.total = total;

        for (auto& item : items)
        {
            auto& entry = *item.entry;
            auto& target = item.target;

            progress.currentFile = entry.name;
            if (callback && !callback(progress))
            {
                report.cancelled = true;
                break;
            }

            std::error_code ec;
            if (entry.isDirectory)
            {
                std::filesystem::create_directories(target, ec);
                if (ec)
                    report.errors.push_back(Format(L"{}: {}", entry.name, toWString(ec.message())));
                continue;
            }

            std::filesystem::create_directories(target.parent_path(), ec);
            if (ec)
            {
                report.errors.push_back(Format(L"{}: cannot create folder {}: {}", entry.name, target.parent_path().wstring(), toWString(ec.message())));
                progress.done += entry.uncompressedSize;
                continue;
            }

            const bool isIni = iequals(target.extension().wstring(), L".ini");
            const bool exists = GetFileAttributesW(target.c_str()) != INVALID_FILE_ATTRIBUTES;

            if (isIni && exists && options.iniMode == IniMode::Skip)
            {
                report.skipped++;
                progress.done += entry.uncompressedSize;
                Log(L"{} was kept (don't replace ini files)", entry.name);
                continue;
            }

            auto temp = target;
            temp += kTempFileSuffix;
            const uint64_t base = progress.done;
            bool cancelled = false;
            bool extracted = reader.ExtractToFile(entry, options.password, temp, [&](uint64_t bytes)
            {
                progress.done = base + bytes;
                if (callback && !callback(progress))
                    cancelled = true;
                return !cancelled;
            });

            progress.done = base + entry.uncompressedSize;

            if (!extracted)
            {
                DeleteFileW(temp.c_str());
                if (cancelled)
                {
                    report.cancelled = true;
                    break;
                }
                report.errors.push_back(reader.Error());
                Log(L"Error: {}", reader.Error());
                continue;
            }

            if (isIni && exists)
            {
                if (options.iniMode == IniMode::Merge && MergeIniFiles(target, temp))
                {
                    report.merged++;
                    Log(L"{} merged, existing settings were kept", entry.name);
                }
                // the previous settings can be restored from the recycle bin
                MoveToRecycleBin(target);
            }

            std::wstring error;
            switch (ReplaceFileWith(target, temp, &error, options.allowPending))
            {
            case ReplaceResult::Replaced:
                report.written++;
                Log(L"{} was updated successfully.", entry.name);
                break;
            case ReplaceResult::ReplacedInUse:
                report.written++;
                report.inUse++;
                Log(L"{} was updated successfully (it was in use).", entry.name);
                break;
            case ReplaceResult::Pending:
            {
                auto pending = target;
                pending += kPendingSuffix;
                report.pending.push_back({ entry.name, pending });
                Log(L"{} is in use, it will be updated on the next launch.", entry.name);
                break;
            }
            case ReplaceResult::Failed:
                DeleteFileW(temp.c_str());
                report.errors.push_back(Format(L"{}: {}", entry.name, error));
                Log(L"Error: cannot write {}: {}", target.wstring(), error);
                break;
            }
        }

        if (callback && !report.cancelled)
        {
            progress.done = progress.total;
            progress.currentFile.clear();
            callback(progress);
        }

        return report.ok();
    }
}

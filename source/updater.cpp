#include "stdafx.h"
#include "updater.h"
#include "string_funcs.h"
#include "ModuleList.hpp"
#include "libmodupdater.h"
#include "archive.h"
#include "fileops.h"
#include "http.h"
#include "log.h"
#include "registry.h"
#include "remote.h"
#include "ui_common.h"
#include <IniReader.h>
#include <date.h>
#include "mINI\src\mini\ini.h"

using namespace mu;

constexpr auto maxContentLength = 220;
// Marker of the standalone modupdater.asi used by old versions, session wide
constexpr auto mtxNameAsiLegacy = L"MODUPDATERASI-0TAPXVW8TY18N5SEP5CW7I4UE1FKOJ";

#ifndef STATICLIB
CIniReader iniReader;
bool muAlwaysUpdate = false;
#endif
bool muSkipUpdateCompleteDialog = false;
std::filesystem::path modulePath, processPath;
std::filesystem::path selfPath, selfName, selfNameNoExt;
std::string token;
bool reqElev;

#define UPDATEURL    L"UpdateUrl"
#define DEVUPDATEURL L"DevUpdateUrl"
#define BUTTONID1    1001
#define BUTTONID2    1002
#define BUTTONID3    1003
#define BUTTONID4    1004
#define BUTTONID5    1005
#define RBUTTONID1   1011
#define RBUTTONID2   1012
#define RBUTTONID3   1013

namespace
{
    // Set for the game that the updater restarts: the files that were in use are free once the old process has exited
    constexpr wchar_t kWaitForProcessVariable[] = L"MODUPDATER_WAIT_PID";

    std::vector<std::wstring>& StartupMessages()
    {
        static std::vector<std::wstring> messages;
        return messages;
    }

    void LogStartupMessages()
    {
        for (auto& message : StartupMessages())
            Log(L"{}", message);
        StartupMessages().clear();
    }

    // Files that the game kept open during an update (archives it reads all the time) wait next to their targets.
    // They are put in place while the next process loads this module, before the game opens them again. This runs
    // during the static initialization of the module: no Log() yet, InitModupdater logs the messages.
    struct PendingFilesAtStartup
    {
        PendingFilesAtStartup()
        {
            try
            {
                DWORD waitFor = 0;
                wchar_t buffer[16] = {};
                DWORD length = GetEnvironmentVariableW(kWaitForProcessVariable, buffer, static_cast<DWORD>(std::size(buffer)));
                if (length > 0 && length < std::size(buffer))
                {
                    waitFor = wcstoul(buffer, nullptr, 10);
                    SetEnvironmentVariableW(kWaitForProcessVariable, nullptr); // not for the processes the game starts
                }

                auto list = GetModuleFilePath(nullptr).parent_path() / kPendingListName;
                if (GetFileAttributesW(list.c_str()) == INVALID_FILE_ATTRIBUTES)
                    return;

                if (waitFor && waitFor != GetCurrentProcessId())
                {
                    if (HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, waitFor))
                    {
                        WaitForSingleObject(process, 15000);
                        CloseHandle(process);
                    }
                }

                auto result = ApplyPendingFiles(list);
                for (auto& file : result.applied)
                    StartupMessages().push_back(Format(L"{} was updated, it was in use during the update.", file.wstring()));
                for (auto& file : result.remaining)
                    StartupMessages().push_back(Format(L"{} is still in use, it waits for the next launch.", file.wstring()));
            }
            catch (...)
            {
                // never take the game down while it loads
            }
        }
    } pendingFilesAtStartup;

    std::wstring PluginMarkerName()
    {
        return Format(L"Local\\modupdater.plugin.{}", GetCurrentProcessId());
    }

    // True if the standalone modupdater.asi is loaded in this process, it updates everything by itself
    bool IsStandalonePluginLoaded()
    {
        if (HANDLE marker = OpenMutexW(SYNCHRONIZE, FALSE, PluginMarkerName().c_str()))
        {
            CloseHandle(marker);
            return true;
        }

        // old versions of the plugin only create a session wide mutex, make sure it belongs to this process
        if (HANDLE legacy = OpenMutexW(SYNCHRONIZE, FALSE, mtxNameAsiLegacy))
        {
            CloseHandle(legacy);
            ModuleList dlls;
            dlls.Enumerate(ModuleList::SearchLocation::All);
            for (auto& e : dlls.m_moduleList)
            {
                if (starts_with(std::get<std::wstring>(e), L"modupdater", false))
                    return true;
            }
        }
        return false;
    }

    // Pads the progress text so the TaskDialog does not change its size while the text changes
    std::wstring PadContent(std::wstring_view str)
    {
        std::wstring result(str);
        size_t visible = ui::GetVisibleTextLength(str);
        if (visible < maxContentLength)
            result.append(maxContentLength - visible, L' ');
        return result;
    }

    bool CanAccessFolder(const std::filesystem::path& folderName, DWORD genericAccessRights)
    {
        bool bRet = false;
        DWORD length = 0;
        if (!::GetFileSecurityW(folderName.c_str(), OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, NULL, NULL, &length) && ERROR_INSUFFICIENT_BUFFER == ::GetLastError())
        {
            std::vector<uint8_t> buffer(length);
            PSECURITY_DESCRIPTOR security = buffer.data();
            if (::GetFileSecurityW(folderName.c_str(), OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, security, length, &length))
            {
                HANDLE hToken = NULL;
                if (::OpenProcessToken(::GetCurrentProcess(), TOKEN_IMPERSONATE | TOKEN_QUERY | TOKEN_DUPLICATE | STANDARD_RIGHTS_READ, &hToken))
                {
                    HANDLE hImpersonatedToken = NULL;
                    if (::DuplicateToken(hToken, SecurityImpersonation, &hImpersonatedToken))
                    {
                        GENERIC_MAPPING mapping = { 0xFFFFFFFF };
                        PRIVILEGE_SET privileges = { 0 };
                        DWORD grantedAccess = 0, privilegesLength = sizeof(privileges);
                        BOOL result = FALSE;

                        mapping.GenericRead = FILE_GENERIC_READ;
                        mapping.GenericWrite = FILE_GENERIC_WRITE;
                        mapping.GenericExecute = FILE_GENERIC_EXECUTE;
                        mapping.GenericAll = FILE_ALL_ACCESS;

                        ::MapGenericMask(&genericAccessRights, &mapping);
                        if (::AccessCheck(security, hImpersonatedToken, genericAccessRights, &mapping, &privileges, &privilegesLength, &grantedAccess, &result))
                        {
                            bRet = (result != FALSE);
                        }
                        ::CloseHandle(hImpersonatedToken);
                    }
                    ::CloseHandle(hToken);
                }
            }
        }
        return bRet;
    }

    // Reads a string from the version resource with the proper API (the value can be in any language block)
    std::wstring ReadVersionString(const std::vector<uint8_t>& block, const wchar_t* key)
    {
        struct LANGANDCODEPAGE { WORD wLanguage; WORD wCodePage; };
        std::vector<std::wstring> subBlocks;

        LANGANDCODEPAGE* translations = nullptr;
        UINT size = 0;
        if (VerQueryValueW(block.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<LPVOID*>(&translations), &size) && translations)
        {
            for (UINT i = 0; i < size / sizeof(LANGANDCODEPAGE); i++)
                subBlocks.push_back(Format(L"\\StringFileInfo\\{:04x}{:04x}\\{}", translations[i].wLanguage, translations[i].wCodePage, key));
        }
        for (auto fallback : { L"040004b0", L"040904b0", L"040904e4", L"000004b0" })
            subBlocks.push_back(Format(L"\\StringFileInfo\\{}\\{}", fallback, key));

        for (auto& subBlock : subBlocks)
        {
            wchar_t* value = nullptr;
            UINT length = 0;
            if (VerQueryValueW(block.data(), subBlock.c_str(), reinterpret_cast<LPVOID*>(&value), &length) && value && length > 0)
                return std::wstring(value, wcsnlen(value, length));
        }
        return {};
    }

    std::pair<std::wstring, std::wstring> ReadUpdateUrls(const std::filesystem::path& file)
    {
        DWORD handle = 0;
        DWORD size = GetFileVersionInfoSizeW(file.c_str(), &handle);
        if (!size)
            return {};

        std::vector<uint8_t> block(size);
        if (!GetFileVersionInfoW(file.c_str(), 0, size, block.data()))
            return {};

        return { ReadVersionString(block, UPDATEURL), ReadVersionString(block, DEVUPDATEURL) };
    }

    std::wstring MachineName(WORD machine)
    {
        if (machine == IMAGE_FILE_MACHINE_I386)
            return L"x86";
        if (machine == IMAGE_FILE_MACHINE_AMD64)
            return L"x64";
        return L"";
    }

    // Worker -> UI thread
    class SharedStatus
    {
    public:
        void Set(const std::wstring& text, int percent)
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!text.empty())
                this->text = text;
            if (percent >= 0)
                this->percent = percent;
            changed = true;
        }

        bool Get(std::wstring& text, int& percent)
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!changed)
                return false;
            text = this->text;
            percent = this->percent;
            changed = false;
            return true;
        }

    private:
        std::mutex mutex;
        std::wstring text;
        int percent = 0;
        bool changed = false;
    };

    struct ListDialogContext
    {
        std::vector<FileUpdateInfo>* toUpdate;
        std::vector<FileUpdateInfo>* toDownload;
        std::wstring body;
    };

    struct ProgressDialogContext
    {
        SharedStatus status;
        std::atomic<bool> cancel = false;
        std::atomic<bool> finished = false;
        std::wstring shownText; // must stay valid while the dialog shows it
    };

    HRESULT CALLBACK ListDialogCallback(HWND hwnd, UINT uNotification, WPARAM, LPARAM lParam, LONG_PTR dwRefData)
    {
        auto ctx = reinterpret_cast<ListDialogContext*>(dwRefData);
        switch (uNotification)
        {
        case TDN_DIALOG_CONSTRUCTED:
            if (reqElev)
                SendMessage(hwnd, TDM_SET_BUTTON_ELEVATION_REQUIRED_STATE, BUTTONID1, TRUE);
            break;
        case TDN_HYPERLINK_CLICKED:
        {
            std::wstring link = reinterpret_cast<LPCWSTR>(lParam);
            if (starts_with(link, L"file:", true))
            {
                // "Don't update this time": drop the file from the lists and from the text
                auto p = link.substr(5);
                auto matches = [&p](const FileUpdateInfo& e) { return e.wszFullFilePath == p; };
                ctx->toUpdate->erase(std::remove_if(ctx->toUpdate->begin(), ctx->toUpdate->end(), matches), ctx->toUpdate->end());
                ctx->toDownload->erase(std::remove_if(ctx->toDownload->begin(), ctx->toDownload->end(), matches), ctx->toDownload->end());

                auto hrefPos = ctx->body.find(L"file:" + p + L"\"");
                if (hrefPos != std::wstring::npos)
                {
                    auto startPos = ctx->body.rfind(L'‌', hrefPos);
                    auto endPos = ctx->body.find(L'‌', hrefPos);
                    if (startPos != std::wstring::npos && endPos != std::wstring::npos)
                        ctx->body.erase(startPos, endPos - startPos + 1);
                }
                SendMessage(hwnd, TDM_SET_ELEMENT_TEXT, TDE_CONTENT, reinterpret_cast<LPARAM>(ctx->body.c_str()));

                if (ctx->toUpdate->empty() && ctx->toDownload->empty())
                    SendMessage(hwnd, TDM_CLICK_BUTTON, BUTTONID2, 0);
            }
            else
            {
                ui::OpenUrl(hwnd, link);
            }
            break;
        }
        default:
            break;
        }
        return S_OK;
    }

    HRESULT CALLBACK ProgressDialogCallback(HWND hwnd, UINT uNotification, WPARAM wParam, LPARAM lParam, LONG_PTR dwRefData)
    {
        auto ctx = reinterpret_cast<ProgressDialogContext*>(dwRefData);
        switch (uNotification)
        {
        case TDN_DIALOG_CONSTRUCTED:
            SendMessage(hwnd, TDM_SET_PROGRESS_BAR_RANGE, 0, MAKELPARAM(0, 100));
            SendMessage(hwnd, TDM_SET_PROGRESS_BAR_POS, 0, 0);
            break;
        case TDN_TIMER:
        {
            std::wstring text;
            int percent = 0;
            if (!ctx->cancel && ctx->status.Get(text, percent))
            {
                ctx->shownText = PadContent(text);
                SendMessage(hwnd, TDM_UPDATE_ELEMENT_TEXT, TDE_CONTENT, reinterpret_cast<LPARAM>(ctx->shownText.c_str()));
                SendMessage(hwnd, TDM_SET_PROGRESS_BAR_POS, percent, 0);
            }
            if (ctx->finished)
                SendMessage(hwnd, TDM_CLICK_BUTTON, IDOK, 0);
            break;
        }
        case TDN_BUTTON_CLICKED:
            if (wParam == IDOK || ctx->finished)
                return S_OK;
            // Cancel button, Esc or the close button: stop the worker and wait for it
            if (!ctx->cancel.exchange(true))
            {
                ctx->shownText = PadContent(L"Cancelling...");
                SendMessage(hwnd, TDM_UPDATE_ELEMENT_TEXT, TDE_CONTENT, reinterpret_cast<LPARAM>(ctx->shownText.c_str()));
                SendMessage(hwnd, TDM_ENABLE_BUTTON, BUTTONID3, FALSE);
            }
            return S_FALSE;
        case TDN_HYPERLINK_CLICKED:
            ui::OpenUrl(hwnd, reinterpret_cast<LPCWSTR>(lParam));
            break;
        default:
            break;
        }
        return S_OK;
    }

    HRESULT CALLBACK ResultDialogCallback(HWND hwnd, UINT uNotification, WPARAM, LPARAM lParam, LONG_PTR)
    {
        if (uNotification == TDN_HYPERLINK_CLICKED)
        {
            std::wstring link = reinterpret_cast<LPCWSTR>(lParam);
            if (link == L"log:")
                ShellExecuteW(hwnd, L"open", LogGetFile().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            else
                ui::OpenUrl(hwnd, link);
        }
        return S_OK;
    }

    void ReturnFocusToGame()
    {
        if (HWND game = ui::FindProcessMainWindow())
            SwitchToThisWindow(game, TRUE);
    }

    std::wstring DescribeUpdate(const FileUpdateInfo& it, bool installed)
    {
        std::wstring text = L"‌" + it.wszFileName + L" ";
        if (it.nFileSize > 0)
        {
            text += L"(<a href=\"" + it.wszDownloadURL + L"\">" + it.wszDownloadName + L"</a> / " + formatBytesW(it.nFileSize) + L")\n";
            if (it.nRemoteFileUpdatedHoursAgo != kRemoteDateUnknown)
                text += L"Remote file updated " + getTimeAgoW(it.nRemoteFileUpdatedHoursAgo) + L".\n";
            else
            {
                text += L"Release date unknown.\n";
                text += L"Warning: Files will be processed even if update is not required.\n";
            }
            if (installed)
                text += L"Local file updated " + getTimeAgoW(it.nLocaFileUpdatedHoursAgo) + L".\n";
        }
        else
        {
            text += L"(" + it.wszDownloadName + L" / Unknown size)\n";
            text += L"Release date unknown.\n";
            text += L"Warning: Files will not be processed if URL is not valid.\n";
            if (installed)
                text += L"Warning: Files will be updated even if update is not required.\n";
        }
        if (!installed)
            text += L"Local file was not present. This mod will be installed.\n";
        text += L"<a href=\"file:" + it.wszFullFilePath + L"\">Don't update this time</a>\n\n‌";
        return text;
    }
}

namespace mu::updater
{
    int32_t HoursSinceFileTime(const FILETIME& time)
    {
        ULARGE_INTEGER ull;
        ull.LowPart = time.dwLowDateTime;
        ull.HighPart = time.dwHighDateTime;
        if (ull.QuadPart < 116444736000000000ULL)
            return INT32_MAX;
        auto seconds = static_cast<int64_t>((ull.QuadPart - 116444736000000000ULL) / 10000000ULL);
        return HoursSince(std::chrono::system_clock::time_point(std::chrono::seconds(seconds)));
    }

    std::vector<FileUpdateInfo> CheckForUpdates(const std::vector<Candidate>& candidates, const std::string& token)
    {
        std::vector<FileUpdateInfo> updates;
        for (auto& candidate : candidates)
        {
            auto strFileName = candidate.path.filename().wstring();
            auto url = candidate.url;
            auto devurl = candidate.devUrl;
            if (url.empty() && !devurl.empty())
            {
                url = devurl;
                devurl.clear();
            }

            Log(L"{} found.", strFileName);
            Log(L"Update URL: {}", url);
            Log(L"Dev URL: {}", devurl);

            int32_t nLocaFileUpdatedHoursAgo = HoursSinceFileTime(candidate.lastWriteTime);
            auto remote = GetRemoteFileInfo(strFileName, toString(url), candidate.machine, token);

            if (!devurl.empty())
            {
                auto remoteDev = GetRemoteFileInfo(strFileName, toString(devurl), candidate.machine, token);
                bool devIsNewer = remoteDev.found() && (!remote.found() ||
                    (remoteDev.hoursAgo != kRemoteDateUnknown && (remote.hoursAgo == kRemoteDateUnknown || remoteDev.hoursAgo < remote.hoursAgo)));
                if (devIsNewer)
                    remote = remoteDev;
            }

            if (!remote.found())
            {
                Log(L"No updates available{}.", remote.error.empty() ? L"" : L" (" + remote.error + L")");
                continue;
            }

            if (remote.hoursAgo < nLocaFileUpdatedHoursAgo || candidate.alwaysUpdate)
            {
                Log(L"Download link: {}", toWString(remote.url));
                if (remote.hoursAgo != kRemoteDateUnknown)
                    Log(L"Remote file updated {}.", getTimeAgoW(remote.hoursAgo));
                else
                    Log(L"Last-Modified header was not specified. Update date unknown.");
                Log(L"Local file updated {}.", getTimeAgoW(nLocaFileUpdatedHoursAgo));
                Log(L"File size: {}.", formatBytesW(remote.size));

                FileUpdateInfo fui;
                fui.wszFullFilePath = candidate.path.wstring();
                fui.wszFileName = strFileName;
                fui.wszDownloadURL = toWString(remote.url);
                fui.wszDownloadName = toWString(remote.name);
                fui.szPassword = candidate.password;
                fui.nRemoteFileUpdatedHoursAgo = remote.hoursAgo;
                fui.nLocaFileUpdatedHoursAgo = nLocaFileUpdatedHoursAgo;
                fui.nFileSize = remote.size;
                updates.push_back(fui);
            }
            else
            {
                Log(L"No updates available.");
            }
        }
        return updates;
    }

    ApplyResult ApplyUpdate(const FileUpdateInfo& update, IniMode iniMode, const std::string& token, const std::function<bool(const Status&)>& status)
    {
        ApplyResult result;
        auto report = [&](const std::wstring& text, int percent) { return !status || status({ text, percent }); };

        std::filesystem::path fullFilePath = update.wszFullFilePath;
        auto directory = fullFilePath.parent_path();
        const bool singleFile = iequals(update.wszFileName, update.wszDownloadName);
        auto target = directory / (singleFile ? update.wszFileName : update.wszDownloadName);
        auto download = target;
        download += L".modupdater";

        Log(L"Downloading {}", update.wszDownloadName);
        report(L"Downloading " + update.wszDownloadName, 0);

        std::error_code ec;
        std::filesystem::create_directories(directory, ec);

        http::Options options;
        options.token = token;
        int lastPercent = -1;
        auto downloaded = http::DownloadToFile(toString(update.wszDownloadURL), download, [&](uint64_t done, uint64_t total)
        {
            uint64_t size = total ? total : update.nFileSize;
            int percent = size ? static_cast<int>(std::min<uint64_t>(100, done * 100 / size)) : 0;
            if (percent == lastPercent)
                return report({}, -1);
            lastPercent = percent;
            return report(Format(L"Downloading {}: {}% ", update.wszDownloadName, percent), percent);
        }, options);

        if (downloaded.cancelled)
        {
            result.cancelled = true;
            return result;
        }
        if (!downloaded.ok)
        {
            result.errors.push_back(Format(L"Cannot download {}: {}", update.wszDownloadName, downloaded.error));
            Log(L"{}", result.errors.back());
            return result;
        }
        Log(L"Download complete.");

        if (singleFile)
        {
            std::wstring error;
            switch (ReplaceFileWith(target, download, &error, true))
            {
            case ReplaceResult::ReplacedInUse:
                result.filesInUse++;
                [[fallthrough]];
            case ReplaceResult::Replaced:
                result.filesWritten++;
                Log(L"{} was updated successfully.", update.wszFileName);
                break;
            case ReplaceResult::Pending:
            {
                auto pending = target;
                pending += kPendingSuffix;
                result.pending.push_back({ update.wszFileName, pending });
                Log(L"{} is in use, it will be updated on the next launch.", update.wszFileName);
                break;
            }
            case ReplaceResult::Failed:
                DeleteFileW(download.c_str());
                result.errors.push_back(Format(L"{}: {}", update.wszFileName, error));
                break;
            }
            return result;
        }

        report(L"Processing " + update.wszDownloadName, 0);
        Log(L"Processing {}", update.wszDownloadName);

        zip::Reader reader;
        if (!reader.Open(download))
        {
            result.errors.push_back(reader.Error());
            DeleteFileW(download.c_str());
            return result;
        }

        // The entry of the file being updated tells where the root of the archive is on disk
        auto baseName = [](const std::wstring& name) { return name.substr(name.find_last_of(L"/\\") + 1); };
        const zip::Entry* anchor = nullptr;
        for (auto& e : reader.Entries())
        {
            if (!e.isDirectory && iequals(baseName(e.name), update.wszFileName))
            {
                anchor = &e;
                break;
            }
        }
        if (!anchor)
        {
            for (auto& e : reader.Entries())
            {
                if (!e.isDirectory && ends_with(e.name, L".asi", false))
                {
                    anchor = &e;
                    break;
                }
            }
        }

        if (!anchor)
        {
            result.errors.push_back(Format(L"{} does not contain {}, the archive was not processed.", update.wszDownloadName, update.wszFileName));
            Log(L"{}", result.errors.back());
            reader.Close();
            DeleteFileW(download.c_str());
            return result;
        }

        std::filesystem::path unpackDir = fullFilePath;
        std::wstring anchorName = anchor->name;
        std::replace(anchorName.begin(), anchorName.end(), L'\\', L'/');
        size_t start = 0;
        while (start <= anchorName.size())
        {
            auto end = anchorName.find(L'/', start);
            if (end == std::wstring::npos)
                end = anchorName.size();
            auto part = anchorName.substr(start, end - start);
            if (!part.empty() && part != L".")
                unpackDir = unpackDir.parent_path();
            start = end + 1;
        }

        if (unpackDir.empty() || unpackDir == unpackDir.root_path())
        {
            result.errors.push_back(Format(L"The folder layout of {} does not match the installed files.", update.wszDownloadName));
            reader.Close();
            DeleteFileW(download.c_str());
            return result;
        }

        Log(L"Extracting {} to {}", update.wszDownloadName, unpackDir.wstring());

        ExtractOptions extractOptions;
        extractOptions.iniMode = iniMode;
        extractOptions.password = update.szPassword;
        extractOptions.allowPending = true; // archives the game keeps open are replaced when it starts again
        ExtractReport extractReport;
        std::wstring lastFile;
        ExtractArchive(reader, unpackDir, extractOptions, [&](const ExtractProgress& p)
        {
            int percent = p.total ? static_cast<int>(p.done * 100 / p.total) : 100;
            if (p.currentFile != lastFile)
            {
                lastFile = p.currentFile;
                return report(p.currentFile.empty() ? L"Processing " + update.wszDownloadName : L"Updating " + p.currentFile, percent);
            }
            return report({}, percent);
        }, extractReport);

        reader.Close();
        DeleteFileW(download.c_str());

        result.cancelled = extractReport.cancelled;
        result.errors = extractReport.errors;
        result.filesWritten = extractReport.written;
        result.filesInUse = extractReport.inUse;
        result.pending = extractReport.pending;
        return result;
    }
}

void ShowUpdateDialog(std::vector<FileUpdateInfo>& FilesToUpdate, std::vector<FileUpdateInfo>& FilesToDownload)
{
    ui::ScopedDpiAwareness dpiAwareness;

    ListDialogContext list = { &FilesToUpdate, &FilesToDownload };
    uint64_t nTotalUpdateSize = 0;
    for (auto& it : FilesToDownload)
    {
        list.body += DescribeUpdate(it, false);
        nTotalUpdateSize += it.nFileSize;
    }
    for (auto& it : FilesToUpdate)
    {
        list.body += DescribeUpdate(it, true);
        nTotalUpdateSize += it.nFileSize;
    }
    list.body += L"Do you want to download these updates?";

    auto szButton1Text = std::wstring(L"Download and install updates now\n" + (nTotalUpdateSize > 0 ? formatBytesW(nTotalUpdateSize) : L""));
    if (reqElev)
        szButton1Text = L"Restart this application with elevated permissions\n"
                        L"If you grant permission by using the User Account Control\nfeature of your operating system, the application may be\nable to complete the requested tasks.";

    TASKDIALOG_BUTTON aCustomButtons[] = {
        { BUTTONID1, szButton1Text.c_str() },
        { BUTTONID2, L"Cancel" }
    };

    TASKDIALOG_BUTTON radioButtons[] = {
        { RBUTTONID1, L"INI files: replace all and keep settings" },
        { RBUTTONID2, L"INI files: replace all and discard settings" },
        { RBUTTONID3, L"INI files: don't replace" }
    };

    TASKDIALOGCONFIG tdc = { sizeof(TASKDIALOGCONFIG) };
    tdc.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_USE_COMMAND_LINKS | TDF_ENABLE_HYPERLINKS | TDF_SIZE_TO_CONTENT | TDF_CAN_BE_MINIMIZED;
    tdc.pButtons = aCustomButtons;
    tdc.cButtons = _countof(aCustomButtons);
    tdc.pRadioButtons = radioButtons;
    tdc.cRadioButtons = _countof(radioButtons);
    tdc.pszWindowTitle = L"modupdater";
    tdc.pszMainInstruction = L"Updates available:\n";
    tdc.pszContent = list.body.c_str();
    tdc.pfCallback = ListDialogCallback;
    tdc.lpCallbackData = reinterpret_cast<LONG_PTR>(&list);

    int nClickedBtnID = -1;
    int nRadioBtnID = -1;
    HRESULT hr = TaskDialogIndirect(&tdc, &nClickedBtnID, &nRadioBtnID, nullptr);

    if (FAILED(hr) || nClickedBtnID != BUTTONID1 || (FilesToUpdate.empty() && FilesToDownload.empty()))
    {
        Log(L"Update cancelled.");
        ReturnFocusToGame();
        return;
    }

    if (reqElev)
    {
        if (ui::RestartProcess(true, ui::GetProcessArguments()))
            ExitProcess(0);
        ReturnFocusToGame();
        return;
    }

    auto iniMode = (nRadioBtnID == RBUTTONID2) ? IniMode::Replace : (nRadioBtnID == RBUTTONID3) ? IniMode::Skip : IniMode::Merge;

    // Download and install
    ProgressDialogContext progress;
    progress.shownText = PadContent(L"Preparing to download...");

    TASKDIALOG_BUTTON aCustomButtons2[] = {
        { BUTTONID3, L"Cancel" }
    };

    TASKDIALOGCONFIG tdcProgress = { sizeof(TASKDIALOGCONFIG) };
    tdcProgress.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_ENABLE_HYPERLINKS | TDF_SIZE_TO_CONTENT | TDF_CAN_BE_MINIMIZED | TDF_SHOW_PROGRESS_BAR | TDF_CALLBACK_TIMER;
    tdcProgress.pButtons = aCustomButtons2;
    tdcProgress.cButtons = _countof(aCustomButtons2);
    tdcProgress.pszWindowTitle = L"modupdater";
    tdcProgress.pszMainInstruction = L"Downloading Update...";
    tdcProgress.pszContent = progress.shownText.c_str();
    tdcProgress.cxWidth = 200;
    tdcProgress.pfCallback = ProgressDialogCallback;
    tdcProgress.lpCallbackData = reinterpret_cast<LONG_PTR>(&progress);

    updater::ApplyResult total;
    std::thread worker([&]
    {
        try
        {
            auto statusCallback = [&](const updater::Status& s)
            {
                if (!s.text.empty() || s.percent >= 0)
                    progress.status.Set(s.text, s.percent);
                return !progress.cancel.load();
            };

            for (auto* list : { &FilesToDownload, &FilesToUpdate })
            {
                for (auto& it : *list)
                {
                    if (progress.cancel || total.cancelled)
                        break;
                    auto result = updater::ApplyUpdate(it, iniMode, token, statusCallback);
                    total.cancelled |= result.cancelled;
                    total.filesWritten += result.filesWritten;
                    total.filesInUse += result.filesInUse;
                    total.pending.insert(total.pending.end(), result.pending.begin(), result.pending.end());
                    total.errors.insert(total.errors.end(), result.errors.begin(), result.errors.end());
                }
            }
        }
        catch (const std::exception& e)
        {
            total.errors.push_back(L"Unexpected error: " + toWString(e.what()));
        }
        progress.finished = true;
    });

    hr = TaskDialogIndirect(&tdcProgress, &nClickedBtnID, nullptr, nullptr);
    if (FAILED(hr))
        progress.cancel = true;
    worker.join();

    // files the game keeps open are put in place when it starts again (also after a cancel, they are written already)
    if (!total.pending.empty())
    {
        std::vector<std::filesystem::path> files;
        for (auto& pending : total.pending)
            files.push_back(pending.file);
        auto list = modulePath / kPendingListName;
        if (!AddPendingFiles(list, files))
            total.errors.push_back(Format(L"Cannot write {}: {}", list.wstring(), Win32ErrorMessage(GetLastError())));
    }

    if (progress.cancel || total.cancelled)
    {
        Log(L"Update cancelled or error occured.");
        ReturnFocusToGame();
        return;
    }

    if (total.errors.empty() && muSkipUpdateCompleteDialog)
    {
        ReturnFocusToGame();
        return;
    }

    std::wstring content;
    std::wstring mainInstruction = L"Update completed successfully.";
    if (!total.pending.empty())
    {
        content += L"The game is using these files, they will be updated when it starts again:\n";
        for (size_t i = 0; i < total.pending.size() && i < 8; i++)
            content += total.pending[i].name + L"\n";
        if (total.pending.size() > 8)
            content += Format(L"...and {} more.\n", total.pending.size() - 8);
    }
    if (!total.errors.empty())
    {
        mainInstruction = total.filesWritten ? L"Update completed with errors." : L"Update failed.";
        if (!content.empty())
            content += L"\n";
        for (size_t i = 0; i < total.errors.size() && i < 8; i++)
            content += total.errors[i] + L"\n";
        if (total.errors.size() > 8)
            content += Format(L"...and {} more.\n", total.errors.size() - 8);
        if (!LogGetFile().empty())
            content += L"\n<a href=\"log:\">Open the log file</a>";
    }

    TASKDIALOG_BUTTON aCustomButtons3[] = {
        { BUTTONID4, L"Restart the game to apply changes" },
        { BUTTONID5, L"Continue" },
    };

    const bool changed = total.filesWritten || !total.pending.empty();
    TASKDIALOGCONFIG tdcResult = { sizeof(TASKDIALOGCONFIG) };
    tdcResult.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_USE_COMMAND_LINKS | TDF_ENABLE_HYPERLINKS | TDF_SIZE_TO_CONTENT | TDF_CAN_BE_MINIMIZED;
    tdcResult.pButtons = changed ? aCustomButtons3 : aCustomButtons3 + 1;
    tdcResult.cButtons = changed ? 2 : 1;
    tdcResult.pszWindowTitle = L"modupdater";
    tdcResult.pszMainInstruction = mainInstruction.c_str();
    tdcResult.pszContent = content.c_str();
    tdcResult.pszMainIcon = total.errors.empty() ? nullptr : TD_WARNING_ICON;
    tdcResult.pfCallback = ResultDialogCallback;

    hr = TaskDialogIndirect(&tdcResult, &nClickedBtnID, nullptr, nullptr);
    if (SUCCEEDED(hr) && nClickedBtnID == BUTTONID4)
    {
        // the new process puts the waiting files in place once this one has exited and closed them
        if (!total.pending.empty())
            SetEnvironmentVariableW(kWaitForProcessVariable, std::to_wstring(GetCurrentProcessId()).c_str());
        if (ui::RestartProcess(false, ui::GetProcessArguments()))
            ExitProcess(0);
        SetEnvironmentVariableW(kWaitForProcessVariable, nullptr);
    }
    ReturnFocusToGame();
}

#ifdef STATICLIB
void ProcessFiles(const RegistrySnapshot& snapshot)
{
    std::vector<updater::Candidate> candidates;
    bool skipCompleteDialog = true;

    for (auto& [module, settings] : snapshot)
    {
        auto url = GetString(settings, K(Key::UpdateUrl));
        auto devurl = GetString(settings, K(Key::DevUpdateUrl));
        if (url.empty() && devurl.empty())
            continue;

        auto filePath = GetModuleFilePath(module);
        WIN32_FILE_ATTRIBUTE_DATA fInfo = {};
        if (filePath.empty() || !GetFileAttributesExW(filePath.c_str(), GetFileExInfoStandard, &fInfo))
        {
            Log(L"Module {:p} is no longer loaded, skipping it.", static_cast<void*>(module));
            continue;
        }

        updater::Candidate candidate;
        candidate.path = filePath;
        candidate.url = toWString(url);
        candidate.devUrl = toWString(devurl);
        candidate.lastWriteTime = fInfo.ftLastWriteTime;
        candidate.machine = MachineName(GetModuleMachine(module));
        candidate.alwaysUpdate = GetInt(settings, K(Key::AlwaysUpdate)) != 0;
        candidate.password = GetString(settings, K(Key::ArchivePassword));
        candidates.push_back(candidate);

        skipCompleteDialog = skipCompleteDialog && GetInt(settings, K(Key::SkipUpdateCompleteDialog)) != 0;
    }

    muSkipUpdateCompleteDialog = !candidates.empty() && skipCompleteDialog;

    auto FilesToUpdate = updater::CheckForUpdates(candidates, token);
    std::vector<FileUpdateInfo> FilesToDownload;

    if (!FilesToUpdate.empty())
        ShowUpdateDialog(FilesToUpdate, FilesToDownload);
    else
        Log(L"No files found to process.");
}
#else
void ProcessFiles()
{
    std::vector<updater::Candidate> candidates;
    std::vector<std::wstring> FilesPresent;

    FindFilesRecursively(modulePath, [&](const std::filesystem::path& file, const WIN32_FIND_DATAW& fd)
    {
        auto strFileName = file.filename().wstring();
        if (ends_with(strFileName, kDeleteOnNextLaunchSuffix, false) || ends_with(strFileName, kTempFileSuffix, false) || ends_with(strFileName, kPendingSuffix, false))
            return;

        std::wstring machine;
        auto extension = toLowerWStr(file.extension().wstring());
        if (extension == L".dll" || extension == L".asi" || extension == L".exe")
            machine = MachineName(GetImageMachine(file));

        // Checking password
        std::string password = iniReader.ReadString(toString(strFileName), "Password", "");

        // Checking ini file for url
        auto iniEntry = iniReader.ReadString("MODS", toString(strFileName), "");
        removeQuotesFromString(iniEntry);
        if (!iniEntry.empty())
        {
            candidates.push_back({ file, toWString(iniEntry), L"", fd.ftLastWriteTime, machine, muAlwaysUpdate, password });
            FilesPresent.push_back(strFileName);
            return;
        }

        // Checking file info for url
        auto [updateUrl, devUpdateUrl] = ReadUpdateUrls(file);
        if (!updateUrl.empty() || !devUpdateUrl.empty())
        {
            candidates.push_back({ file, updateUrl, devUpdateUrl, fd.ftLastWriteTime, machine, muAlwaysUpdate, password });
            FilesPresent.push_back(strFileName);
        }
    });

    auto FilesToUpdate = updater::CheckForUpdates(candidates, token);
    std::vector<FileUpdateInfo> FilesToDownload;

    // mods listed in [MODS] that are not installed yet
    try
    {
        mINI::INIFile ini(iniReader.GetIniPath());
        mINI::INIStructure iniStruct;
        ini.read(iniStruct);

        if (iniStruct.has("MODS"))
        {
            for (auto const& it : iniStruct["MODS"])
            {
                auto strIni = std::get<0>(it);
                auto iniEntry = std::get<1>(it);
                removeQuotesFromString(iniEntry);

                if (strIni.empty() || iniEntry.empty() || strIni.at(0) == '.')
                    continue;

                bool present = std::any_of(FilesPresent.begin(), FilesPresent.end(), [&](const std::wstring& f) { return iequals(f, toWString(strIni)); });
                if (present)
                    continue;

                auto remote = GetRemoteFileInfo(toWString(strIni), iniEntry, L"", token);
                if (remote.found())
                {
                    Log(L"Download link: {}", toWString(remote.url));
                    Log(L"Local file is not present.");

                    FileUpdateInfo fui;
                    fui.wszFileName = toWString(strIni);
                    fui.wszFullFilePath = (selfPath / fui.wszFileName).wstring();
                    fui.wszDownloadURL = toWString(remote.url);
                    fui.wszDownloadName = toWString(remote.name);
                    fui.nRemoteFileUpdatedHoursAgo = remote.hoursAgo;
                    fui.nLocaFileUpdatedHoursAgo = INT_MAX;
                    fui.nFileSize = remote.size;
                    FilesToDownload.push_back(fui);
                }
                else
                {
                    Log(L"No updates available.");
                }
            }
        }
    }
    catch (const std::exception& e)
    {
        Log(L"Cannot read [MODS]: {}", toWString(e.what()));
    }

    if (!FilesToUpdate.empty() || !FilesToDownload.empty())
        ShowUpdateDialog(FilesToUpdate, FilesToDownload);
    else
        Log(L"No files found to process.");
}
#endif

void InitModupdater()
{
    selfPath = GetThisModulePath();
    selfName = GetThisModuleName();
    selfNameNoExt = selfName.stem();
    modulePath = GetExeModulePath();
    processPath = GetModulePath(GetModuleHandleW(NULL));

    if (!CanAccessFolder(modulePath, GENERIC_READ))
        return;

#ifndef STATICLIB
    // tell modules that use the library that this plugin takes care of the updates
    CreateMutexW(NULL, FALSE, PluginMarkerName().c_str());
    CreateMutexW(NULL, TRUE, mtxNameAsiLegacy);

    iniReader.SetIniPath(L"modupdater.ini");

    muAlwaysUpdate = iniReader.ReadInteger("DEBUG", "AlwaysUpdate", 0) != 0;
    muSkipUpdateCompleteDialog = iniReader.ReadInteger("MISC", "SkipUpdateCompleteDialog", 0) != 0;
    auto nUpdateFrequencyInHours = iniReader.ReadInteger("DATE", "UpdateFrequencyInHours", 6);
    auto szWhenLastUpdateAttemptWasInHours = iniReader.ReadString("DATE", "WhenLastUpdateAttemptWas", "");

    if (iniReader.ReadInteger("MISC", "OutputLogToFile", 1) != 0)
        LogSetFile(modulePath / L"modupdater.log");

#ifdef EXECUTABLE
    LogSetConsole(true);
    CleanupLeftovers(modulePath);
#else
    std::thread([]
    {
        CleanupLeftovers(modulePath);
    }).detach();
#endif
    LogStartupMessages();

    auto now = date::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    date::sys_seconds tp;
    auto nWhenLastUpdateAttemptWas = std::chrono::hours(INT_MAX);
    std::istringstream in{ szWhenLastUpdateAttemptWasInHours };
    in >> date::parse("%D %T %Z", tp);
    if (in && now >= tp)
        nWhenLastUpdateAttemptWas = std::chrono::duration_cast<std::chrono::hours>(now - tp);

#ifndef _DEBUG
    if (bool(in) && nWhenLastUpdateAttemptWas < std::chrono::hours(nUpdateFrequencyInHours) && !muAlwaysUpdate)
    {
        Log(L"Last update attempt was {} hours ago.", nWhenLastUpdateAttemptWas.count());
        Log(L"Modupdater is configured to update once every {} hours.", nUpdateFrequencyInHours);
    }
    else
#endif
    {
        Log(L"Current directory: {}", modulePath.wstring());

        token = iniReader.ReadString("DEBUG", "Token", "");

        if (!CanAccessFolder(modulePath, GENERIC_READ | GENERIC_WRITE))
            reqElev = true;
        else
            iniReader.WriteString("DATE", "WhenLastUpdateAttemptWas", date::format("%D %T %Z", now));

#ifdef EXECUTABLE
        ProcessFiles();

        std::cout << "Press Enter to exit...";
        std::cin.get();
#else
        std::thread([]
        {
            try
            {
                ProcessFiles();
            }
            catch (const std::exception& e)
            {
                Log(L"Unexpected error: {}", toWString(e.what()));
            }
        }).detach();
#endif
    }
#else
    auto snapshot = RegistryGetSnapshot();
    for (auto& [module, settings] : snapshot)
    {
        auto logFile = GetString(settings, K(Key::LogFile));
        if (!logFile.empty())
        {
            LogSetFile(toWString(logFile));
            break;
        }
    }
    LogStartupMessages();

    Log(L"Current directory: {}", modulePath.wstring());
    reqElev = !CanAccessFolder(modulePath, GENERIC_READ | GENERIC_WRITE);
    CleanupLeftovers(modulePath);
    ProcessFiles(snapshot);
#endif
}

// API
#ifdef STATICLIB
void muInit()
{
    // the first module that calls muInit checks the updates of all modules
    if (!RegistryClaim(kClaimUpdater))
        return;

    std::thread([]()
    {
        // the thread runs code of this module, keep it loaded
        HMODULE self = NULL;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, reinterpret_cast<LPCWSTR>(&muInit), &self);

        // give the other modules time to register
        std::this_thread::sleep_for(std::chrono::seconds(5));

        if (IsStandalonePluginLoaded())
        {
            Log(L"modupdater.asi is loaded, it will check the updates.");
            return;
        }

        try
        {
            InitModupdater();
        }
        catch (const std::exception& e)
        {
            Log(L"Unexpected error: {}", toWString(e.what()));
        }
    }).detach();
}
#endif

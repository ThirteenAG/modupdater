#include "stdafx.h"
#include "string_funcs.h"
#include "mINI\src\mini\ini.h"
#include "ModuleList.hpp"
#include "libmodupdater.h"

#include <shobjidl.h>
#include <combaseapi.h>
#include <optional>

constexpr auto maxContentLength = 220;
constexpr auto mtxName = L"MODUPDATER-0TAPXVW8TY18N5SEP5CW7I4UE1FKOJ";
constexpr auto mtxNameAsi = L"MODUPDATERASI-0TAPXVW8TY18N5SEP5CW7I4UE1FKOJ";
std::atomic<HANDLE> muMutexHandle = NULL;
std::mutex muMutex;

struct ModuleUpdateInfo
{
    std::string muUpdateURL = "";
    std::string muDevUpdateURL = "";
    std::string muArchivePassword = "";
    bool muSkipUpdateCompleteDialog = false;
    bool muAlwaysUpdate = false;

    HICON muInstallerIcon;
    std::string muInstallerWindowTitle;
    std::string muInstallerMainInstruction;
    std::string muInstallerContent;
    std::string muInstallerFooter;
    std::string muRglAppID;
    std::string muRglAppSubfolder;
    std::string muSteamAppID;
    std::string muSteamAppSubfolder;
}; std::map<HMODULE, ModuleUpdateInfo>* muInfoPtr = nullptr;

extern "C"
{
    __declspec(dllexport) void* __cdecl muGetInfo()
    {
        return (void*)muInfoPtr;
    }
}

std::map<HMODULE, ModuleUpdateInfo>* muGetInfoPtr()
{
    if (!muInfoPtr)
    {
        auto hMutex = CreateMutexW(NULL, TRUE, mtxName);

        if (GetLastError() != ERROR_ALREADY_EXISTS)
        {
            muInfoPtr = new std::map<HMODULE, ModuleUpdateInfo>();
            muMutexHandle = hMutex;
        }
        else
        {
            ModuleList dlls;
            dlls.Enumerate(ModuleList::SearchLocation::LocalOnly);
            for (auto& e : dlls.m_moduleList)
            {
                auto m = std::get<HMODULE>(e);
                auto muGetInfoPtr = (decltype(muGetInfo)*)GetProcAddress(m, "muGetInfo");
                if (muGetInfoPtr)
                {
                    auto ptr = (decltype(muInfoPtr))muGetInfoPtr();
                    if (ptr)
                    {
                        muInfoPtr = ptr;
                        break;
                    }
                }
            }
        }
    }
    return muInfoPtr;
}

struct FileUpdateInfo
{
    std::wstring wszFullFilePath;
    std::wstring wszFileName;
    std::wstring wszDownloadURL;
    std::wstring wszDownloadName;
    std::string  szPassword;
    int32_t nRemoteFileUpdatedHoursAgo;
    int32_t nLocaFileUpdatedHoursAgo;
    int32_t	nFileSize;
};

#ifndef STATICLIB
CIniReader iniReader;
bool muAlwaysUpdate = false;
#endif
bool muSkipUpdateCompleteDialog = false;
HWND MainHwnd, DialogHwnd;
std::filesystem::path modulePath, processPath;
std::filesystem::path selfPath, selfName, selfNameNoExt;
std::wstring messagesBuffer;
std::wofstream logFile;
std::wstreambuf* outbuf;
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

size_t GetContentTextLength(std::wstring_view s)
{
    size_t length = 0;
    size_t i = 0;
    while (i < s.length())
    {
        if (s[i] == L'<')
        {
            // Check for <a ...> or <a> tag start (case-insensitive for 'a')
            if (i + 1 < s.length() && (s[i + 1] == L'a' || s[i + 1] == L'A'))
            {
                size_t tagEndPos = s.find(L'>', i + 1);
                if (tagEndPos != std::wstring_view::npos)
                {
                    // Skip the entire <a ...> tag markup
                    i = tagEndPos + 1;
                    continue;
                }
            }
            // Check for </a> or </A> tag end (case-insensitive for 'a')
            else if (i + 3 < s.length() && s[i + 1] == L'/' &&
                (s[i + 2] == L'a' || s[i + 2] == L'A') && s[i + 3] == L'>')
            {
                // Skip the entire </a> tag markup
                i += 4;
                continue;
            }
            // If it's a '<' but not part of a recognized <a> tag, count it and proceed
        }
        length++; // Count this character as part of the content text
        i++;
    }
    return length;
}

void printToMessages(std::wstring_view str)
{
    size_t contentTextLength = GetContentTextLength(str);

    if (contentTextLength >= maxContentLength)
    {
        if (str.length() >= maxContentLength)
        {
            messagesBuffer.assign(str.data(), maxContentLength);
        }
        else
        {
            messagesBuffer.assign(str.data(), str.length());
        }
    }
    else
    {
        messagesBuffer.assign(str.data(), str.length());
        messagesBuffer.append(maxContentLength - contentTextLength, L' ');
    }
}

bool GetImageFileHeaders(std::wstring fileName, IMAGE_NT_HEADERS& headers)
{
    HANDLE fileHandle = CreateFileW(
        fileName.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        0
    );
    if (fileHandle == INVALID_HANDLE_VALUE)
        return false;

    HANDLE imageHandle = CreateFileMappingW(
        fileHandle,
        nullptr,
        PAGE_READONLY,
        0,
        0,
        nullptr
    );
    if (imageHandle == 0)
    {
        CloseHandle(fileHandle);
        return false;
    }

    void* imagePtr = MapViewOfFile(
        imageHandle,
        FILE_MAP_READ,
        0,
        0,
        0
    );
    if (imagePtr == nullptr)
    {
        CloseHandle(imageHandle);
        CloseHandle(fileHandle);
        return false;
    }

    PIMAGE_NT_HEADERS headersPtr = ImageNtHeader(imagePtr);
    if (headersPtr == nullptr)
    {
        UnmapViewOfFile(imagePtr);
        CloseHandle(imageHandle);
        CloseHandle(fileHandle);
        return false;
    }

    headers = *headersPtr;

    UnmapViewOfFile(imagePtr);
    CloseHandle(imageHandle);
    CloseHandle(fileHandle);

    return true;
}

bool CanAccessFolder(std::filesystem::path folderName, DWORD genericAccessRights)
{
    bool bRet = false;
    DWORD length = 0;
    if (!::GetFileSecurityW(folderName.wstring().c_str(), OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, NULL, NULL, &length) && ERROR_INSUFFICIENT_BUFFER == ::GetLastError())
    {
        PSECURITY_DESCRIPTOR security = static_cast<PSECURITY_DESCRIPTOR>(::malloc(length));
        if (security && ::GetFileSecurityW(folderName.wstring().c_str(), OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, security, length, &length))
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
            ::free(security);
        }
    }
    return bRet;
}

int moveFileToRecycleBin(std::wstring file)
{
    file = file + L'\0' + L'\0';
    SHFILEOPSTRUCTW fileOp = { 0 };
    ZeroMemory(&fileOp, sizeof(SHFILEOPSTRUCT));
    fileOp.hwnd = NULL;
    fileOp.wFunc = FO_DELETE;
    fileOp.pFrom = file.c_str();
    fileOp.pTo = NULL;
    fileOp.fFlags = FOF_ALLOWUNDO | FOF_NOERRORUI | FOF_NOCONFIRMATION | FOF_SILENT;
    return SHFileOperationW(&fileOp);
}

BOOL CheckForFileLock(LPCWSTR pFilePath, bool bReleaseLock = false)
{
    BOOL bResult = FALSE;

    DWORD dwSession;
    WCHAR szSessionKey[CCH_RM_SESSION_KEY + 1] = { 0 };
    DWORD dwError = RmStartSession(&dwSession, 0, szSessionKey);
    if (dwError == ERROR_SUCCESS)
    {
        dwError = RmRegisterResources(dwSession, 1, &pFilePath, 0, NULL, 0, NULL);
        if (dwError == ERROR_SUCCESS)
        {
            UINT nProcInfoNeeded = 0;
            UINT nProcInfo = 0;
            RM_PROCESS_INFO rgpi[1];
            DWORD dwReason;

            dwError = RmGetList(dwSession, &nProcInfoNeeded, &nProcInfo, rgpi, &dwReason);
            if (dwError == ERROR_SUCCESS || dwError == ERROR_MORE_DATA)
            {
                if (nProcInfoNeeded > 0)
                {
                    //If current process does not have enough privileges to close one of
                    //the "offending" processes, you'll get ERROR_FAIL_NOACTION_REBOOT
                    if (bReleaseLock)
                    {
                        dwError = RmShutdown(dwSession, RmForceShutdown, NULL);
                        if (dwError == ERROR_SUCCESS)
                        {
                            bResult = TRUE;
                        }
                    }
                }
                else
                    bResult = TRUE;
            }
        }
    }

    RmEndSession(dwSession);

    SetLastError(dwError);
    return bResult;
}

void FindFilesRecursively(const std::wstring& directory, std::function<void(std::wstring&, WIN32_FIND_DATAW)> callback, bool cancelRecursion = false)
{
    std::wstring tmp = directory + L"\\*";
    WIN32_FIND_DATAW file;
    HANDLE search_handle = FindFirstFileW(tmp.c_str(), &file);
    if (search_handle != INVALID_HANDLE_VALUE)
    {
        std::vector<std::wstring> directories;

        do
        {
            if (file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                if ((!lstrcmpW(file.cFileName, L".")) || (!lstrcmpW(file.cFileName, L"..")))
                    continue;
            }

            tmp = directory + L"\\" + std::wstring(file.cFileName);
            if (!(file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            {
                callback(tmp, file);
            }

            if (file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                directories.push_back(tmp);
        } while (FindNextFileW(search_handle, &file));

        FindClose(search_handle);

        if (!cancelRecursion)
        {
            for (std::vector<std::wstring>::iterator iter = directories.begin(), end = directories.end(); iter != end; ++iter)
                FindFilesRecursively(*iter, callback);
        }
    }
}

void CleanupLockedFiles()
{
    auto cb = [](std::wstring& s, WIN32_FIND_DATAW)
    {
        static std::wstring const targetExtension(L".deleteonnextlaunch");
        if (s.size() >= targetExtension.size() && std::equal(s.end() - targetExtension.size(), s.end(), targetExtension.begin()))
        {
            moveFileToRecycleBin(s.c_str());
            std::wcout << L"Deleted " << s << std::endl;
        }
    };

    FindFilesRecursively(modulePath, cb);
}

void UpdateFile(std::vector<std::pair<std::wstring, std::string>>& downloads, std::wstring wszFileName, std::wstring wszFullFilePath, std::wstring wszDownloadURL, std::wstring wszDownloadName, std::string szPassword, bool bCheckboxChecked, int32_t nRadioBtnID)
{
    printToMessages(L"Downloading " + wszDownloadName);
    std::wcout << messagesBuffer << std::endl;

    cpr::Response r;

    auto itr = std::find_if(downloads.begin(), downloads.end(), [&wszDownloadURL](std::pair<std::wstring, std::string> const& wstr)
    {
        return wstr.first == wszDownloadURL;
    });

    static int lastProgress = 0;
    auto progressCallback = [&wszDownloadName](cpr::cpr_pf_arg_t downloadTotal, cpr::cpr_pf_arg_t downloadNow, cpr::cpr_pf_arg_t uploadTotal, cpr::cpr_pf_arg_t uploadNow, intptr_t userdata) -> bool
    {
        if (downloadTotal > 0)
        {
            int progress = static_cast<int>((downloadNow * 100) / downloadTotal);
            if (progress != lastProgress)
            {
                lastProgress = progress;
                SendMessage(DialogHwnd, TDM_SET_PROGRESS_BAR_POS, progress, 0);
                std::wostringstream oss;
                oss << L"Downloading " << wszDownloadName << L": " << progress << L"% ";
                printToMessages(oss.str());
            }
        }
        return true;
    };

    if (token.empty())
    {
        r = cpr::Get(cpr::Url{ toString(wszDownloadURL) },
            cpr::ProgressCallback{ progressCallback });
    }
    else
    {
        r = cpr::Get(cpr::Url{ toString(wszDownloadURL) },
            cpr::Header{ {"Authorization", "Bearer " + token} },
            cpr::ProgressCallback{ progressCallback });
        downloads.push_back(std::make_pair(wszDownloadURL, r.text));
    }
    lastProgress = 0;

    if (r.status_code == 200)
    {
        std::vector<uint8_t> buffer(r.text.begin(), r.text.end());
        printToMessages(L"Download complete.");
        std::wcout << messagesBuffer << std::endl;

        printToMessages(L"Processing " + wszDownloadName);
        std::wcout << messagesBuffer << std::endl;

        std::wstring fullPath = wszFullFilePath.substr(0, wszFullFilePath.find_last_of('\\') + 1) + ((wszFileName == wszDownloadName) ? wszFileName : wszDownloadName);

        if (CheckForFileLock(fullPath.c_str()) == FALSE)
        {
            std::wcout << wszDownloadName << L" is locked. Renaming..." << std::endl;
            if (MoveFileW(fullPath.c_str(), std::wstring(fullPath + L".deleteonnextlaunch").c_str()))
                std::wcout << wszDownloadName << L" was renamed to " << wszFileName + L".deleteonnextlaunch" << std::endl;
            else
                std::wcout << L"Error: " << GetLastError() << std::endl;
        }
        else
        {
            std::wcout << wszDownloadName << L" is not locked." << std::endl;
        }

        std::filesystem::create_directories(fullPath.substr(0, fullPath.find_last_of('\\')));

        auto muArchive = fullPath;
        if (wszFileName == wszDownloadName)
        {
            std::ofstream outputFile(muArchive, std::ios::binary);
            outputFile.write((const char*)&buffer[0], buffer.size());
            outputFile.close();
            printToMessages(wszFileName + L" was updated succesfully.");
            std::wcout << messagesBuffer << std::endl;
            return;
        }
        else
        {
            muArchive = fullPath + L".modupdater";
            std::ofstream outputFile(muArchive, std::ios::binary);
            outputFile.write((const char*)&buffer[0], buffer.size());
            outputFile.close();
        }

        using namespace zipper;
        std::string passw = szPassword;
        Unzipper unzipper(toString(muArchive), passw);
        std::vector<ZipEntry> entries = unzipper.entries();

        auto itr = std::find_if(entries.begin(), entries.end(), [&wszFileName](auto& s)
        {
            auto pos = s.name.rfind('/');
            auto s2 = s.name;
            if (pos != std::string::npos)
                s2 = s.name.substr(pos + 1);

            if (s2.size() != wszFileName.size())
                return false;

            for (size_t i = 0; i < s2.size(); ++i)
            {
                if (!(::towlower(s2[i]) == ::towlower(wszFileName[i])))
                {
                    return false;
                }
            }
            return true;
        });

        if (itr == entries.end())
        {
            itr = std::find_if(entries.begin(), entries.end(), [](auto& s)
            {
                return s.name.ends_with(".asi");
            });
        }

        if (itr != entries.end())
        {
            printToMessages(L"Processing " + wszDownloadName);
            std::wcout << messagesBuffer << std::endl;

            std::filesystem::path unpackDir = {};

            try
            {
                unpackDir = wszFullFilePath;
                std::filesystem::path zipUpdatePath = itr->name;
                for (auto iter = zipUpdatePath.begin(); iter != zipUpdatePath.end(); ++iter)
                    unpackDir = unpackDir.parent_path();

                std::vector<uint8_t> test;
                auto e = std::find_if(entries.begin(), entries.end(), [](auto& i) { return i.compressedSize != 0 && i.uncompressedSize != 0; });
                if (e != entries.end())
                    unzipper.extractEntryToMemory(e->name, test);
                test.clear();
            }
            catch (const std::exception& e)
            {
                std::wcout << e.what() << std::endl << std::endl;
                std::wcout << L"This archive will not be processed." << std::endl << std::endl;
                unzipper.close();
                moveFileToRecycleBin(muArchive);
                return;
            }

            for (auto it = entries.begin(); it != entries.end(); it++)
            {
                auto itFileName = std::filesystem::path(it->name).make_preferred();
                auto unpackPath = (unpackDir / itFileName).make_preferred();

                if (!unpackPath.wstring().ends_with(unpackPath.preferred_separator))
                {
                    if (CheckForFileLock(unpackPath.c_str()) == FALSE)
                    {
                        std::wcout << itFileName.wstring() << L" is locked. Renaming..." << std::endl;
                        moveFileToRecycleBin(std::wstring(unpackPath.wstring() + L".deleteonnextlaunch").c_str());
                        if (MoveFileW(unpackPath.c_str(), std::wstring(unpackPath.wstring() + L".deleteonnextlaunch").c_str()))
                            std::wcout << itFileName.wstring() << L" was renamed to " << unpackPath.filename().wstring() + L".deleteonnextlaunch" << std::endl;
                        else
                            std::wcout << L"Error: " << GetLastError() << std::endl;
                    }
                    else
                    {
                        std::wcout << itFileName.wstring() << L" is not locked." << std::endl;
                    }

                    std::filesystem::create_directories(std::filesystem::path(unpackPath).remove_filename());

                    if (unpackPath.extension() == L".ini")
                    {
                        if (nRadioBtnID == RBUTTONID3) //don't replace
                        {
                            continue;
                        }
                        else
                        {
                            if (nRadioBtnID == RBUTTONID2) //replace all
                            {
                                std::vector<uint8_t> vec;
                                unzipper.extractEntryToMemory(it->name, vec);
                                if (!vec.empty())
                                {
                                    moveFileToRecycleBin(unpackPath.wstring());
                                    std::ofstream iniFile(unpackPath, std::ios::binary);
                                    unzipper.extractEntryToStream(it->name, iniFile);
                                    iniFile.close();
                                    printToMessages(itFileName.wstring() + L" was updated succesfully.");
                                    std::wcout << messagesBuffer << std::endl;
                                    vec.clear();
                                }
                                continue;
                            }
                            else //merge
                            {
                                mINI::INIFile iniOld(unpackPath);
                                mINI::INIStructure iniOldStruct;
                                iniOld.read(iniOldStruct);

                                if (iniOldStruct.size())
                                {
                                    moveFileToRecycleBin(unpackPath.wstring());
                                    std::ofstream iniFile(unpackPath, std::ios::binary);
                                    unzipper.extractEntryToStream(it->name, iniFile);
                                    iniFile.close();

                                    mINI::INIFile iniNew(unpackPath);
                                    mINI::INIStructure iniNewStruct;
                                    iniNew.read(iniNewStruct);

                                    for (auto const& it : iniOldStruct)
                                    {
                                        auto const& section = std::get<0>(it);
                                        auto const& collection = std::get<1>(it);
                                        for (auto const& it2 : collection)
                                        {
                                            auto const& key = std::get<0>(it2);
                                            if (iniOldStruct.has(section))
                                                if (iniOldStruct[section].has(key))
                                                    iniNewStruct[section][key] = iniOldStruct[section][key];
                                        }
                                    }
                                    iniNew.write(iniNewStruct, true);

                                    printToMessages(itFileName.wstring() + L" was updated succesfully.");
                                    std::wcout << messagesBuffer << std::endl;
                                    continue;
                                }
                            }
                        }
                    }

                    moveFileToRecycleBin(unpackPath.c_str());
                    std::ofstream outputFile(unpackPath, std::ios::binary);
                    unzipper.extractEntryToStream(it->name, outputFile);
                    outputFile.close();

                    printToMessages(itFileName.wstring() + L" was updated succesfully.");
                    std::wcout << messagesBuffer << std::endl;
                }
            }
        }
        unzipper.close();
        moveFileToRecycleBin(muArchive);
    }
}

void ShowUpdateDialog(std::vector<FileUpdateInfo>& FilesToUpdate, std::vector<FileUpdateInfo>& FilesToDownload)
{
    static TASKDIALOGCONFIG tdc = { sizeof(TASKDIALOGCONFIG) };
    static std::wstring szBodyText;
    static auto& upd = FilesToUpdate;
    static auto& dl = FilesToDownload;

    auto TaskDialogCallbackProc = [](HWND hwnd, UINT uNotification, WPARAM wParam, LPARAM lParam, LONG_PTR dwRefData)->HRESULT
    {
        switch (uNotification)
        {
        case TDN_DESTROYED:
            break;
        case TDN_DIALOG_CONSTRUCTED:
            DialogHwnd = hwnd;
            if (dwRefData == 1)
            {
                SendMessage(hwnd, TDM_SET_MARQUEE_PROGRESS_BAR, FALSE, 0);
                SendMessage(hwnd, TDM_SET_PROGRESS_BAR_RANGE, 0, MAKELPARAM(0, 100));
                SendMessage(hwnd, TDM_SET_PROGRESS_BAR_POS, 0, 0);
            }
            else
            {
                SendMessage(hwnd, TDM_SET_MARQUEE_PROGRESS_BAR, FALSE, 0);
                SendMessage(hwnd, TDM_SET_PROGRESS_BAR_RANGE, 0, MAKELPARAM(0, 100));
                SendMessage(DialogHwnd, TDM_SET_PROGRESS_BAR_POS, 100, 0);

                if (dwRefData == 2)
                {
                    if (muSkipUpdateCompleteDialog)
                        SendMessage(DialogHwnd, TDM_CLICK_BUTTON, static_cast<WPARAM>(TDCBF_OK_BUTTON), 0);
                }
            }
            if (reqElev)
                SendMessage(DialogHwnd, TDM_SET_BUTTON_ELEVATION_REQUIRED_STATE, BUTTONID1, true);
            break;
        case TDN_BUTTON_CLICKED:
            break;
        case TDN_TIMER:
            if (dwRefData == 1)
            {
                SendMessage(DialogHwnd, TDM_UPDATE_ELEMENT_TEXT, TDE_CONTENT, (LPARAM)messagesBuffer.c_str());
            }
            break;
        case TDN_HYPERLINK_CLICKED:
        {
            auto p = std::wstring((LPCWSTR)lParam);
            if (starts_with(toString(p).c_str(), "file:", true))
            {
                p.erase(0, p.find(L':') + 1);
                upd.erase(std::remove_if(upd.begin(), upd.end(), [&p](const FileUpdateInfo& e) { return e.wszFullFilePath == p; }), upd.end());
                dl.erase(std::remove_if(dl.begin(), dl.end(), [&p](const FileUpdateInfo& e) { return e.wszFullFilePath == p; }), dl.end());
                auto substrPos = szBodyText.find(p);
                auto startPos = szBodyText.rfind(L"\u200C", substrPos);
                auto endPos = szBodyText.find(L"\u200C", substrPos) + 1;
                szBodyText.erase(startPos, endPos - startPos);
                SendMessage(DialogHwnd, TDM_SET_ELEMENT_TEXT, TDE_CONTENT, reinterpret_cast<LPARAM>(tdc.pszContent));

                if (upd.size() + dl.size() == 0)
                {
                    SendMessage(DialogHwnd, TDM_CLICK_BUTTON, static_cast<WPARAM>(TDCBF_CLOSE_BUTTON), 0);
                }
            }
            else
            {
                ShellExecuteW(hwnd, L"open", (LPCWSTR)lParam, NULL, NULL, SW_SHOW);
            }
            break;
        }
        default:
            break;
        }
        return S_OK;
    };

    auto EnumWindowsProc = [](HWND hwnd, LPARAM lParam)->BOOL
    {
        DWORD lpdwProcessId;
        GetWindowThreadProcessId(hwnd, &lpdwProcessId);
        if (lpdwProcessId == lParam)
        {
            MainHwnd = hwnd;
            return FALSE;
        }
        return TRUE;
    };

    auto szTitle = L"modupdater";
    auto szHeader = std::wstring(L"Updates available:\n");
    int32_t nTotalUpdateSize = 0;

    if (!FilesToDownload.empty())
    {
        for (auto& it : FilesToDownload)
        {
            szBodyText += L"\u200C";
            szBodyText += it.wszFileName + L" ";
            if (it.nFileSize > 0)
            {
                szBodyText += L"(" L"<a href=\"" + it.wszDownloadURL + L"\">" + it.wszDownloadName + L"</a>" + L" / " + formatBytesW(it.nFileSize) + L")" L"\n";
                if (it.nRemoteFileUpdatedHoursAgo != -2)
                    szBodyText += L"Remote file updated " + getTimeAgoW(it.nRemoteFileUpdatedHoursAgo) + L".\n";
                else
                {
                    szBodyText += L"Release date unknown." L"\n";
                    szBodyText += L"Warning: Files will processed even if update is not required." L"\n";
                }
            }
            else
            {
                szBodyText += L"(" + it.wszDownloadName + L" / Unknown size" + L")" L"\n";
                szBodyText += L"Release date unknown." L"\n";
                szBodyText += L"Warning: Files will not be processed if URL is not valid." L"\n";
            }
            szBodyText += L"Local file was not present. This mod will be installed." L"\n";
            szBodyText += L"<a href=\"file:" + it.wszFullFilePath + L"\">" + L"Don't update this time" + L"</a>";
            szBodyText += L"\n\n";
            szBodyText += L"\u200C";
            nTotalUpdateSize += it.nFileSize;
        }
    }

    if (!FilesToUpdate.empty())
    {
        for (auto& it : FilesToUpdate)
        {
            szBodyText += L"\u200C";
            szBodyText += it.wszFileName + L" ";
            if (it.nFileSize > 0)
            {
                szBodyText += L"(" L"<a href=\"" + it.wszDownloadURL + L"\">" + it.wszDownloadName + L"</a>" + L" / " + formatBytesW(it.nFileSize) + L")" L"\n";
                if (it.nRemoteFileUpdatedHoursAgo != -2)
                    szBodyText += L"Remote file updated " + getTimeAgoW(it.nRemoteFileUpdatedHoursAgo) + L".\n";
                else
                {
                    szBodyText += L"Release date unknown." L"\n";
                    szBodyText += L"Warning: Files will processed even if update is not required." L"\n";
                }
                szBodyText += L"Local file updated " + getTimeAgoW(it.nLocaFileUpdatedHoursAgo) + L".\n";
            }
            else
            {
                szBodyText += L"(" + it.wszDownloadName + L" / Unknown size" + L")" L"\n";
                szBodyText += L"Release date unknown." L"\n";
                szBodyText += L"Warning: Files will not be processed if URL is not valid." L"\n";
                szBodyText += L"Warning: Files will updated even if update is not required." L"\n";
            }
            szBodyText += L"<a href=\"file:" + it.wszFullFilePath + L"\">" + L"Don't update this time" + L"</a>";
            szBodyText += L"\n\n";
            szBodyText += L"\u200C";
            nTotalUpdateSize += it.nFileSize;
        }
    }

    szBodyText += L"Do you want to download these updates?";

    auto szButton1Text = std::wstring(L"Download and install updates now\n" + (nTotalUpdateSize > 0 ? formatBytesW(nTotalUpdateSize) : L""));
    if (reqElev)
        szButton1Text = std::wstring(L"Restart this application with elevated permissions\n"
        "If you grant permission by using the User Account Control\nfeature of your operating system, the application may be\nable to complete the requested tasks.");

    TASKDIALOG_BUTTON aCustomButtons[] = {
        { BUTTONID1, szButton1Text.c_str() },
        { BUTTONID2, L"Cancel" }
    };

    TASKDIALOG_BUTTON radioButtons[] =
    {
        { RBUTTONID1, L"INI files: replace all and keep settings" },
        { RBUTTONID2, L"INI files: replace all and discard settings" },
        { RBUTTONID3, L"INI files: don't replace" }
    };

    tdc.hwndParent = NULL;
    tdc.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_USE_COMMAND_LINKS | TDF_ENABLE_HYPERLINKS | TDF_SIZE_TO_CONTENT | TDF_CAN_BE_MINIMIZED;
    tdc.pButtons = aCustomButtons;
    tdc.cButtons = _countof(aCustomButtons);
    tdc.pRadioButtons = radioButtons;
    tdc.cRadioButtons = _countof(radioButtons);
    //tdc.pszMainIcon = TD_INFORMATION_ICON;
    tdc.pszWindowTitle = szTitle;
    tdc.pszMainInstruction = szHeader.c_str();
    tdc.pszContent = szBodyText.c_str();
    tdc.pszVerificationText = L""; //szCheckboxText;
    tdc.pszFooter = L""; // wszFooter.c_str();
    tdc.pszFooterIcon = TD_INFORMATION_ICON;
    tdc.pfCallback = TaskDialogCallbackProc;
    tdc.lpCallbackData = 3;
    auto nClickedBtnID = -1;
    auto nRadioBtnID = -1;
    auto bCheckboxChecked = 0;
    auto hr = TaskDialogIndirect(&tdc, &nClickedBtnID, &nRadioBtnID, &bCheckboxChecked);

    if (SUCCEEDED(hr) && nClickedBtnID == BUTTONID1)
    {
        if (reqElev)
        {
            auto workingDir = std::filesystem::path(processPath).remove_filename();
            SHELLEXECUTEINFOW ShExecInfo = { 0 };
            ShExecInfo.cbSize = sizeof(SHELLEXECUTEINFOW);
            ShExecInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
            ShExecInfo.hwnd = NULL;
            ShExecInfo.lpVerb = L"runas";
            ShExecInfo.lpFile = processPath.c_str();
            ShExecInfo.lpParameters = L"";
            ShExecInfo.lpDirectory = workingDir.c_str();
            ShExecInfo.nShow = SW_SHOWNORMAL;
            ShExecInfo.hInstApp = NULL;
            ShellExecuteExW(&ShExecInfo);
            ExitProcess(0);
        }

        TASKDIALOG_BUTTON aCustomButtons2[] = {
            { BUTTONID3, L"Cancel" }
        };

        static std::wstring content;
        content = L"Preparing to download...";
        std::wstring indent(maxContentLength - content.length(), L' ');
        content += indent;

        tdc.pButtons = aCustomButtons2;
        tdc.cButtons = _countof(aCustomButtons2);
        //tdc.pszMainIcon = TD_INFORMATION_ICON;
        tdc.dwFlags |= TDF_SHOW_MARQUEE_PROGRESS_BAR;
        tdc.dwFlags |= TDF_CALLBACK_TIMER;
        tdc.pszMainInstruction = L"Downloading Update...";
        tdc.pszContent = content.data();
        tdc.pszVerificationText = L"";
        tdc.pRadioButtons = NULL;
        tdc.cRadioButtons = NULL;
        tdc.lpCallbackData = 1;
        tdc.cxWidth = 200;

        std::atomic_bool bCanceledorError = false;
        std::thread t([&FilesToUpdate, &FilesToDownload, &bCheckboxChecked, &bCanceledorError, &nRadioBtnID, &nClickedBtnID]
        {
            std::vector<std::pair<std::wstring, std::string>> downloads;

            for (auto& it : FilesToDownload)
            {
                if (!bCanceledorError)
                    UpdateFile(downloads, it.wszFileName, it.wszFullFilePath, it.wszDownloadURL, it.wszDownloadName, it.szPassword, true, nRadioBtnID);
                else
                    break;
            }

            for (auto& it : FilesToUpdate)
            {
                if (!bCanceledorError)
                    UpdateFile(downloads, it.wszFileName, it.wszFullFilePath, it.wszDownloadURL, it.wszDownloadName, it.szPassword, true, nRadioBtnID);
                else
                    break;
            }

            while (nClickedBtnID != IDOK)
            {
                if (bCanceledorError)
                    break;
                SendMessage(DialogHwnd, TDM_CLICK_BUTTON, static_cast<WPARAM>(TDCBF_OK_BUTTON), 0);
                std::this_thread::yield();
            }
            downloads.clear();
        });

        hr = TaskDialogIndirect(&tdc, &nClickedBtnID, nullptr, nullptr);

        if (SUCCEEDED(hr))
        {
            if (nClickedBtnID != BUTTONID3 && nClickedBtnID != IDCANCEL)
            {
                bCanceledorError = false;

                TASKDIALOG_BUTTON aCustomButtons3[] = {
                    { BUTTONID4, L"Restart the game to apply changes" },
                    { BUTTONID5, L"Continue" },
                };

                tdc.pButtons = aCustomButtons3;
                tdc.cButtons = _countof(aCustomButtons3);
                tdc.pszMainInstruction = L"Update completed succesfully.";
                tdc.pszContent = L"";
                tdc.lpCallbackData = 2;
                tdc.cxWidth = 0;
                tdc.dwFlags |= TDF_CALLBACK_TIMER;

                hr = TaskDialogIndirect(&tdc, &nClickedBtnID, nullptr, nullptr);

                if (nClickedBtnID == BUTTONID4)
                {
                    auto workingDir = std::filesystem::path(processPath).remove_filename();
                    SHELLEXECUTEINFOW ShExecInfo = { 0 };
                    ShExecInfo.cbSize = sizeof(SHELLEXECUTEINFOW);
                    ShExecInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
                    ShExecInfo.hwnd = NULL;
                    ShExecInfo.lpVerb = NULL;
                    ShExecInfo.lpFile = processPath.c_str();
                    ShExecInfo.lpParameters = L"";
                    ShExecInfo.lpDirectory = workingDir.c_str();
                    ShExecInfo.nShow = SW_SHOWNORMAL;
                    ShExecInfo.hInstApp = NULL;
                    ShellExecuteExW(&ShExecInfo);
                    //WaitForSingleObject(ShExecInfo.hProcess, INFINITE);
                    ExitProcess(0);
                }
                else
                {
                    if (nClickedBtnID == BUTTONID5)
                    {
                        if (MainHwnd == NULL)
                            EnumWindows(EnumWindowsProc, GetCurrentProcessId());

                        SwitchToThisWindow(MainHwnd, TRUE);
                    }
                }
            }
            else
            {
                bCanceledorError = true;

                std::wcout << L"Update cancelled or error occured." << std::endl;
                if (MainHwnd == NULL)
                    EnumWindows(EnumWindowsProc, GetCurrentProcessId());

                SwitchToThisWindow(MainHwnd, TRUE);
            }
        }
        else
            bCanceledorError = true;

        t.join();
    }
    else
    {
        std::wcout << L"Update cancelled." << std::endl;
        if (MainHwnd == NULL)
            EnumWindows(EnumWindowsProc, GetCurrentProcessId());

        SwitchToThisWindow(MainHwnd, TRUE);
    }
}

std::tuple<int32_t, std::string, std::string, std::string> GetRemoteFileInfo(std::wstring strFileName, std::wstring strUrl, std::wstring machine)
{
    auto pos = strFileName.find_last_of('.');
    std::wstring strExtension;
    if (pos != std::wstring::npos)
    {
        strExtension = strFileName.substr(pos);
        strFileName.erase(pos);
    }
    else
        strFileName.append(L".asi");

    auto szUrl = toString(strUrl);

    cpr::Response rTest = {};
    if (token.empty())
        rTest = cpr::Head(cpr::Url{ szUrl });
    else
        rTest = cpr::Head(cpr::Url{ szUrl }, cpr::Header{ {"Authorization", "Bearer " + token} });

    if (szUrl.find("github.com") != std::string::npos && (starts_with(rTest.header["Content-Type"].c_str(), "text", false) || starts_with(rTest.header["Content-Type"].c_str(), "application/json", false)))
    {
        static std::string github = "github.com";
        static std::string repos = "/repos/";
        auto str = szUrl;

        str.erase(0, str.find(github) + (github.length() + 1));
        if (szUrl.find(repos) != std::string::npos)
            str.erase(0, str.find(repos) + (repos.length()));

        auto user = str.substr(0, str.find_first_of('/'));
        auto repo = str.substr(user.length() + 1);
        if (repo.find_first_of('/') != std::string::npos)
            repo.erase(repo.find_first_of('/'));

        szUrl = "https://api.github.com" + repos + user + "/" + repo + "/releases" + "?per_page=100";

        std::wcout << L"Connecting to GitHub: " << toWString(szUrl) << std::endl;

        cpr::Response rLink = {};
        if (token.empty())
            rLink = cpr::Head(cpr::Url{ szUrl });
        else
            rLink = cpr::Head(cpr::Url{ szUrl }, cpr::Header{ {"Authorization", "Bearer " + token} });

        size_t numPages = 1;
        static const std::string page = "&page=";
        auto numPagesStr = rLink.header["Link"];
        if (!numPagesStr.empty())
        {
            numPagesStr = numPagesStr.substr(find_nth(numPagesStr, 0, page, 1) + page.length());
            numPagesStr = numPagesStr.substr(0, numPagesStr.find('>'));
            numPages = std::stoi(numPagesStr);
        }

        for (size_t i = 1; i <= numPages; i++)
        {
            cpr::Response r = {};
            if (token.empty())
                r = cpr::Get(cpr::Url{ szUrl + page + std::to_string(i) });
            else
                r = cpr::Get(cpr::Url{ szUrl + page + std::to_string(i) }, cpr::Header{ {"Authorization", "Bearer " + token} });

            if (r.status_code == 200)
            {
                Json::Value parsedFromString;
                Json::Reader reader;
                bool parsingSuccessful = reader.parse(r.text, parsedFromString);

                if (parsingSuccessful)
                {
                    std::wcout << L"GitHub's response parsed successfully." << L" Page " << i << "." << std::endl;

                    std::tuple<int32_t, std::string, std::string, std::string> result;

                    // default use case, when file that's being updated is inside the zip archive of the same name or starts with it
                    for (Json::ValueConstIterator it = parsedFromString.begin(); it != parsedFromString.end(); ++it)
                    {
                        const Json::Value& jVal = *it;

                        if (jVal["draft"] == true /*|| jVal["prerelease"] == true*/)
                            continue;

                        for (auto i = 0; i < (int)jVal["assets"].size(); i++)
                        {
                            auto assetName = toLowerStr(std::string(jVal["assets"][i]["name"].asString().c_str()));
                            if (starts_with(assetName.c_str(), toString(strFileName).c_str(), false)) //case INsensitive!
                            {
                                if (!machine.empty())
                                {
                                    std::vector<std::string> v;
                                    if (machine == L"x64")
                                        v = { "x86", "32bit", "32-bit", "win32", "win-32" };
                                    else if (machine == L"x86")
                                        v = { "x64", "64bit", "64-bit", "x86_64", "win64", "win-64" };

                                    if (std::any_of(v.cbegin(), v.cend(), [&](auto& i) { return assetName.contains(i); }))
                                        continue;
                                }

                                date::sys_seconds tp;
                                auto now = date::floor<std::chrono::seconds>(std::chrono::system_clock::now());
                                std::istringstream ss{ jVal["assets"][i]["updated_at"].asString() };
                                ss >> date::parse("%FT%TZ", tp); // "updated_at": "2016-08-16T11:42:53Z" ISO 8601
                                auto nRemoteFileUpdateTime = date::make_time(now - tp).hours().count();

                                if (std::get<1>(result).empty() || std::get<0>(result) >= nRemoteFileUpdateTime)
                                    result = std::make_tuple((bool(ss) ? nRemoteFileUpdateTime : -2), jVal["assets"][i]["browser_download_url"].asString(), jVal["assets"][i]["name"].asString(), jVal["assets"][i]["size"].asString());
                            }
                        }
                    }

                    if (!std::get<1>(result).empty())
                    {
                        std::wcout << L"Found " << toWString(std::get<2>(result)) << L" on GitHub." << std::endl;
                        return result;
                    }

                    //alternative use cases, files and archives are named differently, so we'll try to guess and find something to update
                    std::wcout << L"Archive with the same name not found, latest release with single asset will be used instead." << std::endl;

                    Json::ValueConstIterator it = parsedFromString.begin();
                    for (; it != parsedFromString.end(); ++it)
                    {
                        const Json::Value& jVal = *it;
                        if (jVal["draft"] == true || jVal["prerelease"] == true)
                            continue;
                        else
                            break;
                    }

                    if (it != parsedFromString.end())
                    {
                        const Json::Value& jVal = *it;

                        if (jVal["assets"].size() == 1)
                        {
                            std::wcout << L"Found " << toWString((jVal["assets"][0]["name"]).asString()) << L" on GitHub." << std::endl;

                            date::sys_seconds tp;
                            auto now = date::floor<std::chrono::seconds>(std::chrono::system_clock::now());
                            std::istringstream ss{ jVal["assets"][0]["updated_at"].asString() };
                            ss >> date::parse("%FT%TZ", tp); // "updated_at": "2016-08-16T11:42:53Z" ISO 8601
                            auto nRemoteFileUpdateTime = date::make_time(now - tp).hours().count();

                            return std::make_tuple((bool(ss) ? nRemoteFileUpdateTime : -2), jVal["assets"][0]["browser_download_url"].asString(), jVal["assets"][0]["name"].asString(), jVal["assets"][0]["size"].asString());
                        }
                    }
                }
                std::wcout << L"Nothing is found on GitHub." << std::endl << std::endl;
            }
            else
            {
                std::wcout << L"Something wrong! " << L"Status code: " << r.status_code << std::endl;
            }
        }
    }
    else
    {
        std::wcout << "Connecting to " << toWString(szUrl) << std::endl;
        cpr::Response rHead = {};
        rHead = cpr::Head(cpr::Url{ szUrl });

        if (rHead.status_code == 405) // method not allowed
        {
            cpr::Response r = {};
            r = cpr::Get(cpr::Url{ szUrl });

            if (r.status_code == 200) // ok
            {
                rHead = r;
            }
        }

        if (rHead.status_code == 200) // ok
        {
            std::wcout << "Found " << toWString(rHead.header["Content-Type"]) << std::endl;
            std::string szDownloadURL = rHead.url.c_str();
            std::string szDownloadName = rHead.header["Content-Disposition"];
            if (szDownloadName.contains("filename="))
                szDownloadName = szDownloadName.substr(szDownloadName.find("filename=") + 9);
            else
                szDownloadName = toString(strFileName);
            std::string szFileSize = rHead.header["Content-Length"];

            date::sys_seconds tp;
            auto now = date::floor<std::chrono::seconds>(std::chrono::system_clock::now());
            std::istringstream ss{ rHead.header["Last-Modified"] };
            ss >> date::parse("%a, %d %b %Y %H:%M:%S %Z", tp); // "updated_at": "2016-08-16T11:42:53Z" ISO 8601
            auto nRemoteFileUpdateTime = date::make_time(now - tp).hours().count();

            return std::make_tuple((bool(ss) ? nRemoteFileUpdateTime : -2), szDownloadURL, szDownloadName, szFileSize);
        }
        else if (rHead.status_code == 403) //forbidden
        {
            std::wcout << L"Found " + toWString(szUrl.substr(szUrl.find_last_of('/'))) << std::endl;
            std::string szDownloadURL = szUrl;
            std::string tempStr = szDownloadURL.substr(szDownloadURL.find_last_of('/') + 1);
            auto endPos = tempStr.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567890_.-");
            std::string szDownloadName = tempStr.substr(0, (endPos == 0) ? std::string::npos : endPos);
            std::string szFileSize = "0";
            return std::make_tuple(0, szDownloadURL, szDownloadName, szFileSize);
        }
        else
            std::wcout << L"Seems like this archive is invalid or the url is broken." << std::endl;
    }

    return std::make_tuple(-1, "", "", "");
}

std::chrono::system_clock::time_point FileTime2TimePoint(const FILETIME& ft)
{
    SYSTEMTIME st = { 0 };
    if (!FileTimeToSystemTime(&ft, &st))
    {
        std::cerr << "Invalid FILETIME" << std::endl;
        return std::chrono::system_clock::time_point((std::chrono::system_clock::time_point::min)());
    }

    ULARGE_INTEGER ull;
    ull.LowPart = ft.dwLowDateTime;
    ull.HighPart = ft.dwHighDateTime;

    time_t secs = ull.QuadPart / 10000000ULL - 11644473600ULL;
    std::chrono::milliseconds ms((ull.QuadPart / 10000ULL) % 1000);

    auto tp = std::chrono::system_clock::from_time_t(secs);
    tp += ms;
    return tp;
}

int32_t GetLocalFileInfo(FILETIME ftCreate, FILETIME ftLastAccess, FILETIME ftLastWrite)
{
    auto now = date::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    return date::make_time(now - FileTime2TimePoint(ftLastWrite)).hours().count();
}

void ProcessFiles()
{
    std::vector<std::tuple<std::wstring, std::wstring, std::wstring, WIN32_FIND_DATAW, std::wstring, bool, std::string>> FilesUpdateData;
    std::vector<FileUpdateInfo> FilesToUpdate;
    std::vector<FileUpdateInfo> FilesToDownload;
    std::vector<std::wstring> FilesPresent;
    std::set<std::pair<std::string, std::string>> IniExcludes;

#ifndef STATICLIB
    auto cb = [&FilesUpdateData, &IniExcludes](std::wstring& s, WIN32_FIND_DATAW fd)
    {
        auto strFileName = s.substr(s.rfind('\\') + 1);

        if (strFileName.find(L".deleteonnextlaunch") != std::wstring::npos)
            return;

        //Checking dll
        std::wstring machine = L"";
        if (toLowerWStr(s).ends_with(L".dll"))
        {
            IMAGE_NT_HEADERS headers;
            if (GetImageFileHeaders(s, headers))
            {
                if (headers.FileHeader.Machine == IMAGE_FILE_MACHINE_I386)
                    machine = L"x86";
                else if (headers.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64)
                    machine = L"x64";
            }
        }

        // Checking password
        std::string password = iniReader.ReadString(toString(strFileName), "Password", "");

        //Checking ini file for url
        auto iniEntry = iniReader.ReadString("MODS", toString(strFileName), "");
        if (!iniEntry.empty())
        {
            removeQuotesFromString(iniEntry);
            FilesUpdateData.push_back(std::make_tuple(s, toWString(iniEntry), L"", fd, machine, muAlwaysUpdate, password));
            return;
        }

        // Checking file info for url
        uint32_t dwDummy;
        uint32_t versionInfoSize = GetFileVersionInfoSizeW(s.c_str(), (LPDWORD)&dwDummy);

        if (versionInfoSize)
        {
            std::vector<wchar_t> versionInfoVec(versionInfoSize);
            GetFileVersionInfoW(s.c_str(), dwDummy, versionInfoSize, versionInfoVec.data());
            std::wstring versionInfo(versionInfoVec.begin(), versionInfoVec.end());

            std::wstring updateUrl{};
            std::wstring devUpdateUrl{};

            if (versionInfo.find(DEVUPDATEURL) != std::wstring::npos)
            {
                try
                {
                    devUpdateUrl = versionInfo.substr(versionInfo.find(DEVUPDATEURL) + wcslen(DEVUPDATEURL) + sizeof(wchar_t));
                    devUpdateUrl = devUpdateUrl.substr(0, devUpdateUrl.find_first_of(L'\0'));
                }
                catch (std::out_of_range& ex)
                {
                    std::wcout << ex.what() << std::endl;
                }
            }

            if (versionInfo.find(UPDATEURL) != std::wstring::npos)
            {
                try
                {
                    updateUrl = versionInfo.substr(versionInfo.find(UPDATEURL) + wcslen(UPDATEURL) + sizeof(wchar_t));
                    updateUrl = updateUrl.substr(0, updateUrl.find_first_of(L'\0'));
                }
                catch (std::out_of_range& ex)
                {
                    std::wcout << ex.what() << std::endl;
                }
            }

            if (!updateUrl.empty() || !devUpdateUrl.empty())
                FilesUpdateData.push_back(std::make_tuple(s, updateUrl, devUpdateUrl, fd, machine, muAlwaysUpdate, password));
        }
    };

    FindFilesRecursively(modulePath, cb);
#else
    auto& info = *muGetInfoPtr();
    for (auto& m : info)
    {
        auto filePath = GetModulePath(m.first);
        WIN32_FILE_ATTRIBUTE_DATA fInfo;
        GetFileAttributesExW(filePath.c_str(), GetFileExInfoStandard, &fInfo);
        WIN32_FIND_DATAW fd = {};
        fd.ftCreationTime = fInfo.ftCreationTime;
        fd.ftLastAccessTime = fInfo.ftLastAccessTime;
        fd.ftLastWriteTime = fInfo.ftLastWriteTime;
        std::wstring machine = L"";
        HMODULE hm = NULL;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&ProcessFiles, &hm);
        IMAGE_NT_HEADERS* headers = ImageNtHeader(hm);
        if (headers->FileHeader.Machine == IMAGE_FILE_MACHINE_I386)
            machine = L"x86";
        else if (headers->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64)
            machine = L"x64";

        FilesUpdateData.emplace_back(std::make_tuple(filePath.wstring(), toWString(m.second.muUpdateURL), toWString(m.second.muDevUpdateURL), fd, machine, m.second.muAlwaysUpdate, m.second.muArchivePassword));
    }

    for (auto& m : info)
    {
        if (m.second.muSkipUpdateCompleteDialog)
            muSkipUpdateCompleteDialog = true;
        else
        {
            muSkipUpdateCompleteDialog = false;
            break;
        }
    }
#endif
    for (auto& tuple : FilesUpdateData)
    {
        std::wstring path = std::get<0>(tuple);
        std::wstring url = std::get<1>(tuple);
        std::wstring devurl = std::get<2>(tuple);
        WIN32_FIND_DATAW fd = std::get<3>(tuple);
        std::wstring machine = std::get<4>(tuple);
        bool bAlwaysUpdate = std::get<5>(tuple);
        std::string szPassword = std::get<6>(tuple);

        auto strFileName = path.substr(path.rfind('\\') + 1);

        if (url.empty() && !devurl.empty())
        {
            url = devurl;
            devurl.clear();
        }

        std::wcout << strFileName << " " << "found." << std::endl;
        std::wcout << "Update URL:" << " " << url << std::endl;
        std::wcout << "Dev URL:" << " " << devurl << std::endl;

        int32_t nLocaFileUpdatedHoursAgo = GetLocalFileInfo(fd.ftCreationTime, fd.ftLastAccessTime, fd.ftLastWriteTime);
        auto RemoteInfo = GetRemoteFileInfo(strFileName, url, machine);
        auto nRemoteFileUpdatedHoursAgo = std::get<0>(RemoteInfo);

        if (!devurl.empty())
        {
            auto RemoteInfoDev = GetRemoteFileInfo(strFileName, devurl, machine);
            auto nRemoteFileUpdatedHoursAgoDev = std::get<0>(RemoteInfoDev);
            if (nRemoteFileUpdatedHoursAgoDev < nRemoteFileUpdatedHoursAgo)
            {
                RemoteInfo = RemoteInfoDev;
                nRemoteFileUpdatedHoursAgo = nRemoteFileUpdatedHoursAgoDev;
            }
        }

        auto szDownloadURL = std::get<1>(RemoteInfo);
        auto szDownloadName = std::get<2>(RemoteInfo);
        auto szFileSize = std::get<3>(RemoteInfo);
        FilesPresent.push_back(strFileName);

        if (nRemoteFileUpdatedHoursAgo != -1 && !szDownloadURL.empty() && !szFileSize.empty())
        {
            if (nRemoteFileUpdatedHoursAgo < nLocaFileUpdatedHoursAgo || bAlwaysUpdate)
            {
                auto nFileSize = std::stoi(szFileSize);
                std::wcout << "Download link: " << toWString(szDownloadURL) << std::endl;
                if (nRemoteFileUpdatedHoursAgo != -2)
                    std::wcout << "Remote file updated " << getTimeAgoW(nRemoteFileUpdatedHoursAgo) << "." << std::endl;
                else
                    std::wcout << "Last-Modified header was not specified. Update date unknown." << std::endl;
                std::wcout << "Local file updated " << getTimeAgoW(nLocaFileUpdatedHoursAgo) << "." << std::endl;
                std::wcout << "File size: " << nFileSize << "KB." << std::endl;
                std::wcout << std::endl;

                FileUpdateInfo fui;
                fui.wszFullFilePath = path;
                fui.wszFileName = strFileName;
                fui.wszDownloadURL = toWString(szDownloadURL);
                fui.wszDownloadName = toWString(szDownloadName);
                fui.szPassword = szPassword;
                fui.nRemoteFileUpdatedHoursAgo = nRemoteFileUpdatedHoursAgo;
                fui.nLocaFileUpdatedHoursAgo = nLocaFileUpdatedHoursAgo;
                fui.nFileSize = nFileSize;

                FilesToUpdate.push_back(fui);
            }
            else
            {
                std::wcout << L"No updates available." << std::endl;
                //MessageBox(NULL, "No updates available.", "modupdater", MB_OK | MB_ICONINFORMATION);
            }
        }
        //else
        //{
        //	std::wcout << L"Error." << std::endl;
        //}
        std::wcout << std::endl;
    }

#ifndef STATICLIB
    mINI::INIFile ini(iniReader.GetIniPath());
    mINI::INIStructure iniStruct;
    ini.read(iniStruct);

    for (auto const& it : iniStruct)
    {
        auto const& section = std::get<0>(it);
        if (section == "MODS")
        {
            auto const& collection = std::get<1>(it);
            for (auto const& it2 : collection)
            {
                auto strIni = std::get<0>(it2);
                auto iniEntry = std::get<1>(it2);

                if (strIni.empty() || iniEntry.empty())
                    continue;

                removeQuotesFromString(iniEntry);

                if (strIni.at(0) == '.')
                    continue;

                auto excl = std::find_if(IniExcludes.begin(), IniExcludes.end(),
                    [&strIni, &iniEntry](auto it) { return (toLowerStr(it.first) == toLowerStr(strIni) && toLowerStr(it.second) == toLowerStr(iniEntry)); });

                if (excl != IniExcludes.end())
                    continue;

                auto iter = std::find_if(FilesPresent.begin(), FilesPresent.end(),
                    [&strIni](const std::wstring& wszFileName) -> bool { return toLowerWStr(wszFileName) == toLowerWStr(strIni); });

                if (iter == FilesPresent.end())
                {
                    auto RemoteInfo = GetRemoteFileInfo(toWString(strIni), toWString(iniEntry), L"");
                    auto nRemoteFileUpdatedHoursAgo = std::get<0>(RemoteInfo);
                    auto szDownloadURL = std::get<1>(RemoteInfo);
                    auto szDownloadName = std::get<2>(RemoteInfo);
                    auto szFileSize = std::get<3>(RemoteInfo);

                    if (nRemoteFileUpdatedHoursAgo != -1 && !szDownloadURL.empty())
                    {
                        auto nFileSize = std::stoi(szFileSize);
                        std::wcout << L"Download link: " << toWString(szDownloadURL) << std::endl;
                        if (nRemoteFileUpdatedHoursAgo != -2)
                            std::wcout << "Remote file updated " << getTimeAgoW(nRemoteFileUpdatedHoursAgo) << "." << std::endl;
                        else
                            std::wcout << "Last-Modified header was not specified. Update date unknown." << std::endl;
                        std::wcout << L"Local file is not present." << std::endl;
                        std::wcout << L"File size: " << nFileSize << "KB." << std::endl;
                        std::wcout << std::endl;

                        FileUpdateInfo fui;
                        fui.wszFileName = toWString(strIni);
                        fui.wszFullFilePath = selfPath / fui.wszFileName;
                        fui.wszDownloadURL = toWString(szDownloadURL);
                        fui.wszDownloadName = toWString(szDownloadName);
                        fui.nRemoteFileUpdatedHoursAgo = nRemoteFileUpdatedHoursAgo;
                        fui.nLocaFileUpdatedHoursAgo = INT_MAX;
                        fui.nFileSize = nFileSize;

                        FilesToDownload.push_back(fui);
                    }
                    else
                    {
                        std::wcout << L"No updates available." << std::endl;
                        //MessageBox(NULL, "No updates available.", "modupdater", MB_OK | MB_ICONINFORMATION);
                    }
                }
            }
        }
    }
#else
    //TODO: FilesToDownload from static lib
#endif

    if (!FilesToUpdate.empty() || !FilesToDownload.empty())
        ShowUpdateDialog(FilesToUpdate, FilesToDownload);
    else
        std::wcout << L"No files found to process." << std::endl;

    if (muMutexHandle)
    {
        delete muInfoPtr;
        muInfoPtr = nullptr;
        CloseHandle(muMutexHandle);
    }

    std::wcout.rdbuf(outbuf);
}

void InitModupdater()
{
    selfPath = GetThisModulePath();
    selfName = GetThisModuleName();
    selfNameNoExt = selfName.stem();
    modulePath = GetExeModulePath();
    processPath = GetModulePath(GetModuleHandleW(NULL));

    if (!CanAccessFolder(modulePath.c_str(), GENERIC_READ))
        return;

#ifndef STATICLIB
    CreateMutexW(NULL, TRUE, mtxNameAsi);

    iniReader.SetIniPath(L"modupdater.ini");

    muAlwaysUpdate = iniReader.ReadInteger("DEBUG", "AlwaysUpdate", 0) != 0;
    muSkipUpdateCompleteDialog = iniReader.ReadInteger("MISC", "SkipUpdateCompleteDialog", 0) != 0;
    auto nUpdateFrequencyInHours = iniReader.ReadInteger("DATE", "UpdateFrequencyInHours", 6);
    auto szWhenLastUpdateAttemptWasInHours = iniReader.ReadString("DATE", "WhenLastUpdateAttemptWas", "");

    if (iniReader.ReadInteger("MISC", "OutputLogToFile", 1) != 0)
    {
        logFile.open(modulePath / L"modupdater.log");
        outbuf = std::wcout.rdbuf(logFile.rdbuf());
    }

#ifdef EXECUTABLE
    CleanupLockedFiles();
#else
    std::thread([]
    {
        CleanupLockedFiles();
    }).detach();
#endif

    auto now = date::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    date::sys_seconds tp;
    auto nWhenLastUpdateAttemptWas = std::chrono::hours(INT_MAX);
    std::istringstream in{ szWhenLastUpdateAttemptWasInHours };
    in >> date::parse("%D %T %Z", tp);
    if (now >= tp)
        nWhenLastUpdateAttemptWas = date::make_time(now - tp).hours();

#ifndef _DEBUG
    if (bool(in) && nWhenLastUpdateAttemptWas < std::chrono::hours(nUpdateFrequencyInHours) && !iniReader.ReadInteger("DEBUG", "AlwaysUpdate", 0) != 0)
    {
        std::wcout << L"Last update attempt was " << nWhenLastUpdateAttemptWas.count() << L" hours ago." << std::endl;
        std::wcout << L"Modupdater is configured to update once every " << nUpdateFrequencyInHours << L" hours." << std::endl;
    }
    else
#endif
    {
        std::wcout << "Current directory: " << modulePath << std::endl;

        token = iniReader.ReadString("DEBUG", "Token", "");

        if (!CanAccessFolder(modulePath.c_str(), GENERIC_READ | GENERIC_WRITE))
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
            ProcessFiles();
        }).detach();
#endif
    }
#else
    CleanupLockedFiles();
    ProcessFiles();
#endif
}

// API
#ifdef STATICLIB
void muSetUpdateURL(HMODULE hModule, const char* url)
{
    std::lock_guard<std::mutex> lock(muMutex);
    auto& info = *muGetInfoPtr();
    info[hModule].muUpdateURL = url;
}

void muSetDevUpdateURL(HMODULE hModule, const char* url)
{
    std::lock_guard<std::mutex> lock(muMutex);
    auto& info = *muGetInfoPtr();
    info[hModule].muDevUpdateURL = url;
}

void muSetArchivePassword(HMODULE hModule, const char* password)
{
    std::lock_guard<std::mutex> lock(muMutex);
    auto& info = *muGetInfoPtr();
    info[hModule].muArchivePassword = password;
}

void muSetSkipUpdateCompleteDialog(HMODULE hModule, bool skipcompletedialog)
{
    std::lock_guard<std::mutex> lock(muMutex);
    auto& info = *muGetInfoPtr();
    info[hModule].muSkipUpdateCompleteDialog = skipcompletedialog;
}

void muSetAlwaysUpdate(HMODULE hModule, bool alwaysupdate)
{
    std::lock_guard<std::mutex> lock(muMutex);
    auto& info = *muGetInfoPtr();
    info[hModule].muAlwaysUpdate = alwaysupdate;
}

void muInit()
{
    std::thread([]()
    {
        auto start = std::chrono::high_resolution_clock::now();
        while (true)
        {
            auto now = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - start);

            if (duration.count() >= 5)
            {
                std::lock_guard<std::mutex> lock(muMutex);
                if (::OpenMutexW(MUTEX_ALL_ACCESS, FALSE, mtxNameAsi))
                {
                    if (muMutexHandle)
                    {
                        delete muInfoPtr;
                        muInfoPtr = nullptr;
                        CloseHandle(muMutexHandle);
                        muMutexHandle = NULL;
                    }
                    return;
                }
                else if (!muMutexHandle)
                {
                    return;
                }
                else
                {
                    InitModupdater();
                }
                return;
            }
            std::this_thread::yield();
        }
    }).detach();
}

namespace installer
{
    std::string getRegistryValue(HKEY hKeyRoot, const std::wstring& subKeyPath, const std::wstring& valueName)
    {
        HKEY hKey;
        std::string result = "";
        if (RegOpenKeyEx(hKeyRoot, subKeyPath.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS)
        {
            wchar_t buffer[MAX_PATH * 2] = { 0 }; // Initialize buffer
            DWORD bufferSize = sizeof(buffer) - sizeof(wchar_t); // Leave space for null terminator
            if (RegQueryValueEx(hKey, valueName.c_str(), nullptr, nullptr, (LPBYTE)buffer, &bufferSize) == ERROR_SUCCESS)
            {
                result = toString(buffer);
            }
            RegCloseKey(hKey);
        }
        return result;
    }

    std::string extractSpecificVdfValue(const std::string& vdfContent, const std::string& key)
    {
        std::string searchKeyPattern = "\"" + key + "\"";
        size_t keyPos = vdfContent.find(searchKeyPattern);

        if (keyPos == std::string::npos)
        {
            return "";
        }

        // Look for the first quote of the value part
        size_t valueStartQuotePos = vdfContent.find('"', keyPos + searchKeyPattern.length());
        if (valueStartQuotePos == std::string::npos)
        {
            return "";
        }
        valueStartQuotePos++; // Move past the quote

        // Look for the second quote of the value part
        size_t valueEndQuotePos = vdfContent.find('"', valueStartQuotePos);
        if (valueEndQuotePos == std::string::npos)
        {
            return "";
        }

        return vdfContent.substr(valueStartQuotePos, valueEndQuotePos - valueStartQuotePos);
    }


    // Gets Steam library folder base paths.
    // steamInstallPath is the root Steam directory, e.g., "C:\Program Files (x86)\Steam"
    std::vector<std::string> getSteamLibraryFolders(const std::string& steamInstallPath)
    {
        std::vector<std::string> libraryBasePaths;

        if (!steamInstallPath.empty() && std::filesystem::exists(steamInstallPath))
        {
            libraryBasePaths.push_back(steamInstallPath); // Main Steam install dir is a library base
        }

        std::filesystem::path libraryFoldersVdfPath = std::filesystem::path(steamInstallPath) / "steamapps" / "libraryfolders.vdf";

        if (std::filesystem::exists(libraryFoldersVdfPath))
        {
            std::ifstream file(libraryFoldersVdfPath);
            if (file.is_open())
            {
                std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                file.close();

                size_t currentPos = 0;
                std::string pathKey = "\"path\"";
                while ((currentPos = content.find(pathKey, currentPos)) != std::string::npos)
                {
                    size_t valueStart = content.find('"', currentPos + pathKey.length());
                    if (valueStart == std::string::npos) break;
                    valueStart++;
                    size_t valueEnd = content.find('"', valueStart);
                    if (valueEnd == std::string::npos) break;

                    std::string pathStr = content.substr(valueStart, valueEnd - valueStart);
                    if (!pathStr.empty() && std::filesystem::exists(pathStr))
                    {
                        bool alreadyAdded = false;
                        for (const auto& p : libraryBasePaths)
                        {
                            if (std::filesystem::equivalent(std::filesystem::path(p), std::filesystem::path(pathStr)))
                            {
                                alreadyAdded = true;
                                break;
                            }
                        }
                        if (!alreadyAdded)
                        {
                            libraryBasePaths.push_back(pathStr);
                        }
                    }
                    currentPos = valueEnd + 1;
                }
            }
        }
        return libraryBasePaths;
    }

    // Finds a Steam game by its AppID or name.
    // Returns the installation path of the game, or an empty string if not found.
    std::string findSteamGame(const std::string& gameIdentifier, const std::string& appendSubfolder)
    {
        std::string steamPath;
        steamPath = getRegistryValue(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath");
        if (steamPath.empty())
        {
            steamPath = getRegistryValue(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath");
        }
        if (steamPath.empty())
        { // Fallback for 32-bit systems or non-WOW6432Node installs
            steamPath = getRegistryValue(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Valve\\Steam", L"InstallPath");
        }

        if (steamPath.empty())
        {
            return "";
        }

        std::replace(steamPath.begin(), steamPath.end(), '/', '\\'); // Normalize path separators

        std::vector<std::string> libraryBasePaths = getSteamLibraryFolders(steamPath);
        if (libraryBasePaths.empty())
        {
            return "";
        }

        std::string lowerGameIdentifier = gameIdentifier;
        std::transform(lowerGameIdentifier.begin(), lowerGameIdentifier.end(), lowerGameIdentifier.begin(), ::tolower);

        for (const std::string& libBasePathStr : libraryBasePaths)
        {
            std::filesystem::path libBasePath = libBasePathStr;
            std::filesystem::path steamAppsPath = libBasePath / "steamapps";

            if (!std::filesystem::exists(steamAppsPath) || !std::filesystem::is_directory(steamAppsPath))
            {
                continue;
            }

            try
            {
                for (const auto& entry : std::filesystem::directory_iterator(steamAppsPath))
                {
                    if (entry.is_regular_file())
                    {
                        std::string filename = entry.path().filename().string();
                        if (filename.rfind("appmanifest_", 0) == 0 && filename.size() > 16 && filename.substr(filename.size() - 4) == ".acf")
                        { // "appmanifest_X.acf"
                            std::ifstream acfFile(entry.path());
                            if (acfFile.is_open())
                            {
                                std::string acfContent((std::istreambuf_iterator<char>(acfFile)), std::istreambuf_iterator<char>());
                                acfFile.close();

                                std::string appId = extractSpecificVdfValue(acfContent, "appid");
                                std::string name = extractSpecificVdfValue(acfContent, "name");
                                std::string installDir = extractSpecificVdfValue(acfContent, "installdir");

                                std::string lowerName = name;
                                std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

                                if ((!appId.empty() && appId == gameIdentifier) || (!name.empty() && lowerName == lowerGameIdentifier))
                                {
                                    if (!installDir.empty())
                                    {
                                        std::filesystem::path gamePath = steamAppsPath / "common" / installDir / appendSubfolder;
                                        if (std::filesystem::exists(gamePath) && std::filesystem::is_directory(gamePath))
                                        {
                                            return gamePath.lexically_normal().string();
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            catch (const std::filesystem::filesystem_error&)
            {
                continue; // Skip this library if there's an access issue
            }
        }
        return "";
    }

    // Finds a Rockstar game by its name.
    // Returns the installation path of the game, or an empty string if not found.
    std::string findRockstarGame(const std::string& gameName, const std::string& appendSubfolder)
    {
        std::vector<std::wstring> RGL_Specific_RegistryPaths =
        {
            L"SOFTWARE\\WOW6432Node\\Rockstar Games\\" + toWString(gameName),
            L"SOFTWARE\\Rockstar Games\\" + toWString(gameName)
        };

        for (const auto& specificRegPath : RGL_Specific_RegistryPaths)
        {
            try
            {
                std::filesystem::path installFolder = getRegistryValue(HKEY_LOCAL_MACHINE, specificRegPath, L"InstallFolder");
                std::filesystem::path path = installFolder / appendSubfolder;
                if (!path.empty())
                {
                    if (std::filesystem::exists(path) && std::filesystem::is_directory(path))
                    {
                        return path.lexically_normal().string();
                    }
                }
            }
            catch (const std::filesystem::filesystem_error&)
            {
                continue;
            }
        }

        return "";
    }

    // Custom stream buffer for reading file regions
    class FileRegionStreamBuf : public std::streambuf
    {
    private:
        std::ifstream file_;
        std::streampos start_pos_;
        std::streampos end_pos_;
        std::streampos current_pos_;
        static const size_t buffer_size_ = 8192;
        char buffer_[buffer_size_];

    public:
        FileRegionStreamBuf(const std::string& filename, std::streampos start, std::streampos size)
            : start_pos_(start), current_pos_(start)
        {
            end_pos_ = start + size;
            file_.open(filename, std::ios::binary);
            if (file_.is_open())
            {
                file_.seekg(start_pos_);
                setg(buffer_, buffer_, buffer_);
            }
        }

        ~FileRegionStreamBuf()
        {
            if (file_.is_open())
            {
                file_.close();
            }
        }

        bool is_open() const
        {
            return file_.is_open();
        }

    protected:
        std::streampos seekoff(std::streamoff off, std::ios_base::seekdir way,
            std::ios_base::openmode which = std::ios_base::in) override
        {
            if (!file_.is_open()) return -1;

            std::streampos new_pos;
            switch (way)
            {
            case std::ios_base::beg:
                new_pos = start_pos_ + off;
                break;
            case std::ios_base::cur:
                new_pos = current_pos_ + off;
                break;
            case std::ios_base::end:
                new_pos = end_pos_ + off;
                break;
            default:
                return -1;
            }

            if (new_pos < start_pos_ || new_pos > end_pos_)
            {
                return -1;
            }

            current_pos_ = new_pos;
            file_.seekg(current_pos_);
            setg(buffer_, buffer_, buffer_);
            return current_pos_ - start_pos_;
        }

        std::streampos seekpos(std::streampos sp,
            std::ios_base::openmode which = std::ios_base::in) override
        {
            return seekoff(sp, std::ios_base::beg, which);
        }

        int underflow() override
        {
            if (!file_.is_open() || current_pos_ >= end_pos_)
            {
                return traits_type::eof();
            }

            file_.seekg(current_pos_);
            std::streamsize to_read = std::min(static_cast<std::streamsize>(buffer_size_),
                static_cast<std::streamsize>(end_pos_ - current_pos_));

            file_.read(buffer_, to_read);
            std::streamsize bytes_read = file_.gcount();

            if (bytes_read == 0)
            {
                return traits_type::eof();
            }

            current_pos_ += bytes_read;
            setg(buffer_, buffer_, buffer_ + bytes_read);
            return traits_type::to_int_type(*gptr());
        }
    };

    class FileRegionStream : public std::istream
    {
    private:
        FileRegionStreamBuf buffer_;

    public:
        FileRegionStream(const std::string& filename, std::streampos start, std::streampos size)
            : std::istream(&buffer_), buffer_(filename, start, size)
        {
            if (!buffer_.is_open())
            {
                setstate(std::ios::failbit);
            }
        }

        bool is_open() const
        {
            return buffer_.is_open();
        }
    };

    struct EmbeddedZip
    {
        std::string name;
        uint64_t size;
        std::streampos offset;
        std::unique_ptr<FileRegionStream> stream;
    };

    // Fast ZIP reader using direct footer parsing with debug output
    class EmbeddedZipReader
    {
    public:
        static std::vector<EmbeddedZip> extractZipsFromExe(const std::string& exePath)
        {
            std::vector<EmbeddedZip> zips;
            std::ifstream file(exePath, std::ios::binary);
            if (!file.is_open())
            {
                return zips;
            }

            file.seekg(0, std::ios::end);
            std::streampos fileSize = file.tellg();

            // Check if file is big enough to contain at least one ZIP entry footer
            // Footer minimum size: magic(4) + zipSize(8) + nameLength(4) + name(1) = 17 bytes
            if (fileSize <= 17)
            {
                file.close();
                return zips;
            }

            // Get file content and check for ZIPE magic at the end
            file.seekg(-4, std::ios::end); // Position to read the magic
            uint32_t endMagic;
            file.read(reinterpret_cast<char*>(&endMagic), 4);

            if (endMagic != 0x5A495045)
            { // "ZIPE"
                file.close();
                return zips;
            }

            // Process from the end of the file
            std::streampos currentPos = fileSize - static_cast<std::streampos>(4); // Start after magic

            // Loop to read all appended ZIPs
            while (currentPos > 0)
            {
                // Read ZIP size (positioned before magic)
                file.seekg(currentPos - static_cast<std::streampos>(8), std::ios::beg);
                uint64_t zipSize;
                if (!file.read(reinterpret_cast<char*>(&zipSize), 8))
                {
                    break;
                }
                currentPos -= 8;

                // Read name length (positioned before size)
                file.seekg(currentPos - static_cast<std::streampos>(4), std::ios::beg);
                uint32_t nameLength;
                if (!file.read(reinterpret_cast<char*>(&nameLength), 4))
                {
                    break;
                }
                currentPos -= 4;

                // Validate name length
                if (nameLength == 0 || nameLength > 1024)
                {
                    // Try to recover by searching for the next valid footer
                    currentPos = std::max<std::streampos>(0, currentPos - static_cast<std::streampos>(16)); // Move back and continue searching
                    continue;
                }

                // Read ZIP name
                if (currentPos < static_cast<std::streampos>(nameLength))
                {
                    break;
                }

                file.seekg(currentPos - static_cast<std::streampos>(nameLength), std::ios::beg);
                std::string zipName(nameLength, '\0');
                if (!file.read(&zipName[0], nameLength))
                {
                    break;
                }
                currentPos -= nameLength;

                // Calculate ZIP data offset
                std::streampos zipDataOffset = currentPos - static_cast<std::streampos>(zipSize);

                if (zipDataOffset < 0)
                {
                    break;
                }

                // Create EmbeddedZip entry
                EmbeddedZip embeddedZip;
                embeddedZip.name = zipName;
                embeddedZip.size = zipSize;
                embeddedZip.offset = zipDataOffset;

                // Create stream for the ZIP data
                embeddedZip.stream = std::make_unique<FileRegionStream>(exePath, zipDataOffset, zipSize);

                if (embeddedZip.stream->is_open())
                {
                    zips.push_back(std::move(embeddedZip));

                    // Move position to before the current ZIP data for the next iteration
                    currentPos = zipDataOffset;

                    // Look for another magic
                    if (currentPos >= 4)
                    {
                        file.seekg(currentPos - static_cast<std::streampos>(4), std::ios::beg);
                        uint32_t prevMagic;
                        if (file.read(reinterpret_cast<char*>(&prevMagic), 4) && prevMagic == 0x5A495045)
                        {
                            // Found another ZIP marker
                            currentPos -= 4;
                        }
                        else
                        {
                            // No more ZIPs
                            break;
                        }
                    }
                    else
                    {
                        // No more space for ZIPs
                        break;
                    }
                }
                else
                {
                    break;
                }
            }

            file.close();
            return zips;
        }

        static std::istream* getZipStream(const std::vector<EmbeddedZip>& zips, const std::string& name)
        {
            for (const auto& zip : zips)
            {
                if (zip.name == name)
                {
                    if (zip.stream && zip.stream->is_open())
                    {
                        zip.stream->clear();
                        zip.stream->seekg(0);
                        return zip.stream.get();
                    }
                }
            }
            return nullptr;
        }

        static bool hasEmbeddedZips(const std::string& exePath)
        {
            std::ifstream file(exePath, std::ios::binary);
            if (!file.is_open()) return false;

            file.seekg(0, std::ios::end);
            std::streampos fileSize = file.tellg();
            if (fileSize < 4) return false;

            file.seekg(-4, std::ios::end);
            uint32_t magic;
            if (!file.read(reinterpret_cast<char*>(&magic), 4))
            {
                file.close();
                return false;
            }
            file.close();
            return magic == 0x5A495045; // "ZIPE"
        }
    };

    class ZipAppender
    {
    public:
        static bool isZipFile(const std::string& filepath)
        {
            std::ifstream file(filepath, std::ios::binary);
            if (!file.is_open()) return false;

            uint16_t signature;
            file.read(reinterpret_cast<char*>(&signature), 2);
            file.close();
            return signature == 0x4b50; // "PK" in little-endian
        }

        static std::string getExecutablePath()
        {
            char buffer[MAX_PATH];
            GetModuleFileNameA(NULL, buffer, MAX_PATH);
            return std::string(buffer);
        }

        static std::string generateOutputName(const std::string& exePath, const std::string& zipPath)
        {
            std::filesystem::path exeFile(exePath);
            std::filesystem::path zipFile(zipPath);

            std::string baseName = exeFile.stem().string();
            std::string zipName = zipFile.stem().string();
            std::string extension = exeFile.extension().string();

            std::filesystem::path outputPath = exeFile.parent_path() / (baseName + "_with_" + zipName + extension);
            return outputPath.string();
        }

        static bool appendZipToExe(const std::string& exePath, const std::string& zipPath, const std::string& outputPath)
        {
            try
            {
                // Copy the executable
                std::filesystem::copy_file(exePath, outputPath, std::filesystem::copy_options::overwrite_existing);
                std::cout << "Debug: Copied executable to: " << outputPath << std::endl;

                // Open output file for appending
                std::ofstream outFile(outputPath, std::ios::binary | std::ios::app);
                if (!outFile.is_open())
                {
                    return false;
                }

                // Open ZIP file
                std::ifstream zipFile(zipPath, std::ios::binary);
                if (!zipFile.is_open())
                {
                    outFile.close();
                    return false;
                }

                // Get ZIP size
                zipFile.seekg(0, std::ios::end);
                uint64_t zipSize = static_cast<uint64_t>(zipFile.tellg());
                zipFile.seekg(0, std::ios::beg);

                // Get ZIP name
                std::filesystem::path zipFilePath(zipPath);
                std::string zipName = zipFilePath.filename().string();

                // Validate ZIP name
                if (zipName.empty() || zipName == "." || zipName == "..")
                {
                    zipFile.close();
                    outFile.close();
                    return false;
                }

                uint32_t nameLength = static_cast<uint32_t>(zipName.length());
                if (nameLength == 0 || nameLength > 1024)
                {
                    zipFile.close();
                    outFile.close();
                    return false;
                }

                // Copy ZIP data
                const size_t bufferSize = 1024 * 1024; // 1MB buffer
                std::vector<char> buffer(bufferSize);

                while (zipFile.good())
                {
                    zipFile.read(buffer.data(), bufferSize);
                    std::streamsize bytesRead = zipFile.gcount();
                    if (bytesRead > 0)
                    {
                        outFile.write(buffer.data(), bytesRead);
                        if (!outFile.good())
                        {
                            zipFile.close();
                            outFile.close();
                            return false;
                        }
                    }
                }

                // Check for read errors
                if (zipFile.bad())
                {
                    zipFile.close();
                    outFile.close();
                    return false;
                }

                // First write the name
                outFile.write(zipName.c_str(), nameLength);
                if (!outFile.good())
                {
                    zipFile.close();
                    outFile.close();
                    return false;
                }

                // Then write the name length
                outFile.write(reinterpret_cast<const char*>(&nameLength), sizeof(nameLength));
                if (!outFile.good())
                {
                    zipFile.close();
                    outFile.close();
                    return false;
                }

                // Then write the ZIP size
                outFile.write(reinterpret_cast<const char*>(&zipSize), sizeof(zipSize));
                if (!outFile.good())
                {
                    zipFile.close();
                    outFile.close();
                    return false;
                }

                // Finally write the magic marker
                uint32_t magic = 0x5A495045; // "ZIPE"
                outFile.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
                if (!outFile.good())
                {
                    zipFile.close();
                    outFile.close();
                    return false;
                }

                outFile.close();
                zipFile.close();

                return true;

            }
            catch (const std::exception&)
            {
                return false;
            }
        }

        static bool hasLastAppendedZip(const std::string& exePath)
        {
            std::ifstream file(exePath, std::ios::binary);
            if (!file.is_open()) return false;

            file.seekg(0, std::ios::end);
            std::streampos fileSize = file.tellg();

            // Minimum metadata: magic(4) + zipsize(8) + namelength_field(4) + name(1 char) = 17
            if (fileSize < 17)
            {
                file.close();
                return false;
            }

            file.seekg(-static_cast<std::streamoff>(sizeof(uint32_t)), std::ios::end);
            uint32_t magic;
            if (!file.read(reinterpret_cast<char*>(&magic), sizeof(uint32_t)))
            {
                file.close();
                return false;
            }
            file.close();
            return magic == 0x5A495045; // "ZIPE"
        }
    };

    // Helper to browse for a folder using IFileOpenDialog
    std::wstring BrowseForFolder(HWND hwndOwner)
    {
        std::wstring folderPath;
        // Ensure COM is initialized for this thread, as IFileOpenDialog is a COM object.
        // CoInitializeEx should be balanced with CoUninitialize.
        HRESULT hrCoInit = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

        IFileOpenDialog* pfd = NULL;
        if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd))))
        {
            DWORD dwOptions;
            if (SUCCEEDED(pfd->GetOptions(&dwOptions)))
            {
                // FOS_PICKFOLDERS to select folders, FOS_FORCEFILESYSTEM to ensure it's a file system path
                pfd->SetOptions(dwOptions | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR);
            }
            pfd->SetTitle(L"Select Installation Folder");

            if (SUCCEEDED(pfd->Show(hwndOwner)))
            {
                IShellItem* psi;
                if (SUCCEEDED(pfd->GetResult(&psi)))
                {
                    PWSTR pszPath = nullptr;
                    if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &pszPath)))
                    {
                        folderPath = pszPath;
                        CoTaskMemFree(pszPath);
                    }
                    psi->Release();
                }
            }
            pfd->Release();
        }

        // Uninitialize COM if it was initialized by this call.
        if (SUCCEEDED(hrCoInit))
        { // Only uninitialize if CoInitializeEx succeeded.
            CoUninitialize();
        }
        return folderPath;
    }

    // State for path selection dialog to handle dynamic updates
    struct PathSelectionDialogState
    {
        std::vector<std::wstring> predefinedPaths;
        std::vector<std::wstring> customPaths; // Paths added via "Browse"
        std::wstring selectedPath;
        bool browseActionTaken = false; // Flag to indicate the dialog should be reshown after browse
        HWND currentDialogHwnd = NULL; // HWND of the currently displayed path dialog
    };

    // Callback for the path selection dialog
    HRESULT CALLBACK PathSelectionDialogCallbackProc(HWND hwnd, UINT uNotification, WPARAM wParam, LPARAM lParam, LONG_PTR dwRefData)
    {
        PathSelectionDialogState* state = reinterpret_cast<PathSelectionDialogState*>(dwRefData);
        if (!state) return E_FAIL;

        switch (uNotification)
        {
        case TDN_DIALOG_CONSTRUCTED:
            state->currentDialogHwnd = hwnd; // Store the HWND of this dialog instance
            break;
        case TDN_BUTTON_CLICKED:
        {
            int buttonID = static_cast<int>(wParam);
            size_t predefinedCount = state->predefinedPaths.size();
            size_t customCount = state->customPaths.size();
            int baseButtonId = 1001; // Must match the starting ID used when creating buttons

            if (buttonID >= baseButtonId && buttonID < baseButtonId + predefinedCount)
            {
                // Predefined path selected
                state->selectedPath = state->predefinedPaths[buttonID - baseButtonId];
                state->browseActionTaken = false;
                return S_OK; // Close dialog, path is selected
            }
            else if (buttonID >= baseButtonId + predefinedCount && buttonID < baseButtonId + predefinedCount + customCount)
            {
                // Custom path selected
                state->selectedPath = state->customPaths[buttonID - (baseButtonId + predefinedCount)];
                state->browseActionTaken = false;
                return S_OK; // Close dialog, path is selected
            }
            else if (buttonID == baseButtonId + predefinedCount + customCount)
            {
                // "Browse..." button selected
                std::wstring newPathStr = BrowseForFolder(hwnd); // Use current dialog's HWND as parent
                if (!newPathStr.empty())
                {
                    bool pathExists = false;
                    std::filesystem::path newPath(newPathStr);

                    // Check against predefined paths
                    for (const auto& p : state->predefinedPaths)
                    {
                        try
                        {
                            if (std::filesystem::equivalent(newPath, std::filesystem::path(p)))
                            {
                                pathExists = true;
                                break;
                            }
                        }
                        catch (const std::filesystem::filesystem_error&)
                        {
                            if (newPath.lexically_normal() == std::filesystem::path(p).lexically_normal())
                            {
                                pathExists = true;
                                break;
                            }
                        }
                    }

                    // Check against custom paths if not already found
                    if (!pathExists)
                    {
                        for (const auto& p : state->customPaths)
                        {
                            try
                            {
                                if (std::filesystem::equivalent(newPath, std::filesystem::path(p)))
                                {
                                    pathExists = true;
                                    break;
                                }
                            }
                            catch (const std::filesystem::filesystem_error&)
                            {
                                if (newPath.lexically_normal() == std::filesystem::path(p).lexically_normal())
                                {
                                    pathExists = true;
                                    break;
                                }
                            }
                        }
                    }

                    if (!pathExists)
                    {
                        // Ensure the new path has a trailing platform-specific slash before adding it.
                        if (newPathStr.back() != std::filesystem::path::preferred_separator)
                        {
                            newPathStr += std::filesystem::path::preferred_separator;
                        }
                        state->customPaths.push_back(newPathStr);
                    }
                    else
                    {
                        // Path already exists, do nothing and keep the dialog open for another selection.
                        state->browseActionTaken = false;
                        state->selectedPath.clear();
                        return S_FALSE; // Returning S_FALSE prevents the dialog from closing.
                    }
                }
                else
                {
                    // User cancelled the browse dialog.
                    state->browseActionTaken = false;
                    state->selectedPath.clear();
                    return S_FALSE; // Keep the main selection dialog open.
                }

                state->browseActionTaken = true; // Signal to re-show the dialog with the new custom path.
                state->selectedPath.clear();   // Clear any previous selection.
                return S_OK; // Close current dialog; the loop in ShowPathSelectionDialog will re-open it.
            }
            else if (buttonID == IDCANCEL)
            {
                state->selectedPath.clear();
                state->browseActionTaken = false;
                return S_OK; // Close dialog, user cancelled.
            }
            return S_FALSE; // Keep dialog open for unhandled cases.
        }
        case TDN_HYPERLINK_CLICKED:
        {
            ShellExecuteW(hwnd, L"open", (LPCWSTR)lParam, NULL, NULL, SW_SHOW);
            break;
        }
        default:
            break;
        }
        return S_OK;
    }

    // Shows the path selection dialog, potentially multiple times if "Browse" is used
    std::wstring ShowPathSelectionDialog(HWND hwndParent, PathSelectionDialogState& state)
    {
        state.selectedPath.clear(); // Ensure it's clear at the start
        std::wstring WindowTitle = L"Installer";
        std::wstring MainInstruction = L"Select Installation Path";
        std::wstring Content = L"Choose where to install the mod:";
        std::wstring Footer = L"";
        HICON icon = NULL;

        auto& info = *muGetInfoPtr();
        if (!info.empty())
        {
            auto& mui = info.begin()->second;

            if (!mui.muInstallerWindowTitle.empty())
                WindowTitle = toWString(mui.muInstallerWindowTitle);

            if (!mui.muInstallerMainInstruction.empty())
                MainInstruction = toWString(mui.muInstallerMainInstruction);

            if (!mui.muInstallerContent.empty())
                Content = toWString(mui.muInstallerContent);

            if (!mui.muInstallerFooter.empty())
                Footer = toWString(mui.muInstallerFooter);

            icon = mui.muInstallerIcon;
        }

        while (true)
        {
            state.browseActionTaken = false; // Reset for this iteration

            std::vector<std::wstring> buttonTextStrings; // Store the actual string data
            buttonTextStrings.reserve(state.predefinedPaths.size() + state.customPaths.size() + 1); // Pre-allocate

            std::vector<TASKDIALOG_BUTTON> buttons;
            int currentButtonId = 1001; // Start IDs for path buttons

            for (const auto& path : state.predefinedPaths)
            {
                buttonTextStrings.push_back(path);
                buttons.push_back({ currentButtonId++, buttonTextStrings.back().c_str() });
            }
            for (const auto& path : state.customPaths)
            {
                buttonTextStrings.push_back(path);
                buttons.push_back({ currentButtonId++, buttonTextStrings.back().c_str() });
            }
            // For the "Browse" button, a string literal is fine as it has static storage duration.
            buttons.push_back({ currentButtonId++, L"Browse for another folder..." });

            TASKDIALOGCONFIG tdc = { sizeof(TASKDIALOGCONFIG) };
            tdc.hwndParent = hwndParent; // Use the initial parent HWND
            tdc.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_USE_COMMAND_LINKS | TDF_CAN_BE_MINIMIZED | TDF_SIZE_TO_CONTENT | TDF_ENABLE_HYPERLINKS;
            tdc.pszWindowTitle = WindowTitle.c_str();
            tdc.pszMainInstruction = MainInstruction.c_str();
            tdc.pszContent = Content.c_str();
            tdc.pszFooter = Footer.c_str();
            tdc.pButtons = buttons.data();
            tdc.cButtons = static_cast<UINT>(buttons.size());
            tdc.pfCallback = PathSelectionDialogCallbackProc;
            tdc.lpCallbackData = reinterpret_cast<LONG_PTR>(&state);
            if (icon != NULL)
            {
                tdc.dwFlags |= TDF_USE_HICON_MAIN;
                tdc.hMainIcon = icon;
            }

            int clickedButtonId = 0; // This will receive the ID of the button that closes the dialog
            HRESULT hr = TaskDialogIndirect(&tdc, &clickedButtonId, nullptr, nullptr);

            if (SUCCEEDED(hr))
            {
                if (state.browseActionTaken)
                {
                    // If browse was clicked, the callback set the flag. Loop to show dialog again.
                    continue;
                }
                // If not browse, selectedPath is either set (by path button) or empty (if Cancel was clicked)
                return state.selectedPath;
            }
            else
            {
                // Dialog creation failed or was closed unexpectedly
                return L""; // Indicate failure or cancellation
            }
        }
    }

    bool PerformOnlineInstallation(HWND hwndParent, const std::wstring& installPath)
    {
        // Get update URL from module info
        std::string updateUrl = "";
        std::wstring WindowTitle = L"Installing";
        std::wstring MainInstruction = L"Downloading and Installing...";
        std::wstring Footer = L"";
        HICON icon = NULL;

        // Get module information from muInfo
        auto& info = *muGetInfoPtr();
        if (!info.empty())
        {
            auto& mui = info.begin()->second;

            if (!mui.muInstallerWindowTitle.empty())
            {
                WindowTitle = toWString(mui.muInstallerWindowTitle);
                MainInstruction = L"Downloading and Installing " + WindowTitle + L"...";
            }

            if (!mui.muInstallerFooter.empty())
            {
                Footer = toWString(mui.muInstallerFooter);
            }

            icon = mui.muInstallerIcon;
            updateUrl = mui.muUpdateURL;
        }

        if (updateUrl.empty())
        {
            MessageBoxW(hwndParent, L"No update URL found. Please specify an update URL using muSetUpdateURL API function.",
                L"Installation Error", MB_OK | MB_ICONERROR);
            return false;
        }

        // HWND for the progress dialog, to be set in its callback
        // Making it local to this function's scope and captured by lambdas
        static HWND progressDialogHwnd = NULL;

        printToMessages(L"Preparing to download...");

        // Show progress dialog
        TASKDIALOGCONFIG tdc = { sizeof(TASKDIALOGCONFIG) };
        TASKDIALOG_BUTTON cancelButton[] = {
            { BUTTONID3, L"Cancel" } // Using BUTTONID3 as the cancel button ID
        };

        tdc.hwndParent = hwndParent;
        tdc.dwFlags = TDF_SIZE_TO_CONTENT | TDF_ENABLE_HYPERLINKS | TDF_SHOW_PROGRESS_BAR | TDF_CALLBACK_TIMER | TDF_ALLOW_DIALOG_CANCELLATION | TDF_CAN_BE_MINIMIZED;
        tdc.pButtons = cancelButton;
        tdc.cButtons = _countof(cancelButton);
        tdc.pszWindowTitle = WindowTitle.c_str();
        tdc.pszMainInstruction = MainInstruction.c_str();
        tdc.pszContent = messagesBuffer.c_str(); // Initial content
        tdc.pszFooter = Footer.c_str();
        tdc.cxWidth = 0;
        if (icon != NULL)
        {
            tdc.dwFlags |= TDF_USE_HICON_MAIN;
            tdc.hMainIcon = icon;
        }

        static std::atomic_bool bCanceledOrError = false;

        // Callback to update progress bar and handle cancellation
        // Capturing progressDialogHwnd by reference to set it.
        auto TaskDialogCallbackProc = [](HWND hwnd, UINT uNotification, WPARAM wParam, LPARAM lParam, LONG_PTR dwRefData)->HRESULT
        {
            switch (uNotification)
            {
            case TDN_DIALOG_CONSTRUCTED:
            {
                progressDialogHwnd = hwnd; // Set the HWND for this specific dialog
                // Using global DialogHwnd for SendMessage inside CPR callback is risky if multiple dialogs can exist.
                // It's better if CPR callback can get the correct HWND or use a shared context.
                // For now, assuming DialogHwnd will be set to progressDialogHwnd by this.
                DialogHwnd = hwnd;
                SendMessage(hwnd, TDM_SET_MARQUEE_PROGRESS_BAR, FALSE, 0);
                SendMessage(hwnd, TDM_SET_PROGRESS_BAR_RANGE, 0, MAKELPARAM(0, 100));
                SendMessage(hwnd, TDM_SET_PROGRESS_BAR_POS, 0, 0);
                break;
            }
            case TDN_TIMER:
            {
                SendMessage(hwnd, TDM_UPDATE_ELEMENT_TEXT, TDE_CONTENT, (LPARAM)messagesBuffer.c_str());
                break;
            }
            case TDN_BUTTON_CLICKED:
                if (wParam == BUTTONID3 || wParam == IDCANCEL)
                { // Check for our cancel button or standard cancel
                    bCanceledOrError = true; // Signal cancellation
                    // The dialog will close automatically due to this S_OK with button click.
                }
                break;
            case TDN_HYPERLINK_CLICKED:
            {
                ShellExecuteW(hwnd, L"open", (LPCWSTR)lParam, NULL, NULL, SW_SHOW);
                break;
            }
            default:
                break;
            }
            return S_OK;
        };

        tdc.pfCallback = TaskDialogCallbackProc;

        // Launch worker thread to download and process files
        std::thread worker([&updateUrl, &installPath]()
        {
            std::filesystem::path targetDir = installPath;

            static std::wstring Url = toWString(updateUrl);

            printToMessages(L"Preparing to download...");
            
            static int lastProgress = 0; // Static for CPR callback, ensure single instance context or pass userdata
            auto cprProgressCallback = [](cpr::cpr_pf_arg_t downloadTotal, cpr::cpr_pf_arg_t downloadNow, cpr::cpr_pf_arg_t uploadTotal, cpr::cpr_pf_arg_t uploadNow, intptr_t userdata) -> bool
            {
                if (bCanceledOrError.load(std::memory_order_relaxed))
                {
                    return false; // Abort download
                }
                if (downloadTotal > 0)
                {
                    int progress = static_cast<int>((downloadNow * 100) / downloadTotal);
                    if (progress != lastProgress)
                    {
                        lastProgress = progress;
                        if (progressDialogHwnd) SendMessage(progressDialogHwnd, TDM_SET_PROGRESS_BAR_POS, progress, 0);
                        std::wostringstream oss;
                        oss << L"Downloading " << L"<a href=\"" << Url << L"\">" << Url.substr(Url.find_last_of(L'/') + 1) << L"</a>" << L": " << progress << L"% ";
                        printToMessages(oss.str());
                    }
                }
                return true; // Continue download
            };

            cpr::Response r;
            if (token.empty())
            {
                r = cpr::Get(cpr::Url{ updateUrl }, cpr::ProgressCallback{ cprProgressCallback });
            }
            else
            {
                r = cpr::Get(cpr::Url{ updateUrl },
                    cpr::Header{ {"Authorization", "Bearer " + token} },
                    cpr::ProgressCallback{ cprProgressCallback });
            }
            lastProgress = 0;

            if (bCanceledOrError.load(std::memory_order_relaxed))
            { // Check immediately after download attempt
                printToMessages(L"Download cancelled.");
                if (progressDialogHwnd) SendMessage(progressDialogHwnd, TDM_CLICK_BUTTON, BUTTONID3, 0);
                return;
            }

            if (r.status_code == 200)
            {
                std::vector<uint8_t> buffer(r.text.begin(), r.text.end());
                printToMessages(L"Download complete. Processing files...");
                std::wcout << messagesBuffer << std::endl;

                try
                {
                    printToMessages(L"Extracting files...");
                    using namespace zipper;
                    std::string password = "";
                    auto& modInfo = *muGetInfoPtr();
                    if (!modInfo.empty() && !modInfo.begin()->second.muArchivePassword.empty())
                    {
                        password = modInfo.begin()->second.muArchivePassword;
                    }

                    Unzipper unzipper(buffer, password);
                    std::vector<ZipEntry> entries = unzipper.entries();
                    auto totalEntries = entries.size();
                    if (totalEntries == 0)
                        totalEntries = 1; // Avoid division by zero
                    auto processedEntries = 0;
                    int32_t nRadioBtnID = RBUTTONID1; // Default to merge

                    for (auto& entry : entries)
                    {
                        if (bCanceledOrError.load(std::memory_order_relaxed)) break;

                        processedEntries++;
                        auto progress = (processedEntries * 100) / totalEntries;
                        if (progressDialogHwnd) SendMessage(progressDialogHwnd, TDM_SET_PROGRESS_BAR_POS, progress, 0);

                        auto itemFileName = std::filesystem::path(entry.name).make_preferred();
                        printToMessages(L"Extracting\u00A0" + toWString(entry.name) + L"...");

                        auto unpackPath = (targetDir / itemFileName).make_preferred();
                        if (unpackPath.wstring().ends_with(unpackPath.preferred_separator)) continue;

                        if (CheckForFileLock(unpackPath.c_str()) == FALSE)
                        {
                            printToMessages(itemFileName.wstring() + L" is locked. Renaming...");
                            moveFileToRecycleBin(std::wstring(unpackPath.wstring() + L".deleteonnextlaunch").c_str());
                            if (MoveFileW(unpackPath.c_str(), std::wstring(unpackPath.wstring() + L".deleteonnextlaunch").c_str()))
                            {
                                printToMessages(itemFileName.wstring() + L" renamed to " + unpackPath.filename().wstring() + L".deleteonnextlaunch");
                            }
                        }
                        std::filesystem::create_directories(std::filesystem::path(unpackPath).remove_filename());

                        if (unpackPath.extension() == L".ini")
                        {
                            if (nRadioBtnID == RBUTTONID3) continue;
                            else if (nRadioBtnID == RBUTTONID2)
                            {
                                std::vector<uint8_t> vec;
                                unzipper.extractEntryToMemory(entry.name, vec);
                                if (!vec.empty())
                                {
                                    moveFileToRecycleBin(unpackPath.wstring());
                                    std::ofstream iniFile(unpackPath, std::ios::binary);
                                    iniFile.write(reinterpret_cast<const char*>(vec.data()), vec.size());
                                    iniFile.close();
                                    printToMessages(itemFileName.wstring() + L" updated.");
                                }
                                continue;
                            }
                            else
                            { // Merge (RBUTTONID1)
                                mINI::INIFile iniOld(unpackPath.string()); // mINI uses std::string
                                mINI::INIStructure iniOldStruct;
                                if (std::filesystem::exists(unpackPath))
                                {
                                    iniOld.read(iniOldStruct);
                                    if (iniOldStruct.size())
                                    {
                                        moveFileToRecycleBin(unpackPath.wstring());
                                        std::vector<uint8_t> vec;
                                        unzipper.extractEntryToMemory(entry.name, vec);
                                        std::ofstream iniFile(unpackPath, std::ios::binary);
                                        iniFile.write(reinterpret_cast<const char*>(vec.data()), vec.size());
                                        iniFile.close();

                                        mINI::INIFile iniNew(unpackPath.string());
                                        mINI::INIStructure iniNewStruct;
                                        iniNew.read(iniNewStruct);
                                        for (const auto& it_ini : iniOldStruct)
                                        { // Renamed 'it'
                                            auto const& section = std::get<0>(it_ini);
                                            auto const& collection = std::get<1>(it_ini);
                                            for (auto const& it2 : collection)
                                            {
                                                auto const& key = std::get<0>(it2);
                                                if (iniOldStruct.has(section) && iniOldStruct[section].has(key))
                                                    iniNewStruct[section][key] = iniOldStruct[section][key];
                                            }
                                        }
                                        iniNew.write(iniNewStruct, true);
                                        printToMessages(itemFileName.wstring() + L" merged.");
                                        continue;
                                    }
                                }
                                // If old INI doesn't exist or is empty, just extract new one
                                std::vector<uint8_t> vec;
                                unzipper.extractEntryToMemory(entry.name, vec);
                                if (!vec.empty())
                                {
                                    std::ofstream iniFile(unpackPath, std::ios::binary);
                                    iniFile.write(reinterpret_cast<const char*>(vec.data()), vec.size());
                                    iniFile.close();
                                    printToMessages(itemFileName.wstring() + L" installed (new).");
                                }
                                continue;
                            }
                        }

                        moveFileToRecycleBin(unpackPath.c_str());
                        std::vector<uint8_t> fileData;
                        unzipper.extractEntryToMemory(entry.name, fileData);
                        std::ofstream outputFileStream(unpackPath, std::ios::binary); // Renamed
                        outputFileStream.write(reinterpret_cast<const char*>(fileData.data()), fileData.size());
                        outputFileStream.close();
                        printToMessages(itemFileName.wstring() + L" installed.");
                    }
                    unzipper.close();
                }
                catch (const std::exception& e)
                {
                    std::wostringstream err;
                    err << L"Error extracting: " << toWString(e.what());
                    printToMessages(err.str());
                    bCanceledOrError = true;
                }
            }
            else
            { // Download failed
                std::wostringstream err;
                err << L"Download failed. Status: " << r.status_code;
                printToMessages(err.str());
                bCanceledOrError = true;
            }

            if (bCanceledOrError.load(std::memory_order_relaxed))
            {
                if (progressDialogHwnd)
                    SendMessage(progressDialogHwnd, TDM_CLICK_BUTTON, BUTTONID3, 0); // Close dialog on error/cancel
            }
            else
            {
                printToMessages(L"Installation complete!");
                if (progressDialogHwnd)
                    SendMessage(progressDialogHwnd, TDM_CLICK_BUTTON, TDCBF_OK_BUTTON, 0); // Close dialog on success
            }
        });

        int resultButtonId = 0;
        TaskDialogIndirect(&tdc, &resultButtonId, NULL, NULL);

        if (worker.joinable())
        {
            worker.join();
        }

        if (bCanceledOrError.load(std::memory_order_relaxed) && resultButtonId != BUTTONID3 && resultButtonId != IDCANCEL)
        {
            return false;
        }
        else if (resultButtonId == BUTTONID3 || resultButtonId == IDCANCEL)
        {
            return false;
        }

        return true;
    }

    bool PerformOfflineInstallation(HWND hwndParent, const std::wstring& installPath)
    {
        std::string exePath = ZipAppender::getExecutablePath();
        auto embeddedZips = EmbeddedZipReader::extractZipsFromExe(exePath);
        std::wstring WindowTitle = L"Installing";
        std::wstring MainInstruction = L"Installing...";
        std::wstring Footer = L"";
        HICON icon = NULL;

        auto& info = *muGetInfoPtr();
        if (!info.empty())
        {
            auto& mui = info.begin()->second;

            if (!mui.muInstallerWindowTitle.empty())
            {
                WindowTitle = toWString(mui.muInstallerWindowTitle);
                MainInstruction = L"Installing " + WindowTitle + L"...";
            }

            if (!mui.muInstallerFooter.empty())
            {
                Footer = toWString(mui.muInstallerFooter);
            }

            icon = mui.muInstallerIcon;
        }

        if (embeddedZips.empty())
        {
            MessageBoxW(hwndParent, L"No installation packages found in the executable.",
                L"Installation Error", MB_OK | MB_ICONERROR);
            return false;
        }

        static HWND progressDialogHwnd = NULL;
        printToMessages(L"Preparing to extract...");

        TASKDIALOGCONFIG tdc = { sizeof(TASKDIALOGCONFIG) };
        TASKDIALOG_BUTTON cancelButton[] = {
            { BUTTONID3, L"Cancel" }
        };

        tdc.hwndParent = hwndParent;
        tdc.dwFlags = TDF_SIZE_TO_CONTENT | TDF_ENABLE_HYPERLINKS | TDF_SHOW_PROGRESS_BAR | TDF_CALLBACK_TIMER | TDF_ALLOW_DIALOG_CANCELLATION | TDF_CAN_BE_MINIMIZED;
        tdc.pButtons = cancelButton;
        tdc.cButtons = _countof(cancelButton);
        tdc.pszWindowTitle = WindowTitle.c_str();
        tdc.pszMainInstruction = MainInstruction.c_str();
        tdc.pszContent = messagesBuffer.c_str();
        tdc.pszFooter = Footer.c_str();
        tdc.cxWidth = 0;
        if (icon != NULL)
        {
            tdc.dwFlags |= TDF_USE_HICON_MAIN;
            tdc.hMainIcon = icon;
        }

        static std::atomic_bool bCanceledOrError = false;

        auto TaskDialogCallbackProc = [](HWND hwnd, UINT uNotification, WPARAM wParam, LPARAM lParam, LONG_PTR dwRefData)->HRESULT
        {
            switch (uNotification)
            {
            case TDN_DIALOG_CONSTRUCTED:
            {
                progressDialogHwnd = hwnd;
                DialogHwnd = hwnd; // For global access if needed, though progressDialogHwnd is preferred
                SendMessage(hwnd, TDM_SET_MARQUEE_PROGRESS_BAR, FALSE, 0);
                SendMessage(hwnd, TDM_SET_PROGRESS_BAR_RANGE, 0, MAKELPARAM(0, 100));
                SendMessage(hwnd, TDM_SET_PROGRESS_BAR_POS, 0, 0);
                break;
            }
            case TDN_TIMER:
            {
                SendMessage(hwnd, TDM_UPDATE_ELEMENT_TEXT, TDE_CONTENT, (LPARAM)messagesBuffer.c_str());
                break;
            }
            case TDN_BUTTON_CLICKED:
                if (wParam == BUTTONID3 || wParam == IDCANCEL)
                {
                    bCanceledOrError = true;
                }
                break;
            default:
                break;
            }
            return S_OK;
        };

        tdc.pfCallback = TaskDialogCallbackProc;

        std::thread worker([&embeddedZips, &installPath]()
        {
            std::filesystem::path targetDir = installPath;
            int totalFiles = 0;
            int processedFiles = 0;

            for (const auto& zip : embeddedZips)
            {
                if (bCanceledOrError.load(std::memory_order_relaxed)) break;
                printToMessages(L"Analyzing " + toWString(zip.name) + L"...");

                try
                {
                    std::istream* zipStream = zip.stream.get();
                    if (!zipStream || !zipStream->good())
                    {
                        printToMessages(L"Error: Stream for " + toWString(zip.name) + L" is invalid.");
                        std::this_thread::sleep_for(std::chrono::seconds(1)); // Brief pause
                        continue;
                    }
                    zipStream->clear(); // Clear any error flags
                    zipStream->seekg(0, std::ios::beg); // Rewind stream

                    zipper::Unzipper unzipper(*zipStream);
                    totalFiles += int(unzipper.entries().size());
                    unzipper.close(); // Close unzipper, stream remains managed by EmbeddedZip
                }
                catch (const std::exception& e)
                {
                    std::wostringstream err;
                    err << L"Error analyzing " << toWString(zip.name) << L": " << toWString(e.what());
                    printToMessages(err.str());
                    std::this_thread::sleep_for(std::chrono::seconds(2));
                }
            }
            if (totalFiles == 0 && !embeddedZips.empty())
            { // If analysis failed for all zips but zips were present
                printToMessages(L"Error: Could not analyze any installation packages.");
                bCanceledOrError = true;
            }

            for (const auto& zip : embeddedZips)
            {
                if (bCanceledOrError.load(std::memory_order_relaxed)) break;

                printToMessages(L"Processing " + toWString(zip.name) + L"...");

                try
                {
                    std::istream* zipStream = zip.stream.get();
                    if (!zipStream || !zipStream->good())
                    {
                        printToMessages(L"Error: Stream for " + toWString(zip.name) + L" is invalid for processing.");
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                        continue;
                    }
                    zipStream->clear();
                    zipStream->seekg(0, std::ios::beg);

                    zipper::Unzipper unzipper(*zipStream);
                    auto entries = unzipper.entries();
                    int32_t nRadioBtnID = RBUTTONID1; // Default to merge

                    for (auto& entry : entries)
                    {
                        if (bCanceledOrError.load(std::memory_order_relaxed)) break;

                        processedFiles++;
                        if (totalFiles > 0)
                        { // Avoid division by zero if totalFiles is 0
                            int progress = (processedFiles * 100) / totalFiles;
                            if (progressDialogHwnd) SendMessage(progressDialogHwnd, TDM_SET_PROGRESS_BAR_POS, progress, 0);
                        }

                        auto itemFileName = std::filesystem::path(entry.name).make_preferred();
                        printToMessages(L"Extracting\u00A0" + toWString(entry.name) + L"..."); 

                        auto unpackPath = (targetDir / itemFileName).make_preferred();
                        if (unpackPath.wstring().ends_with(unpackPath.preferred_separator)) continue;

                        if (CheckForFileLock(unpackPath.c_str()) == FALSE)
                        {
                            printToMessages(itemFileName.wstring() + L" is locked. Renaming...");
                            moveFileToRecycleBin(std::wstring(unpackPath.wstring() + L".deleteonnextlaunch").c_str());
                            if (MoveFileW(unpackPath.c_str(), std::wstring(unpackPath.wstring() + L".deleteonnextlaunch").c_str()))
                            {
                                printToMessages(itemFileName.wstring() + L" renamed to " + unpackPath.filename().wstring() + L".deleteonnextlaunch");
                            }
                        }
                        std::filesystem::create_directories(std::filesystem::path(unpackPath).remove_filename());

                        if (unpackPath.extension() == L".ini")
                        {
                            if (nRadioBtnID == RBUTTONID3) continue;
                            else if (nRadioBtnID == RBUTTONID2)
                            {
                                std::vector<uint8_t> vec;
                                unzipper.extractEntryToMemory(entry.name, vec);
                                if (!vec.empty())
                                {
                                    moveFileToRecycleBin(unpackPath.wstring());
                                    std::ofstream iniFile(unpackPath, std::ios::binary);
                                    iniFile.write(reinterpret_cast<const char*>(vec.data()), vec.size());
                                    iniFile.close();
                                    printToMessages(itemFileName.wstring() + L" updated.");
                                }
                                continue;
                            }
                            else
                            { // Merge (RBUTTONID1)
                                mINI::INIFile iniOld(unpackPath.string());
                                mINI::INIStructure iniOldStruct;
                                if (std::filesystem::exists(unpackPath))
                                {
                                    iniOld.read(iniOldStruct);
                                    if (iniOldStruct.size())
                                    {
                                        moveFileToRecycleBin(unpackPath.wstring());
                                        std::vector<uint8_t> vec;
                                        unzipper.extractEntryToMemory(entry.name, vec);
                                        std::ofstream iniFile(unpackPath, std::ios::binary);
                                        iniFile.write(reinterpret_cast<const char*>(vec.data()), vec.size());
                                        iniFile.close();

                                        mINI::INIFile iniNew(unpackPath.string());
                                        mINI::INIStructure iniNewStruct;
                                        iniNew.read(iniNewStruct);
                                        for (const auto& it_ini : iniOldStruct)
                                        { // Renamed 'it'
                                            auto const& section = std::get<0>(it_ini);
                                            auto const& collection = std::get<1>(it_ini);
                                            for (auto const& it2 : collection)
                                            {
                                                auto const& key = std::get<0>(it2);
                                                if (iniOldStruct.has(section) && iniOldStruct[section].has(key))
                                                    iniNewStruct[section][key] = iniOldStruct[section][key];
                                            }
                                        }
                                        iniNew.write(iniNewStruct, true);
                                        printToMessages(itemFileName.wstring() + L" merged.");
                                        continue;
                                    }
                                }
                                // If old INI doesn't exist or is empty, just extract new one
                                std::vector<uint8_t> vec;
                                unzipper.extractEntryToMemory(entry.name, vec);
                                if (!vec.empty())
                                {
                                    std::ofstream iniFile(unpackPath, std::ios::binary);
                                    iniFile.write(reinterpret_cast<const char*>(vec.data()), vec.size());
                                    iniFile.close();
                                    printToMessages(itemFileName.wstring() + L" installed (new).");
                                }
                                continue;
                            }
                        }

                        moveFileToRecycleBin(unpackPath.c_str());
                        std::vector<uint8_t> fileData;
                        unzipper.extractEntryToMemory(entry.name, fileData);
                        std::ofstream outputFileStream(unpackPath, std::ios::binary); // Renamed
                        outputFileStream.write(reinterpret_cast<const char*>(fileData.data()), fileData.size());
                        outputFileStream.close();
                        printToMessages(itemFileName.wstring() + L" installed.");
                    }
                    unzipper.close();
                }
                catch (const std::exception& e)
                {
                    std::wostringstream err;
                    err << L"Error processing " << toWString(zip.name) << L": " << toWString(e.what());
                    printToMessages(err.str());
                    bCanceledOrError = true; // Set error flag
                    // No need to sleep here, loop will check bCanceledOrError
                }
                if (bCanceledOrError.load(std::memory_order_relaxed)) break; // Check after each zip processing
            }

            if (bCanceledOrError.load(std::memory_order_relaxed))
            {
                if (progressDialogHwnd) SendMessage(progressDialogHwnd, TDM_CLICK_BUTTON, BUTTONID3, 0);
            }
            else
            {
                printToMessages(L"Installation complete!");
                if (progressDialogHwnd) SendMessage(progressDialogHwnd, TDM_CLICK_BUTTON, TDCBF_OK_BUTTON, 0);
            }
        });

        int resultButtonId = 0;
        TaskDialogIndirect(&tdc, &resultButtonId, NULL, NULL);

        if (worker.joinable())
        {
            worker.join();
        }

        if (bCanceledOrError.load(std::memory_order_relaxed) && resultButtonId != BUTTONID3 && resultButtonId != IDCANCEL)
        {
            return false;
        }
        else if (resultButtonId == BUTTONID3 || resultButtonId == IDCANCEL)
        {
            return false;
        }

        return true;
    }

    // Shows the final completion dialog
    void ShowInstallationFinishedDialog(HWND hwndParent)
    {
        std::wstring WindowTitle = L"Installer";
        std::wstring MainInstruction = L"Installation Complete";
        std::wstring Content = L"Installation finished successfully.";
        HICON icon = NULL;

        auto& info = *muGetInfoPtr();
        if (!info.empty())
        {
            auto& mui = info.begin()->second;

            if (!mui.muInstallerWindowTitle.empty())
                WindowTitle = toWString(mui.muInstallerWindowTitle);

            icon = mui.muInstallerIcon;
        }

        TASKDIALOGCONFIG tdc = { sizeof(TASKDIALOGCONFIG) };
        tdc.hwndParent = hwndParent;
        tdc.pszWindowTitle = WindowTitle.c_str();
        tdc.pszMainInstruction = MainInstruction.c_str();
        tdc.pszContent = Content.c_str();
        tdc.dwCommonButtons = TDCBF_CLOSE_BUTTON;
        tdc.nDefaultButton = TDCBF_CLOSE_BUTTON; // Make Close the default
        if (icon != NULL)
        {
            tdc.dwFlags |= TDF_USE_HICON_MAIN;
            tdc.hMainIcon = icon;
        }

        TaskDialogIndirect(&tdc, nullptr, nullptr, nullptr);
    }

    // Shows the failed installation dialog
    void ShowInstallationFailedDialog(HWND hwndParent)
    {
        std::wstring WindowTitle = L"Installer";
        std::wstring MainInstruction = L"Installation Failed";
        std::wstring Content = L"An installation was canceled or error occurred while preparing the installation. Try running this application again.";
        HICON icon = NULL;

        auto& info = *muGetInfoPtr();
        if (!info.empty())
        {
            auto& mui = info.begin()->second;

            if (!mui.muInstallerWindowTitle.empty())
                WindowTitle = toWString(mui.muInstallerWindowTitle);

            icon = mui.muInstallerIcon;
        }

        TASKDIALOGCONFIG tdc = { sizeof(TASKDIALOGCONFIG) };
        tdc.hwndParent = hwndParent;
        tdc.pszWindowTitle = WindowTitle.c_str();
        tdc.pszMainInstruction = MainInstruction.c_str();
        tdc.pszContent = Content.c_str();
        tdc.dwCommonButtons = TDCBF_CLOSE_BUTTON;
        tdc.nDefaultButton = TDCBF_CLOSE_BUTTON; // Make Close the default
        if (icon != NULL)
        {
            tdc.dwFlags |= TDF_USE_HICON_MAIN;
            tdc.hMainIcon = icon;
        }

        TaskDialogIndirect(&tdc, nullptr, nullptr, nullptr);
    }
}

bool muAppendZipFile(int argc, char* argv[])
{
    using namespace installer;

    if (argc < 2)
    {
        return false;
    }

    std::string zipPath = argv[1];

    if (!std::filesystem::exists(zipPath))
    {
        return false;
    }

    if (!ZipAppender::isZipFile(zipPath))
    {
        return false;
    }

    std::string exePath = ZipAppender::getExecutablePath();
    std::string outputPath = ZipAppender::generateOutputName(exePath, zipPath);

    return ZipAppender::appendZipToExe(exePath, zipPath, outputPath);
}

void muSetInstallerIcon(HMODULE hModule, HICON icon)
{
    std::lock_guard<std::mutex> lock(muMutex);
    auto& info = *muGetInfoPtr();
    info[hModule].muInstallerIcon = icon;
}

void muSetInstallerWindowTitle(HMODULE hModule, const char* title)
{
    std::lock_guard<std::mutex> lock(muMutex);
    auto& info = *muGetInfoPtr();
    info[hModule].muInstallerWindowTitle = title ? title : "";
}

void muSetInstallerMainInstruction(HMODULE hModule, const char* maininstr)
{
    std::lock_guard<std::mutex> lock(muMutex);
    auto& info = *muGetInfoPtr();
    info[hModule].muInstallerMainInstruction = maininstr ? maininstr : "";
}

void muSetInstallerContent(HMODULE hModule, const char* content)
{
    std::lock_guard<std::mutex> lock(muMutex);
    auto& info = *muGetInfoPtr();
    info[hModule].muInstallerContent = content ? content : "";
}

void muSetInstallerFooter(HMODULE hModule, const char* footer)
{
    std::lock_guard<std::mutex> lock(muMutex);
    auto& info = *muGetInfoPtr();
    info[hModule].muInstallerFooter = footer ? footer : "";
}

void muSetRGLAppID(HMODULE hModule, const char* id, const char* subfolder)
{
    std::lock_guard<std::mutex> lock(muMutex);
    auto& info = *muGetInfoPtr();
    info[hModule].muRglAppID = id ? id : "";
    info[hModule].muRglAppSubfolder = subfolder ? subfolder : "";
}

void muSetSteamAppID(HMODULE hModule, const char* id, const char* subfolder)
{
    std::lock_guard<std::mutex> lock(muMutex);
    auto& info = *muGetInfoPtr();
    info[hModule].muSteamAppID = id ? id : "";
    info[hModule].muSteamAppSubfolder = subfolder ? subfolder : "";
}

void muInitInstaller()
{
    using namespace installer;

    std::lock_guard<std::mutex> lock(muMutex);
    if (::OpenMutexW(MUTEX_ALL_ACCESS, FALSE, mtxNameAsi))
    {
        if (muMutexHandle)
        {
            delete muInfoPtr;
            muInfoPtr = nullptr;
            CloseHandle(muMutexHandle);
            muMutexHandle = NULL;
        }
        return;
    }
    else if (!muMutexHandle)
    {
        return;
    }
    else
    {
        HWND parentDialogHwnd = NULL;
        PathSelectionDialogState pathState;

        auto& info = *muGetInfoPtr();

        if (!info.empty())
        {
            // Helper to add a path only if it's not already present in a semantically equivalent form.
            auto addUniquePredefinedPath = [](PathSelectionDialogState& state, const std::string& newPathStr)
            {
                if (newPathStr.empty())
                {
                    return;
                }

                std::filesystem::path newPath(newPathStr);

                for (const auto& existingPathWStr : state.predefinedPaths)
                {
                    try
                    {
                        // std::filesystem::equivalent is the most reliable way to check if two paths
                        // point to the same file system object. It handles trailing slashes, symlinks, etc.
                        if (std::filesystem::equivalent(newPath, std::filesystem::path(existingPathWStr)))
                        {
                            return; // Path is already in the list.
                        }
                    }
                    catch (const std::filesystem::filesystem_error&)
                    {
                        // This can throw if a path doesn't exist.
                        // As a fallback, we can compare the paths after normalizing them.
                        auto p1 = newPath.lexically_normal();
                        auto p2 = std::filesystem::path(existingPathWStr).lexically_normal();
                        if (p1 == p2)
                        {
                            return; // Paths are lexically equivalent.
                        }
                    }
                }
                // If no equivalent path was found, add the new one.
                std::wstring pathWithSlash = toWString(newPathStr);
                if (!pathWithSlash.empty() && pathWithSlash.back() != std::filesystem::path::preferred_separator)
                {
                    pathWithSlash += std::filesystem::path::preferred_separator;
                }
                state.predefinedPaths.push_back(pathWithSlash);
            };

            if (!info.begin()->second.muSteamAppID.empty())
            {
                auto gamePath = findSteamGame(info.begin()->second.muSteamAppID, info.begin()->second.muSteamAppSubfolder);
                addUniquePredefinedPath(pathState, gamePath);
            }

            if (!info.begin()->second.muRglAppID.empty())
            {
                auto gamePath = findRockstarGame(info.begin()->second.muRglAppID, info.begin()->second.muRglAppSubfolder);
                addUniquePredefinedPath(pathState, gamePath);
            }

            auto installPath = ShowPathSelectionDialog(parentDialogHwnd, pathState);

            if (!installPath.empty())
            {
                bool result = false;
                if (EmbeddedZipReader::hasEmbeddedZips(ZipAppender::getExecutablePath()))
                    result = PerformOfflineInstallation(parentDialogHwnd, installPath);
                else
                    result = PerformOnlineInstallation(parentDialogHwnd, installPath);

                if (result)
                    ShowInstallationFinishedDialog(parentDialogHwnd);
                else
                    ShowInstallationFailedDialog(parentDialogHwnd);
            }
        }
    }
}
#endif

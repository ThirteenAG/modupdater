#include "stdafx.h"
#include "string_funcs.h"
#include "mINI\src\mini\ini.h"
#include "ModuleList.hpp"
#include "libmodupdater.h"

constexpr auto maxContentLength = 255;
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

bool GetImageFileHeaders(std::wstring fileName, IMAGE_NT_HEADERS& headers)
{
    HANDLE fileHandle = CreateFile(
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

    HANDLE imageHandle = CreateFileMapping(
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
    if (!::GetFileSecurityW(folderName.wstring().c_str(), OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, NULL, NULL, &length) && ERROR_INSUFFICIENT_BUFFER == ::GetLastError()) {
        PSECURITY_DESCRIPTOR security = static_cast<PSECURITY_DESCRIPTOR>(::malloc(length));
        if (security && ::GetFileSecurityW(folderName.wstring().c_str(), OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, security, length, &length)) {
            HANDLE hToken = NULL;
            if (::OpenProcessToken(::GetCurrentProcess(), TOKEN_IMPERSONATE | TOKEN_QUERY | TOKEN_DUPLICATE | STANDARD_RIGHTS_READ, &hToken)) {
                HANDLE hImpersonatedToken = NULL;
                if (::DuplicateToken(hToken, SecurityImpersonation, &hImpersonatedToken)) {
                    GENERIC_MAPPING mapping = { 0xFFFFFFFF };
                    PRIVILEGE_SET privileges = { 0 };
                    DWORD grantedAccess = 0, privilegesLength = sizeof(privileges);
                    BOOL result = FALSE;

                    mapping.GenericRead = FILE_GENERIC_READ;
                    mapping.GenericWrite = FILE_GENERIC_WRITE;
                    mapping.GenericExecute = FILE_GENERIC_EXECUTE;
                    mapping.GenericAll = FILE_ALL_ACCESS;

                    ::MapGenericMask(&genericAccessRights, &mapping);
                    if (::AccessCheck(security, hImpersonatedToken, genericAccessRights, &mapping, &privileges, &privilegesLength, &grantedAccess, &result)) {
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

void FindFilesRecursively(const std::wstring &directory, std::function<void(std::wstring&, WIN32_FIND_DATAW)> callback, bool cancelRecursion = false)
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
    auto cb = [](std::wstring &s, WIN32_FIND_DATAW)
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
    messagesBuffer = L"Downloading " + wszDownloadName;
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
                oss << L"Downloading " << wszDownloadName << L": " << progress << L" % ";
                messagesBuffer = oss.str();
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
        messagesBuffer = L"Download complete.";
        std::wcout << messagesBuffer << std::endl;

        messagesBuffer = L"Processing " + wszDownloadName;
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
            messagesBuffer = wszFileName + L" was updated succesfully.";
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
        
        auto itr = std::find_if(entries.begin(), entries.end(), [&wszFileName](auto &s)
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
            itr = std::find_if(entries.begin(), entries.end(), [](auto &s)
            {
                return s.name.ends_with(".asi");
            });
        }

        if (itr != entries.end())
        {
            messagesBuffer = L"Processing " + wszDownloadName;
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
                                    messagesBuffer = itFileName.wstring() + L" was updated succesfully.";
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

                                    messagesBuffer = itFileName.wstring() + L" was updated succesfully.";
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

                    messagesBuffer = itFileName.wstring() + L" was updated succesfully.";
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
                if (messagesBuffer.length() > maxContentLength)
                    messagesBuffer.resize(maxContentLength);
                std::wstring indent(maxContentLength - messagesBuffer.length(), L' ');
                messagesBuffer += indent;
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
        for (auto &it : FilesToDownload)
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
        for (auto &it : FilesToUpdate)
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

            for (auto &it : FilesToDownload)
            {
                if (!bCanceledorError)
                    UpdateFile(downloads, it.wszFileName, it.wszFullFilePath, it.wszDownloadURL, it.wszDownloadName, it.szPassword, true, nRadioBtnID);
                else
                    break;
            }

            for (auto &it : FilesToUpdate)
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
                                    else if(machine == L"x86")
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
    if (!FileTimeToSystemTime(&ft, &st)) {
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
                try {
                    devUpdateUrl = versionInfo.substr(versionInfo.find(DEVUPDATEURL) + wcslen(DEVUPDATEURL) + sizeof(wchar_t));
                    devUpdateUrl = devUpdateUrl.substr(0, devUpdateUrl.find_first_of(L'\0'));
                }
                catch (std::out_of_range& ex) {
                    std::wcout << ex.what() << std::endl;
                }
            }

            if (versionInfo.find(UPDATEURL) != std::wstring::npos)
            {
                try {
                    updateUrl = versionInfo.substr(versionInfo.find(UPDATEURL) + wcslen(UPDATEURL) + sizeof(wchar_t));
                    updateUrl = updateUrl.substr(0, updateUrl.find_first_of(L'\0'));
                }
                catch (std::out_of_range & ex) {
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

        if (url.empty() && !devurl.empty()) {
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

    if (muMutexHandle) {
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
                     if (muMutexHandle) {
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
#endif

#include "stdafx.h"
#include "string_funcs.h"

struct FileUpdateInfo
{
    std::wstring wszFullFilePath;
    std::wstring wszFileName;
    std::wstring wszDownloadURL;
    std::wstring wszDownloadName;
    int32_t nRemoteFileUpdatedHoursAgo;
    int32_t nLocaFileUpdatedHoursAgo;
    int32_t	nFileSize;
};

CIniReader iniReader;
HWND MainHwnd, DialogHwnd;
std::wstring modulePath, processPath, selfPath;
std::wstring messagesBuffer;
std::wofstream logFile;
std::wstreambuf* outbuf;
std::string token;
bool reqElev;

#define EXTRACTSINGLEFILE "ExtractSingleFile"
#define PLACETOROOT       "PlaceToRoot"
#define IGNOREUPDATES     "IgnoreUpdates"
#define CUSTOMPATH        "CustomPath"
#define SOURCENAME        "SourceName"
#define PASSWORD          "Password"

#define UPDATEURL L"UpdateUrl"
#define UALNAME   L"Ultimate-ASI-Loader.zip"
#define BUTTONID1  1001
#define BUTTONID2  1002
#define BUTTONID3  1003
#define BUTTONID4  1004
#define BUTTONID5  1005
#define RBUTTONID1 1011
#define RBUTTONID2 1012
#define RBUTTONID3 1013

bool CanAccessFolder(LPCWSTR folderName, DWORD genericAccessRights)
{
    bool bRet = false;
    DWORD length = 0;
    if (!::GetFileSecurityW(folderName, OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, NULL, NULL, &length) && ERROR_INSUFFICIENT_BUFFER == ::GetLastError()) {
        PSECURITY_DESCRIPTOR security = static_cast<PSECURITY_DESCRIPTOR>(::malloc(length));
        if (security && ::GetFileSecurityW(folderName, OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, security, length, &length)) {
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
                        bRet = (result == TRUE);
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

void FindFilesRecursively(const std::wstring &directory, std::function<void(std::wstring, WIN32_FIND_DATAW)> callback, bool cancelRecursion = false)
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
            DeleteFileW(s.c_str());
            std::wcout << L"Deleted " << s << std::endl;
        }
    };

    if (iniReader.ReadInteger("MISC", "ScanFromRootFolder", 0) == 0)
    {
        if (modulePath != processPath.substr(0, processPath.rfind('\\') + 1))
            FindFilesRecursively(processPath.substr(0, processPath.rfind('\\') + 1), cb, true);
    }
    FindFilesRecursively(modulePath, cb);
}

void UpdateFile(std::vector<std::pair<std::wstring, std::string>>& downloads, std::wstring wszFileName, std::wstring wszFullFilePath, std::wstring wszDownloadURL, std::wstring wszDownloadName, bool bCheckboxChecked, int32_t nRadioBtnID, bool bCreateDirectories)
{
    messagesBuffer = L"Downloading " + wszDownloadName;
    std::wcout << messagesBuffer << std::endl;

    std::wstring ualName;
    if (toLowerWStr(wszDownloadName) == toLowerWStr(std::wstring(UALNAME)))
    {
        ualName = wszFileName;
        wszFileName = L"dinput8.dll";
    }

    std::string cusPath;
    auto iniData = iniReader.data.find(toString(wszFileName));
    if (iniData != iniReader.data.end())
    {
        if (iniData->second.find(CUSTOMPATH) != iniData->second.end() && bCreateDirectories) //bCreateDirectories - if downloading files, not updating
        {
            auto str = iniData->second[CUSTOMPATH];
            removeQuotesFromString(str);
            cusPath = "\\" + str + "\\";
        }
    }

    cpr::Response r;

    auto itr = std::find_if(downloads.begin(), downloads.end(), [&wszDownloadURL](std::pair<std::wstring, std::string> const& wstr)
    {
        return wstr.first == wszDownloadURL;
    });

    if (itr != downloads.end())
    {
        r.status_code = 200;
        r.text = itr->second;
    }
    else
    {
        r = cpr::Get(cpr::Url{ toString(wszDownloadURL) });
        downloads.push_back(std::make_pair(wszDownloadURL, r.text));
    }

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
                std::wcout << wszDownloadName << L" was renamed to " << fullPath + L".deleteonnextlaunch" << std::endl;
            else
                std::wcout << L"Error: " << GetLastError() << std::endl;
        }
        else
        {
            std::wcout << wszDownloadName << L" is not locked." << std::endl;
        }

        createFolder(fullPath.substr(0, fullPath.find_last_of('\\')));

        auto muArchive = fullPath + L".modupdater";

        std::ofstream outputFile(muArchive, std::ios::binary);
        outputFile.write((const char*)&buffer[0], buffer.size());
        outputFile.close();

        if (wszFileName == wszDownloadName)
        {
            messagesBuffer = wszFileName + L" was updated succesfully.";
            std::wcout << messagesBuffer << std::endl;
            return;
        }

        using namespace zipper;
        std::string passw = iniReader.ReadString(toString(wszFileName), PASSWORD, "");
        Unzipper unzipper(toString(muArchive), passw);
        std::vector<ZipEntry> entries = unzipper.entries();
        std::wstring szFilePath, szFileName;

        auto itr = std::find_if(entries.begin(), entries.end(), [&wszFileName, &szFilePath, &szFileName](auto &s)
        {
            auto s1 = s.name.substr(0, s.name.rfind('/') + 1);
            auto s2 = s.name.substr(s.name.rfind('/') + 1);

            if (s2.size() != wszFileName.size())
                return false;

            for (size_t i = 0; i < s2.size(); ++i)
            {
                if (!(::towlower(s2[i]) == ::towlower(wszFileName[i])))
                {
                    return false;
                }
            }
            szFilePath = toWString(s1);
            szFileName = toWString(s2);
            return true;
        });

        if (itr != entries.end())
        {
            messagesBuffer = L"Processing " + wszDownloadName;
            std::wcout << messagesBuffer << std::endl;

            std::vector<uint8_t> test;
            try {
                unzipper.extractEntryToMemory(entries.begin()->name, test);
            }
            catch (const std::exception& e) {
                std::wcout << e.what() << std::endl << std::endl;
                std::wcout << L"This archive will not be processed." << std::endl << std::endl;
                return;
            }
            test.clear();

            for (auto it = entries.begin(); it != entries.end(); it++)
            {
                std::wstring lowcsIt, lowcsFilePath, lowcsFileName, itFileName, lowcsItFileName;
                lowcsIt = toLowerWStr(it->name);
                lowcsFilePath = toLowerWStr(szFilePath);
                lowcsFileName = toLowerWStr(szFileName);
                itFileName = toWString(it->name);
                std::replace(itFileName.begin(), itFileName.end(), '/', '\\');
                itFileName.erase(0, szFilePath.length());
                lowcsItFileName = toLowerWStr(itFileName);

                if (!itFileName.empty() && (itFileName.back() != L'\\'))
                {
                    if (!selfPath.empty() && wszDownloadURL.find(L"ThirteenAG/modupdater") != std::wstring::npos)
                    {
                        auto selfName = toLowerWStr(selfPath.substr(selfPath.rfind('\\') + 1));
                        auto selfNameNoExt = toLowerWStr(selfName.substr(0, selfName.rfind('.') + 1));
                        if ((lowcsFileName == selfName || lowcsFileName == selfNameNoExt + L"asi" || lowcsFileName == selfNameNoExt + L"exe"))
                            continue;
                    }

                    if ((!bCheckboxChecked && lowcsIt.find(lowcsFileName) != std::wstring::npos) || (bCheckboxChecked && lowcsIt.find(lowcsFilePath) != std::wstring::npos))
                    {
                        if (wszFullFilePath.find(L"modloader\\modloader.asi") != std::wstring::npos && itFileName.find(L"modloader\\") != std::wstring::npos)
                            itFileName.erase(0, std::wstring(L"modloader\\").length());

                        std::wstring fullPath = wszFullFilePath.substr(0, wszFullFilePath.find_last_of('\\') + 1) + toWString(cusPath) + itFileName;

                        auto bExtractSingleFile = iniData != iniReader.data.end() && iniData->second.find(EXTRACTSINGLEFILE) != iniData->second.end();
                        auto bPlaceToRoot = iniData != iniReader.data.end() && iniData->second.find(PLACETOROOT) != iniData->second.end();

                        if (bPlaceToRoot || !ualName.empty())
                            fullPath = processPath.substr(0, processPath.rfind('\\') + 1) + (ualName.empty() ? itFileName : ualName);

                        if (!ualName.empty())
                            ualName.clear();

                        std::string fullPathStr = toString(fullPath);
                        if (CheckForFileLock(fullPath.c_str()) == FALSE)
                        {
                            std::wcout << itFileName << L" is locked. Renaming..." << std::endl;
                            if (MoveFileW(fullPath.c_str(), std::wstring(fullPath + L".deleteonnextlaunch").c_str()))
                                std::wcout << itFileName << L" was renamed to " << fullPath + L".deleteonnextlaunch" << std::endl;
                            else
                                std::wcout << L"Error: " << GetLastError() << std::endl;
                        }
                        else
                        {
                            std::wcout << itFileName << L" is not locked." << std::endl;
                        }

                        if (bCreateDirectories)
                            createFolder(fullPath.substr(0, fullPath.find_last_of('\\')));

                        auto pos = lowcsItFileName.find_last_of('.');
                        if (pos != std::wstring::npos)
                        {
                            auto iniName = lowcsItFileName;
                            auto iniPath = fullPath.substr(0, fullPath.find_last_of('.'));
                            auto iniEntry = it->name.substr(0, it->name.find_last_of('.'));
                            auto fileExtension = lowcsItFileName.substr(pos);
                            iniName.erase(pos);
                            iniName.append(L".ini");
                            iniPath.append(L".ini");
                            iniEntry.append(".ini");

                            if (nRadioBtnID == RBUTTONID3) //don't replace
                            {
                                if (fileExtension == L".ini")
                                    continue;
                            }
                            else
                            {
                                if (nRadioBtnID == RBUTTONID2) //replace all
                                {
                                    if (!bCheckboxChecked)
                                    {
                                        std::vector<uint8_t> vec;
                                        unzipper.extractEntryToMemory(iniEntry, vec);
                                        if (!vec.empty())
                                        {
                                            moveFileToRecycleBin(iniPath);
                                            std::ofstream iniFile(iniPath, std::ios::binary);
                                            unzipper.extractEntryToStream(iniEntry, iniFile);
                                            iniFile.close();
                                            messagesBuffer = iniName + L" was updated succesfully.";
                                            std::wcout << messagesBuffer << std::endl;
                                            vec.clear();
                                        }
                                    }
                                }
                                else //merge
                                {
                                    CIniReader iniOld(toString(iniPath));

                                    moveFileToRecycleBin(iniPath);
                                    std::ofstream iniFile(iniPath, std::ios::binary);
                                    unzipper.extractEntryToStream(iniEntry, iniFile);
                                    iniFile.close();

                                    CIniReader iniNew(iniEntry);
                                    for each (auto sec in iniOld.data)
                                    {
                                        for each (auto key in sec.second)
                                        {
                                            iniNew.data[sec.first][key.first] = iniOld.data[sec.first][key.first];
                                        }
                                    }
                                    iniNew.data.write_file(iniEntry);

                                    messagesBuffer = iniName + L" was updated succesfully.";
                                    std::wcout << messagesBuffer << std::endl;

                                    if (bCheckboxChecked && fileExtension == L".ini")
                                        continue;
                                }
                            }
                        }

                        moveFileToRecycleBin(fullPath);
                        std::ofstream outputFile(fullPath, std::ios::binary);
                        unzipper.extractEntryToStream(it->name, outputFile);
                        outputFile.close();

                        messagesBuffer = itFileName + L" was updated succesfully.";
                        std::wcout << messagesBuffer << std::endl;

                        if (bExtractSingleFile)
                        {
                            unzipper.close();
                            moveFileToRecycleBin(muArchive);
                            return;
                        }
                    }
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
        case TDN_DIALOG_CONSTRUCTED:
            DialogHwnd = hwnd;
            if (dwRefData == 1)
            {
                SendMessage(hwnd, TDM_SET_MARQUEE_PROGRESS_BAR, 1, 0);
                SendMessage(hwnd, TDM_SET_PROGRESS_BAR_MARQUEE, 1, 0);
            }
            else
            {
                if (dwRefData == 2)
                {
                    SendMessage(DialogHwnd, TDM_SET_MARQUEE_PROGRESS_BAR, FALSE, 0);
                    SendMessage(DialogHwnd, TDM_SET_PROGRESS_BAR_POS, 100, 0);
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
                messagesBuffer.resize(55);
                SendMessage(DialogHwnd, TDM_UPDATE_ELEMENT_TEXT, TDE_CONTENT, (LPARAM)messagesBuffer.c_str());
            }
            break;
        case TDN_HYPERLINK_CLICKED:
        {
            auto p = std::wstring((LPCWSTR)lParam);
            if (starts_with(toString(p).c_str(), "file:", true) || starts_with(toString(p).c_str(), "disable:", true))
            {
                if (starts_with(toString(p).c_str(), "disable:", true))
                {
                    auto strFileName = toString(p.substr(p.rfind('\\') + 1));
                    iniReader.WriteString(strFileName, IGNOREUPDATES, "");
                }

                p.erase(0, p.find(L':') + 1);
                upd.erase(std::remove_if(upd.begin(), upd.end(), [&p](const FileUpdateInfo& e) { return e.wszFullFilePath == p; }), upd.end());
                dl.erase(std::remove_if(dl.begin(), dl.end(), [&p](const FileUpdateInfo& e) { return e.wszFullFilePath == p; }), dl.end());
                auto& x = szBodyText;
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
            else if (starts_with(toString(p).c_str(), ":disableupdates", true))
            {
                MoveFileW(selfPath.c_str(), std::wstring(selfPath + L".deleteme").c_str());
                SendMessage(DialogHwnd, TDM_CLICK_BUTTON, static_cast<WPARAM>(TDCBF_CLOSE_BUTTON), 0);
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
        return FALSE;
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
    auto szCheckboxText = L"Update all downloaded files.";
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
            if (!reqElev)
                szBodyText += L" | <a href=\"disable:" + it.wszFullFilePath + L"\">" + L"Disable updates for this file" + L"</a>\n";
            szBodyText += L"\n";
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
            if (!reqElev)
                szBodyText += L" | <a href=\"disable:" + it.wszFullFilePath + L"\">" + L"Disable updates for this file" + L"</a>\n";
            szBodyText += L"\n";
            szBodyText += L"\u200C";
            nTotalUpdateSize += it.nFileSize;
        }
    }

    szBodyText += L"Do you want to download these updates?";

    auto szButton1Text = std::wstring(L"Download and install updates now\n" + (nTotalUpdateSize > 0 ? formatBytesW(nTotalUpdateSize) : L""));
    if (reqElev)
        szButton1Text = std::wstring(L"Restart this application with elevated permissions\n"
            "If you grant permission by using the User Account Control\nfeature of your operating system, the application may be\nable to complete the requested tasks.");

    std::string szURL = iniReader.ReadString("MISC", "WebUrl", "https://thirteenag.github.io/wfp");
    removeQuotesFromString(szURL);
    auto wszFooter = std::wstring(L"<a href=\"" + toWString(szURL) + L"\">" + toWString(szURL) + L"</a>");
    if (ends_with(toString(selfPath).c_str(), ".asi", false))
        wszFooter += L"\n\n<a href=\":disableupdates\">Click here if you want to cancel and do not wish to receive any updates in the future</a> ";

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
    //tdc.pszMainIcon = TD_WARNING_ICON;
    tdc.pszWindowTitle = szTitle;
    tdc.pszMainInstruction = szHeader.c_str();
    tdc.pszContent = szBodyText.c_str();
    tdc.pszVerificationText = szCheckboxText;
    tdc.pszFooter = wszFooter.c_str();
    tdc.pszFooterIcon = TD_INFORMATION_ICON;
    tdc.pfCallback = TaskDialogCallbackProc;
    tdc.lpCallbackData = 0;
    auto nClickedBtnID = -1;
    auto nRadioBtnID = -1;
    auto bCheckboxChecked = 0;
    auto hr = TaskDialogIndirect(&tdc, &nClickedBtnID, &nRadioBtnID, &bCheckboxChecked);

    if (SUCCEEDED(hr) && nClickedBtnID == BUTTONID1)
    {
        if (reqElev)
        {
            auto workingDir = processPath.substr(0, processPath.find_last_of('\\'));
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

        tdc.pButtons = aCustomButtons2;
        tdc.cButtons = _countof(aCustomButtons2);
        //tdc.pszMainIcon = TD_INFORMATION_ICON;
        tdc.dwFlags |= TDF_SHOW_MARQUEE_PROGRESS_BAR;
        tdc.dwFlags |= TDF_CALLBACK_TIMER;
        tdc.pszMainInstruction = L"Downloading Update...";
        tdc.pszContent = L"Preparing to download...";
        tdc.pszVerificationText = L"";
        tdc.pRadioButtons = NULL;
        tdc.cRadioButtons = NULL;
        tdc.lpCallbackData = 1;

        bool bNotCanceledorError = false;
        std::thread t([&FilesToUpdate, &FilesToDownload, &bCheckboxChecked, &nRadioBtnID, &bNotCanceledorError]
        {
            std::vector<std::pair<std::wstring, std::string>> downloads;

            for (auto &it : FilesToDownload)
            {
                UpdateFile(downloads, it.wszFileName, it.wszFullFilePath, it.wszDownloadURL, it.wszDownloadName, true, nRadioBtnID, true);
            }

            for (auto &it : FilesToUpdate)
            {
                UpdateFile(downloads, it.wszFileName, it.wszFullFilePath, it.wszDownloadURL, it.wszDownloadName, bCheckboxChecked != 0, nRadioBtnID, false);
            }
            SendMessage(DialogHwnd, TDM_CLICK_BUTTON, static_cast<WPARAM>(TDCBF_OK_BUTTON), 0);
            bNotCanceledorError = true;
            downloads.clear();
        });

        hr = TaskDialogIndirect(&tdc, &nClickedBtnID, nullptr, nullptr);

        if (SUCCEEDED(hr))
        {
            t.join();

            if (bNotCanceledorError && nClickedBtnID != BUTTONID3)
            {
                TASKDIALOG_BUTTON aCustomButtons3[] = {
                    { BUTTONID4, L"Restart the game to apply changes" },
                    { BUTTONID5, L"Continue" },
                };

                tdc.pButtons = aCustomButtons3;
                tdc.cButtons = _countof(aCustomButtons3);
                tdc.pszMainInstruction = L"Update completed succesfully.";
                tdc.pszContent = L"";
                tdc.lpCallbackData = 2;
                tdc.dwFlags |= TDF_CALLBACK_TIMER;

                hr = TaskDialogIndirect(&tdc, &nClickedBtnID, nullptr, nullptr);

                bool bSkipUpdateCompleteDialog = iniReader.ReadInteger("MISC", "SkipUpdateCompleteDialog", 0) != 0;
                if (bSkipUpdateCompleteDialog)
                    SendMessage(DialogHwnd, TDM_CLICK_BUTTON, static_cast<WPARAM>(TDCBF_OK_BUTTON), 0);

                if (nClickedBtnID == BUTTONID4)
                {
                    auto workingDir = processPath.substr(0, processPath.find_last_of('\\'));
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
                std::wcout << L"Update cancelled or error occured." << std::endl;
                if (MainHwnd == NULL)
                    EnumWindows(EnumWindowsProc, GetCurrentProcessId());

                SwitchToThisWindow(MainHwnd, TRUE);
            }
        }
    }
    else
    {
        std::wcout << L"Update cancelled." << std::endl;
        if (MainHwnd == NULL)
            EnumWindows(EnumWindowsProc, GetCurrentProcessId());

        SwitchToThisWindow(MainHwnd, TRUE);
    }
}

std::tuple<int32_t, std::string, std::string, std::string> GetRemoteFileInfo(std::wstring strFileName, std::wstring strUrl)
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
    auto rTest = cpr::Head(cpr::Url{ szUrl });

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

        szUrl = "https://api.github.com" + repos + user + "/" + repo + "/releases" + "?per_page=100" + (!token.empty() ? ("&access_token=" + token) : "");

        std::wcout << L"Connecting to GitHub: " << toWString(szUrl) << std::endl;

        auto rLink = cpr::Head(cpr::Url{ szUrl });

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
            auto r = cpr::Get(cpr::Url{ szUrl + page + std::to_string(i) });

            if (r.status_code == 200)
            {
                Json::Value parsedFromString;
                Json::Reader reader;
                bool parsingSuccessful = reader.parse(r.text, parsedFromString);

                if (parsingSuccessful)
                {
                    std::wcout << L"GitHub's response parsed successfully." << L" Page " << i << "." << std::endl;

                    // default use case, when file that's being updated is inside the zip archive of the same name
                    for (Json::ValueConstIterator it = parsedFromString.begin(); it != parsedFromString.end(); ++it)
                    {
                        const Json::Value& jVal = *it;

                        if (jVal["draft"] == true || jVal["prerelease"] == true)
                            continue;

                        for (size_t i = 0; i < jVal["assets"].size(); i++)
                        {
                            if (starts_with(jVal["assets"][i]["name"].asString().c_str(), toString(strFileName).c_str(), false)) //case INsensitive!
                            {
                                std::wcout << L"Found " << toWString((jVal["assets"][i]["name"]).asString()) << L" on GitHub." << std::endl;

                                date::sys_seconds tp;
                                auto now = date::floor<std::chrono::seconds>(std::chrono::system_clock::now());
                                std::istringstream ss{ jVal["assets"][i]["updated_at"].asString() };
                                ss >> date::parse("%FT%TZ", tp); // "updated_at": "2016-08-16T11:42:53Z" ISO 8601
                                auto nRemoteFileUpdateTime = date::make_time(now - tp).hours().count();

                                return std::make_tuple((bool(ss) ? nRemoteFileUpdateTime : -2), jVal["assets"][i]["browser_download_url"].asString(), jVal["assets"][i]["name"].asString(), jVal["assets"][i]["size"].asString());
                            }
                        }
                    }

                    //alternative use cases, files and archives are named differently, so we'll try to guess and find something to update
                    auto iniSecCount = iniReader.data.count(toString(strFileName + strExtension));
                    auto iniSecSourceName = iniReader.ReadString(toString(strFileName + strExtension), SOURCENAME, "");

                    if (!iniSecSourceName.empty())
                    {
                        std::wcout << L"Archive with the same name not found, using " + toWString(iniSecSourceName) + L" parameter from ini." << std::endl;

                        for (Json::ValueConstIterator it = parsedFromString.begin(); it != parsedFromString.end(); ++it)
                        {
                            const Json::Value& jVal = *it;

                            if (jVal["draft"] == true || jVal["prerelease"] == true)
                                continue;

                            for (size_t i = 0; i < jVal["assets"].size(); i++)
                            {
                                std::string name(jVal["assets"][i]["name"].asString());
                                if (starts_with(name.c_str(), iniSecSourceName.c_str(), false)) //case INsensitive!
                                {
                                    std::wcout << L"Found " << toWString((jVal["assets"][i]["name"]).asString()) << L" on GitHub." << std::endl;

                                    date::sys_seconds tp;
                                    auto now = date::floor<std::chrono::seconds>(std::chrono::system_clock::now());
                                    std::istringstream ss{ jVal["assets"][i]["updated_at"].asString() };
                                    ss >> date::parse("%FT%TZ", tp); // "updated_at": "2016-08-16T11:42:53Z" ISO 8601
                                    auto nRemoteFileUpdateTime = date::make_time(now - tp).hours().count();

                                    return std::make_tuple((bool(ss) ? nRemoteFileUpdateTime : -2), jVal["assets"][i]["browser_download_url"].asString(), jVal["assets"][i]["name"].asString(), jVal["assets"][i]["size"].asString());
                                }
                            }
                        }
                    }
                    else if (iniSecCount) // if section with the file name is present, plugin will assume that the source is valid
                    {
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
        if (szUrl.find("node-js-geturl") != std::string::npos)
        {
            auto r = cpr::Get(cpr::Url{ szUrl });
            if (!r.text.empty())
                szUrl = r.text;
        }

        std::wcout << "Connecting to " << toWString(szUrl) << std::endl;

        auto rHead = cpr::Head(cpr::Url{ szUrl });

        if (rHead.status_code == 405) // method not allowed
        {
            auto r = cpr::Get(cpr::Url{ szUrl });
            if (r.status_code == 200) // ok
            {
                rHead = r;
            }
        }

        if (rHead.status_code == 200) // ok
        {
            std::wcout << "Found " << toWString(rHead.header["Content-Type"]) << std::endl;
            std::string szDownloadURL = rHead.url.c_str();
            std::string tempStr = szDownloadURL.substr(szDownloadURL.find_last_of('/') + 1);
            auto endPos = tempStr.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567890_.");
            std::string szDownloadName = tempStr.substr(0, (endPos == 0) ? std::string::npos : endPos);
            std::string szFileSize = rHead.header["Content-Length"];

            std::tm t;
            std::istringstream ss(rHead.header["Last-Modified"]); // Wed, 27 Jul 2016 18:43:42 GMT
            ss >> std::get_time(&t, "%a, %d %b %Y %H:%M:%S %Z");

            auto now = date::floor<std::chrono::seconds>(std::chrono::system_clock::now());
            auto tp = date::sys_days{ date::year{ t.tm_year + 1900 } / (t.tm_mon + 1) / t.tm_mday } +std::chrono::hours{ t.tm_hour } +std::chrono::minutes{ t.tm_min } +std::chrono::seconds{ t.tm_sec };
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

DWORD WINAPI ProcessFiles(LPVOID)
{
    CleanupLockedFiles();

    bool bSelfUpdate = iniReader.ReadInteger("MISC", "SelfUpdate", 1) != 0;
    bool bAlwaysUpdate = iniReader.ReadInteger("DEBUG", "AlwaysUpdate", 0) != 0;

    std::vector<std::tuple<std::wstring, std::wstring, WIN32_FIND_DATAW>> FilesUpdateData;
    std::vector<FileUpdateInfo> FilesToUpdate;
    std::vector<FileUpdateInfo> FilesToDownload;
    std::vector<std::wstring> FilesPresent;
    std::set<std::pair<std::string, std::string>> IniExcludes;

    auto cb = [&FilesUpdateData, &IniExcludes, &bSelfUpdate](std::wstring& s, WIN32_FIND_DATAW& fd)
    {
        auto strFileName = s.substr(s.rfind('\\') + 1);
        auto selfName = toLowerWStr(selfPath.substr(selfPath.rfind('\\') + 1));
        auto selfNameNoExt = toLowerWStr(selfName.substr(0, selfName.rfind('.') + 1));

        if ((bSelfUpdate == false) && (strFileName == selfName || strFileName == selfNameNoExt + L"asi" || strFileName == selfNameNoExt + L"exe"))
            return;

        if (strFileName.find(L".deleteonnextlaunch") != std::wstring::npos)
            return;

        auto iniData = iniReader.data.find(toString(strFileName));
        if (iniData != iniReader.data.end())
        {
            if (iniData->second.find(IGNOREUPDATES) != iniData->second.end())
            {
                std::wcout << strFileName << L" is ignored." << std::endl;
                return;
            }
        }

        //Checking ini file for url
        auto iniEntry = iniReader.data.get("MODS", toString(strFileName), "");
        if (!iniEntry.empty())
        {
            removeQuotesFromString(iniEntry);
            FilesUpdateData.push_back(std::make_tuple(s, toWString(iniEntry), fd));
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
            wchar_t* UpdateUrl = UPDATEURL;
            if (versionInfo.find(UpdateUrl) != std::wstring::npos)
            {
                try {
                    versionInfo = versionInfo.substr(versionInfo.find(UpdateUrl) + wcslen(UpdateUrl) + sizeof(wchar_t));
                    versionInfo = versionInfo.substr(0, versionInfo.find_first_of(L'\0'));
                    FilesUpdateData.push_back(std::make_tuple(s, versionInfo, fd));
                    return;
                }
                catch (std::out_of_range & ex) {
                    std::wcout << ex.what() << std::endl;
                }
            }
        }

        //Checking if string ends with
        if (iniReader.data.find("MODS") != iniReader.data.end())
        {
            for (auto& pair : iniReader.data.at("MODS"))
            {
                auto strIni = pair.first;
                auto iniEntry = pair.second;
                auto ext = toWString(strIni.substr(strIni.find_last_of('.')));
                removeQuotesFromString(iniEntry);

                auto lcs = GetLongestCommonSubstring(toWString(strIni), strFileName);
                auto strippedname = strFileName.substr(0, strFileName.find_last_of('.'));
                strippedname = strippedname.substr(1, strippedname.find_last_of('.'));
                auto lcs2 = GetLongestCommonSubstring(toWString(iniEntry), strippedname);

                if (lcs != ext)
                {
                    if ((lcs.length() > std::string("???.asi").length()) || (lcs2.length() >= std::string("skygfx").length()))
                    {
                        if (ends_with(toString(strFileName).c_str(), toString(lcs).c_str(), false))
                        {
                            IniExcludes.insert(std::make_pair(strIni, iniEntry));
                            FilesUpdateData.push_back(std::make_tuple(s, toWString(iniEntry), fd));
                            return;
                        }
                    }
                }
            }
        }

        //WFP
        if (ends_with(toString(strFileName).c_str(), ".WidescreenFix.asi", false))
            FilesUpdateData.push_back(std::make_tuple(s, std::wstring(L"https://github.com/ThirteenAG/WidescreenFixesPack/releases"), fd));
    };

    FindFilesRecursively(modulePath, cb);

    if (iniReader.ReadInteger("MISC", "ScanFromRootFolder", 0) == 0)
    {
        if (modulePath != processPath.substr(0, processPath.rfind('\\') + 1))
            FindFilesRecursively(processPath.substr(0, processPath.rfind('\\') + 1), cb, true);
    }

    for (auto& tuple : FilesUpdateData)
    {
        std::wstring path = std::get<0>(tuple);
        std::wstring url = std::get<1>(tuple);
        WIN32_FIND_DATAW fd = std::get<2>(tuple);

        auto strFileName = path.substr(path.rfind('\\') + 1);

        std::wcout << strFileName << " " << "found." << std::endl;
        std::wcout << "Update URL:" << " " << url << std::endl;

        int32_t nLocaFileUpdatedHoursAgo = GetLocalFileInfo(fd.ftCreationTime, fd.ftLastAccessTime, fd.ftLastWriteTime);
        auto RemoteInfo = GetRemoteFileInfo(strFileName, url);
        auto nRemoteFileUpdatedHoursAgo = std::get<0>(RemoteInfo);
        auto szDownloadURL = std::get<1>(RemoteInfo);
        auto szDownloadName = std::get<2>(RemoteInfo);
        auto szFileSize = std::get<3>(RemoteInfo);
        FilesPresent.push_back(strFileName);

        if (nRemoteFileUpdatedHoursAgo != -1 && !szDownloadURL.empty())
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

    if (iniReader.data.find("MODS") != iniReader.data.end())
    {
        for (auto& pair : iniReader.data.at("MODS"))
        {
            auto strIni = pair.first;
            auto iniEntry = pair.second;
            removeQuotesFromString(iniEntry);

            auto iniData = iniReader.data.find(strIni);
            if (iniData != iniReader.data.end())
            {
                if (iniData->second.find(IGNOREUPDATES) != iniData->second.end())
                {
                    std::wcout << toWString(strIni) << L" is ignored." << std::endl;
                    continue;
                }
            }

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
                auto RemoteInfo = GetRemoteFileInfo(toWString(strIni), toWString(iniEntry));
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
                    fui.wszFullFilePath = selfPath.substr(0, selfPath.rfind('\\') + 1) + fui.wszFileName;
                    fui.wszDownloadURL = toWString(szDownloadURL);
                    fui.wszDownloadName = toWString(szDownloadName);
                    fui.nRemoteFileUpdatedHoursAgo = nRemoteFileUpdatedHoursAgo;
                    fui.nLocaFileUpdatedHoursAgo = INT_MAX;
                    fui.nFileSize = nFileSize;

                    if (fui.wszFullFilePath.find(L"\\modloader\\") != std::wstring::npos && fui.wszFullFilePath.find(L"modloader\\modloader.asi") == std::wstring::npos)
                    {
                        auto pos = fui.wszFullFilePath.find_last_of('\\');
                        fui.wszFullFilePath.insert(pos, L"\\" + fui.wszFileName.substr(0, fui.wszFileName.find_last_of(L".")));
                    }

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

    if (!FilesToUpdate.empty() || !FilesToDownload.empty())
        ShowUpdateDialog(FilesToUpdate, FilesToDownload);
    else
        std::wcout << L"No files found to process." << std::endl;

    std::wcout.rdbuf(outbuf);
    return 0;
}

void Init()
{
    wchar_t buffer[MAX_PATH];
    HMODULE hm = NULL;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&Init, &hm))
        std::wcout << L"GetModuleHandle returned " << GetLastError() << std::endl;
    GetModuleFileNameW(hm, buffer, sizeof(buffer));
    selfPath = buffer;
    modulePath = std::wstring(buffer).substr(0, std::wstring(buffer).rfind('\\') + 1);
    GetModuleFileNameW(NULL, buffer, sizeof(buffer));
    processPath = std::wstring(buffer);

    if (!CanAccessFolder(modulePath.c_str(), GENERIC_READ))
        return;

    iniReader.SetIniPath();

    auto nUpdateFrequencyInHours = iniReader.ReadInteger("DATE", "UpdateFrequencyInHours", 6);
    auto szWhenLastUpdateAttemptWasInHours = iniReader.ReadString("DATE", "WhenLastUpdateAttemptWas", "");

    if (iniReader.ReadInteger("MISC", "OutputLogToFile", 0) != 0)
    {
        logFile.open(modulePath + L"modupdater.log");
        outbuf = std::wcout.rdbuf(logFile.rdbuf());
    }

    auto now = date::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    date::sys_seconds tp;
    auto nWhenLastUpdateAttemptWas = std::chrono::hours(INT_MAX);
    std::istringstream in{ szWhenLastUpdateAttemptWasInHours };
    in >> date::parse("%D %T %Z", tp);
    if (now >= tp)
        nWhenLastUpdateAttemptWas = date::make_time(now - tp).hours();

    if (bool(in) && nWhenLastUpdateAttemptWas < std::chrono::hours(nUpdateFrequencyInHours) && !iniReader.ReadInteger("DEBUG", "AlwaysUpdate", 0) != 0)
    {
        std::wcout << L"Last update attempt was " << nWhenLastUpdateAttemptWas.count() << L" hours ago." << std::endl;
        std::wcout << L"Modupdater is configured to update once every " << nUpdateFrequencyInHours << L" hours." << std::endl;
    }
    else
    {
        iniReader.WriteString("DATE", "WhenLastUpdateAttemptWas", date::format("%D %T %Z", now));

        std::wcout << "Current directory: " << modulePath << std::endl;

        auto procFold = processPath.substr(0, processPath.rfind('\\') + 1);
        if (iniReader.ReadInteger("MISC", "ScanFromRootFolder", 0) != 0)
            modulePath = procFold;

        token = iniReader.ReadString("DEBUG", "Token", "");

        if (!CanAccessFolder(modulePath.c_str(), GENERIC_READ | GENERIC_WRITE) || !CanAccessFolder(procFold.c_str(), GENERIC_READ | GENERIC_WRITE))
            reqElev = true;

        CloseHandle(CreateThread(0, 0, (LPTHREAD_START_ROUTINE)&ProcessFiles, 0, 0, NULL));
    }

    std::getchar();
}

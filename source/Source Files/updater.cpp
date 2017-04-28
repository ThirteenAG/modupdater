#include "stdafx.h"
#include "string_funcs.h"

struct FileUpdateInfo
{
    std::wstring wszFullFilePath;
    std::wstring wszFileName;
    std::wstring wszDownloadURL;
    std::wstring wszDownloadName;
    int32_t nRemoteFileUpdatedDaysAgo;
    int32_t nLocaFileUpdatedDaysAgo;
    int32_t	nFileSize;
};

CIniReader iniReader;
HWND MainHwnd, DialogHwnd;
std::wstring modulePath, processPath, selfPath;
std::wstring messagesBuffer;
std::wofstream logFile;
std::wstreambuf* outbuf;

#define EXTRACTSINGLEFILE "ExtractSingleFile"
#define PLACETOROOT       "PlaceToRoot"
#define IGNOREUPDATES     "IgnoreUpdates"
#define CUSTOMPATH        "CustomPath"

#define UPDATEURL L"UpdateUrl"
#define GHTOKEN    "63c1f1cc5782c8f1dafad05448e308f0cf8c9198"
#define UALNAME   L"Ultimate-ASI-Loader.zip"
#define BUTTONID1  1001
#define BUTTONID2  1002
#define BUTTONID3  1003
#define BUTTONID4  1004
#define BUTTONID5  1005
#define RBUTTONID1 1011
#define RBUTTONID2 1012
#define RBUTTONID3 1013

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

void UpdateFile(std::vector<std::pair<std::wstring, std::string>>& downloads, std::wstring wzsFileName, std::wstring wszFullFilePath, std::wstring wszDownloadURL, std::wstring wszDownloadName, bool bCheckboxChecked, int32_t nRadioBtnID, bool bCreateDirectories)
{
    messagesBuffer = L"Downloading " + wszDownloadName;
    std::wcout << messagesBuffer << std::endl;

    std::wstring ualName;
    if (wszDownloadName == UALNAME)
    {
        ualName = wzsFileName;
        wzsFileName = L"dinput8.dll";
    }

    std::string cusPath;
    auto iniData = iniReader.data[toString(wzsFileName)];
    if (iniData.find(CUSTOMPATH) != iniData.end() && bCreateDirectories) //bCreateDirectories - if downloading files, not updating
        cusPath = "\\" + iniData[CUSTOMPATH] + "\\";

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

        if (wszDownloadName.substr(wszDownloadName.find_last_of('.')) != L".zip")
        {
            messagesBuffer = L"Processing " + wszDownloadName;
            std::wcout << messagesBuffer << std::endl;

            std::wstring fullPath = wszFullFilePath.substr(0, wszFullFilePath.find_last_of('\\') + 1) + wzsFileName;

            if (CheckForFileLock(fullPath.c_str()) == FALSE)
            {
                std::wcout << wszDownloadName << L" is locked. Renaming..." << std::endl;
                if (MoveFileW(_T(fullPath.c_str()), _T(std::wstring(fullPath + L".deleteonnextlaunch").c_str())))
                    std::wcout << wszDownloadName << L" was renamed to " << fullPath + L".deleteonnextlaunch" << std::endl;
                else
                    std::wcout << L"Error: " << GetLastError() << std::endl;
            }
            else
            {
                std::wcout << wszDownloadName << L" is not locked." << std::endl;
            }

            createFolder(fullPath.substr(0, fullPath.find_last_of('\\')));

            std::ofstream outputFile(fullPath, std::ios::binary);
            outputFile.write((const char*)&buffer[0], buffer.size());
            outputFile.close();

            messagesBuffer = wzsFileName + L" was updated succesfully.";
            std::wcout << messagesBuffer << std::endl;
            return;
        }

        using namespace zipper;
        Unzipper unzipper(buffer);
        std::vector<ZipEntry> entries = unzipper.entries();
        std::wstring szFilePath, szFileName;

        auto itr = std::find_if(entries.begin(), entries.end(), [&wzsFileName, &szFilePath, &szFileName](auto &s)
        {
            auto s1 = s.name.substr(0, s.name.rfind('/') + 1);
            auto s2 = s.name.substr(s.name.rfind('/') + 1);

            if (s2.size() != wzsFileName.size())
                return false;

            for (size_t i = 0; i < s2.size(); ++i)
            {
                if (!(::tolower(s2[i]) == ::tolower(wzsFileName[i])))
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

            for (auto it = entries.begin(); it != entries.end(); it++)
            {
                std::wstring lowcsIt, lowcsFilePath, lowcsFileName, itFileName, lowcsItFileName;
                std::transform(it->name.begin(), it->name.end(), std::back_inserter(lowcsIt), ::tolower);
                std::transform(szFilePath.begin(), szFilePath.end(), std::back_inserter(lowcsFilePath), ::tolower);
                std::transform(szFileName.begin(), szFileName.end(), std::back_inserter(lowcsFileName), ::tolower);
                itFileName = toWString(it->name);
                std::replace(itFileName.begin(), itFileName.end(), '/', '\\');
                itFileName.erase(0, szFilePath.length());
                std::transform(itFileName.begin(), itFileName.end(), std::back_inserter(lowcsItFileName), ::tolower);

                if (!itFileName.empty() && (itFileName.back() != L'\\'))
                {
                    if ((!bCheckboxChecked && lowcsIt.find(lowcsFileName) != std::string::npos) || (bCheckboxChecked && lowcsIt.find(lowcsFilePath) != std::string::npos))
                    {
                        if (wszFullFilePath.find(L"modloader\\modloader.asi") != std::string::npos && itFileName.find(L"modloader\\") != std::string::npos)
                            itFileName.erase(0, std::wstring(L"modloader\\").length());

                        std::wstring fullPath = wszFullFilePath.substr(0, wszFullFilePath.find_last_of('\\') + 1) + toWString(cusPath) + itFileName;

                        auto bExtractSingleFile = iniData.find(EXTRACTSINGLEFILE) != iniData.end();
                        auto bPlaceToRoot = iniData.find(PLACETOROOT) != iniData.end();

                        if (bPlaceToRoot || !ualName.empty())
                            fullPath = processPath.substr(0, processPath.rfind('\\') + 1) + (ualName.empty() ? itFileName : ualName);

                        if (!ualName.empty())
                            ualName.clear();

                        std::string fullPathStr = toString(fullPath);
                        if (CheckForFileLock(fullPath.c_str()) == FALSE)
                        {
                            std::wcout << itFileName << L" is locked. Renaming..." << std::endl;
                            if (MoveFileW(_T(fullPath.c_str()), _T(std::wstring(fullPath + L".deleteonnextlaunch").c_str())))
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
                        if (pos != std::string::npos)
                        {
                            auto iniName = lowcsItFileName;
                            auto iniPath = fullPath.substr(0, fullPath.find_last_of('.'));
                            auto iniEntry = it->name.substr(0, it->name.find_last_of('.'));
                            auto fileExtension = lowcsItFileName.substr(pos);
                            iniName.erase(pos);
                            iniName.append(L".ini");
                            iniPath.append(L".ini");
                            iniEntry.append(".ini");

                            auto extractINI = [&unzipper, &iniEntry, &iniPath, &iniName]()->void
                            {
                                std::vector<uint8_t> vec;
                                unzipper.extractEntryToMemory(iniEntry, vec);
                                if (!vec.empty())
                                {
                                    std::ofstream iniFile(iniPath, std::ios::binary);
                                    unzipper.extractEntryToStream(iniEntry, iniFile);
                                    iniFile.close();
                                    messagesBuffer = iniName + L" was updated succesfully.";
                                    std::wcout << messagesBuffer << std::endl;
                                    vec.clear();
                                }
                            };

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
                                        extractINI();
                                    }
                                }
                                else //compare and replace
                                {
                                    if (bCheckboxChecked)
                                    {
                                        if (fileExtension == L".ini")
                                        {
                                            std::stringstream iniSS;
                                            unzipper.extractEntryToStream(it->name, iniSS);
                                            CIniReader ini1(iniSS);
                                            CIniReader ini2(fullPathStr.c_str());

                                            if (ini1.CompareBySections(ini2))
                                            {
                                                messagesBuffer = iniName + L" doesn't seem to be changed.";
                                                std::wcout << messagesBuffer << std::endl;
                                                continue;
                                            }
                                            else
                                            {
                                                messagesBuffer = iniName + L" is changed, replacing.";
                                                std::wcout << messagesBuffer << std::endl;
                                            }
                                        }
                                    }
                                    else
                                    {
                                        std::stringstream iniSS;
                                        unzipper.extractEntryToStream(iniEntry, iniSS);
                                        CIniReader ini1(iniSS);
                                        CIniReader ini2(toString(iniPath).c_str());

                                        if (ini1.CompareBySections(ini2))
                                        {
                                            messagesBuffer = iniName + L" doesn't seem to be changed.";
                                            std::wcout << messagesBuffer << std::endl;
                                        }
                                        else
                                        {
                                            messagesBuffer = iniName + L" is changed, replacing.";
                                            std::wcout << messagesBuffer << std::endl;
                                            extractINI();
                                        }
                                    }
                                }
                            }
                        }

                        std::ofstream outputFile(fullPath, std::ios::binary);
                        unzipper.extractEntryToStream(it->name, outputFile);
                        outputFile.close();

                        messagesBuffer = itFileName + L" was updated succesfully.";
                        std::wcout << messagesBuffer << std::endl;

                        if (bExtractSingleFile)
                        {
                            unzipper.close();
                            return;
                        }
                    }
                }
            }
        }
        unzipper.close();
    }
}

void ShowUpdateDialog(std::vector<FileUpdateInfo>& FilesToUpdate, std::vector<FileUpdateInfo>& FilesToDownload)
{
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
            ShellExecuteW(hwnd, _T(L"open"), (LPCWSTR)lParam, NULL, NULL, SW_SHOW);
            break;
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

    TASKDIALOGCONFIG tdc = { sizeof(TASKDIALOGCONFIG) };
    auto szTitle = L"modupdater";
    auto szCheckboxText = L"Update all downloaded files.";
    auto szHeader = std::wstring(L"Updates available:\n");
    std::wstring szBodyText;
    int32_t nTotalUpdateSize = 0;

    if (!FilesToDownload.empty())
    {
        for (auto &it : FilesToDownload)
        {
            szBodyText += L"<a href=\"" + it.wszDownloadURL + L"\">" + it.wszFileName + L"</a> ";
            szBodyText += L"(" + it.wszDownloadName + L" / " + formatBytesW(it.nFileSize) + L")" L"\n";
            szBodyText += L"Remote file updated " + std::to_wstring(it.nRemoteFileUpdatedDaysAgo) + L" days ago." L"\n";
            szBodyText += L"Local file was not present. This mod will be installed." L"\n";
            szBodyText += L"\n";
            nTotalUpdateSize += it.nFileSize;
        }
    }

    if (!FilesToUpdate.empty())
    {
        for (auto &it : FilesToUpdate)
        {
            szBodyText += L"<a href=\"" + it.wszDownloadURL + L"\">" + it.wszFileName + L"</a> ";
            szBodyText += L"(" + it.wszDownloadName + L" / " + formatBytesW(it.nFileSize) + L")" L"\n";
            szBodyText += L"Remote file updated " + std::to_wstring(it.nRemoteFileUpdatedDaysAgo) + L" days ago." L"\n";
            szBodyText += L"Local file updated " + std::to_wstring(it.nLocaFileUpdatedDaysAgo) + L" days ago." L"\n";
            szBodyText += L"\n";
            nTotalUpdateSize += it.nFileSize;
        }
    }

    szBodyText += L"Do you want to download these updates?";

    auto szButton1Text = std::wstring(L"Download and install updates now\n" + formatBytesW(nTotalUpdateSize));
    std::string szURL = std::string(iniReader.ReadString("MISC", "WebUrl", "https://thirteenag.github.io/wfp"));
    auto wszFooter = std::wstring(L"<a href=\"" + toWString(szURL) + L"\">" + toWString(szURL) + L"</a>");

    TASKDIALOG_BUTTON aCustomButtons[] = {
        { BUTTONID1, szButton1Text.c_str() },
        { BUTTONID2, L"Cancel" },
    };

    TASKDIALOG_BUTTON radioButtons[] =
    {
        { RBUTTONID1, L"INI files: replace if sections or keys don't match" },
        { RBUTTONID2, L"INI files: replace all" },
        { RBUTTONID3, L"Don't replace INI files" }
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
        TASKDIALOG_BUTTON aCustomButtons2[] = {
            { BUTTONID3, L"Cancel" },
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
    if (pos != std::string::npos)
    {
        strExtension = strFileName.substr(pos);
        strFileName.erase(pos);
    }
    else
        strFileName.append(L".asi");

    auto szUrl = toString(strUrl);

    if (szUrl.find("github.com") != std::string::npos)
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

        szUrl = "https://api.github.com" + repos + user + "/" + repo + "/releases" + "?access_token=" GHTOKEN; // "&per_page=100";

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

                    for (Json::ValueConstIterator it = parsedFromString.begin(); it != parsedFromString.end(); ++it)
                    {
                        const Json::Value& wsFix = *it;

                        for (size_t i = 0; i < wsFix["assets"].size(); i++)
                        {
                            std::string str1, str2;
                            std::string name(wsFix["assets"][i]["name"].asString());

                            std::transform(strFileName.begin(), strFileName.end(), std::back_inserter(str1), ::tolower);
                            std::transform(name.begin(), name.end(), std::back_inserter(str2), ::tolower);

                            if (((str1 + ".zip") == str2) || ((str1 + toString(strExtension)) == str2) || (szUrl.find(name.substr(0, name.find('.'))) != std::string::npos))
                            {
                                std::wcout << L"Found " << toWString((wsFix["assets"][i]["name"]).asString()) << L" on GitHub." << std::endl;
                                auto szDownloadURL = wsFix["assets"][i]["browser_download_url"].asString();
                                auto szDownloadName = wsFix["assets"][i]["name"].asString();
                                auto szFileSize = wsFix["assets"][i]["size"].asString();

                                using namespace date;
                                int32_t y, m, d; // "updated_at": "2016-08-16T11:42:53Z"
                                sscanf_s(wsFix["assets"][i]["updated_at"].asCString(), "%d-%d-%d%*s", &y, &m, &d);
                                auto nRemoteFileUpdateTime = date::year(y) / date::month(m) / date::day(d);
                                auto now = floor<days>(std::chrono::system_clock::now());
                                return std::make_tuple((sys_days{ now } - sys_days{ nRemoteFileUpdateTime }).count(), szDownloadURL, szDownloadName, szFileSize);
                            }
                        }
                    }
                }
            }
            else
            {
                std::wcout << L"Something wrong! " << L"Status code: " << r.status_code << std::endl;
            }
        }
        std::wcout << L"Nothing is found on GitHub." << std::endl << std::endl;
    }
    else
    {
        if (szUrl.find("node-js-geturl") != std::string::npos)
        {
            auto r = cpr::Get(cpr::Url{ szUrl });
            if (!r.text.empty())
                szUrl = r.text;
        }

        strFileName.append(toWString(szUrl.substr(szUrl.find_last_of('.'))));

        std::wcout << "Connecting to " << toWString(szUrl) << std::endl;

        auto rHead = cpr::Head(cpr::Url{ szUrl });

        if (rHead.status_code == 405) // method not allowed
        {
            auto r = cpr::Get(cpr::Url{ szUrl });
            if (r.status_code == 200)
            {
                rHead = r;
            }
        }

        if (rHead.status_code == 200)
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

            using namespace date;
            auto nRemoteFileUpdateTime = date::year(t.tm_year + 1900) / date::month(t.tm_mon + 1) / date::day(t.tm_mday);
            auto now = floor<days>(std::chrono::system_clock::now());
            return std::make_tuple((sys_days{ now } - sys_days{ nRemoteFileUpdateTime }).count(), szDownloadURL, szDownloadName, szFileSize);
        }
        else
            std::wcout << L"Seems like this archive doesn't contain " << strFileName.substr(0, strFileName.find_last_of(L".")) << strExtension << std::endl;
    }

    return std::make_tuple(-1, "", "", "");
}

int32_t GetLocalFileInfo(FILETIME ftCreate, FILETIME ftLastAccess, FILETIME ftLastWrite)
{
    using namespace date;
    SYSTEMTIME stUTC;
    FileTimeToSystemTime(&ftLastWrite, &stUTC);
    auto nLocalFileUpdateTime = date::year(stUTC.wYear) / date::month(stUTC.wMonth) / date::day(stUTC.wDay);
    auto now = floor<days>(std::chrono::system_clock::now());
    return (sys_days{ now } - sys_days{ nLocalFileUpdateTime }).count();
}

DWORD WINAPI ProcessFiles(LPVOID)
{
    CleanupLockedFiles();

    if (iniReader.ReadInteger("MISC", "OutputLogToFile", 0) != 0)
    {
        logFile.open(modulePath + L"modupdater.log");
        outbuf = std::wcout.rdbuf(logFile.rdbuf());
        std::wcout << "Current directory: " << modulePath << std::endl;
    }
    
    bool bSelfUpdate = iniReader.ReadInteger("MISC", "SelfUpdate", 1) != 0;
    bool bAlwaysUpdate = iniReader.ReadInteger("DEBUG", "AlwaysUpdate", 0) != 0;

    std::vector<std::tuple<std::wstring, std::wstring, WIN32_FIND_DATAW>> FilesUpdateData;
    std::vector<FileUpdateInfo> FilesToUpdate;
    std::vector<FileUpdateInfo> FilesToDownload;
    std::vector<std::wstring> FilesPresent;
    std::set<std::pair<std::string, std::string>> IniExcludes;

    auto cb = [&FilesUpdateData, &IniExcludes](std::wstring& s, WIN32_FIND_DATAW& fd)
    {
        auto strFileName = s.substr(s.rfind('\\') + 1);

        if (strFileName.find(L".deleteonnextlaunch") != std::string::npos)
            return;

        auto iniData = iniReader.data[toString(strFileName)];
        if (iniData.find(IGNOREUPDATES) != iniData.end())
        {
            std::wcout << strFileName << L" is ignored." << std::endl;
            return;
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
        for (auto& pair : iniReader.data["MODS"])
        {
            auto strIni = pair.first;
            auto iniEntry = pair.second;
            removeQuotesFromString(iniEntry);

            auto lcs = GetLongestCommonSubstring(toWString(strIni), strFileName);
            auto lcs2 = GetLongestCommonSubstring(toWString(iniEntry), strFileName);

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

        if (!selfPath.empty())
        {
            auto selfName = selfPath.substr(selfPath.rfind('\\') + 1);
            selfName = selfName.substr(0, selfName.rfind('.') + 1);
            if (!bSelfUpdate && (strFileName == (selfName + L"asi") || strFileName == (selfName + L"exe")))
                continue;
        }

        std::wcout << strFileName << " " << "found." << std::endl;
        std::wcout << "Update URL:" << " " << url << std::endl;

        int32_t nLocaFileUpdatedDaysAgo = GetLocalFileInfo(fd.ftCreationTime, fd.ftLastAccessTime, fd.ftLastWriteTime);
        auto RemoteInfo = GetRemoteFileInfo(strFileName, url);
        auto nRemoteFileUpdatedDaysAgo = std::get<0>(RemoteInfo);
        auto szDownloadURL = std::get<1>(RemoteInfo);
        auto szDownloadName = std::get<2>(RemoteInfo);
        auto szFileSize = std::get<3>(RemoteInfo);
        FilesPresent.push_back(strFileName);

        if (nRemoteFileUpdatedDaysAgo != -1 && !szDownloadURL.empty())
        {
            if (nRemoteFileUpdatedDaysAgo < nLocaFileUpdatedDaysAgo || bAlwaysUpdate)
            {
                auto nFileSize = std::stoi(szFileSize);
                std::wcout << "Download link: " << toWString(szDownloadURL) << std::endl;
                std::wcout << "Remote file updated " << nRemoteFileUpdatedDaysAgo << " days ago." << std::endl;
                std::wcout << "Local file updated " << nLocaFileUpdatedDaysAgo << " days ago." << std::endl;
                std::wcout << "File size: " << nFileSize << "KB." << std::endl;
                std::wcout << std::endl;

                FileUpdateInfo fui;
                fui.wszFullFilePath = path;
                fui.wszFileName = strFileName;
                fui.wszDownloadURL = toWString(szDownloadURL);
                fui.wszDownloadName = toWString(szDownloadName);
                fui.nRemoteFileUpdatedDaysAgo = nRemoteFileUpdatedDaysAgo;
                fui.nLocaFileUpdatedDaysAgo = nLocaFileUpdatedDaysAgo;
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

    for (auto& pair : iniReader.data["MODS"])
    {
        auto strIni = pair.first;
        auto iniEntry = pair.second;
        removeQuotesFromString(iniEntry);

        auto iniData = iniReader.data[strIni];
        if (iniData.find(IGNOREUPDATES) != iniData.end())
        {
            std::wcout << toWString(strIni) << L" is ignored." << std::endl;
            continue;
        }

        if (strIni.at(0) == '.')
            continue;

        auto excl = std::find_if(IniExcludes.begin(), IniExcludes.end(),
            [&strIni, &iniEntry](auto it) { return (it.first == strIni && it.second == iniEntry); });

        if (excl != IniExcludes.end())
            continue;

        auto iter = std::find_if(FilesPresent.begin(), FilesPresent.end(),
            [&strIni](const std::wstring& wszFileName) -> bool { return wszFileName == toWString(strIni); });

        if (iter == FilesPresent.end())
        {
            auto RemoteInfo = GetRemoteFileInfo(toWString(strIni), toWString(iniEntry));
            auto nRemoteFileUpdatedDaysAgo = std::get<0>(RemoteInfo);
            auto szDownloadURL = std::get<1>(RemoteInfo);
            auto szDownloadName = std::get<2>(RemoteInfo);
            auto szFileSize = std::get<3>(RemoteInfo);

            if (nRemoteFileUpdatedDaysAgo != -1 && !szDownloadURL.empty())
            {
                auto nFileSize = std::stoi(szFileSize);
                std::wcout << L"Download link: " << toWString(szDownloadURL) << std::endl;
                std::wcout << L"Remote file updated " << nRemoteFileUpdatedDaysAgo << " days ago." << std::endl;
                std::wcout << L"Local file is not present." << std::endl;
                std::wcout << L"File size: " << nFileSize << "KB." << std::endl;
                std::wcout << std::endl;

                FileUpdateInfo fui;
                fui.wszFileName = toWString(strIni);
                fui.wszFullFilePath = selfPath.substr(0, selfPath.rfind('\\') + 1) + fui.wszFileName;
                fui.wszDownloadURL = toWString(szDownloadURL);
                fui.wszDownloadName = toWString(szDownloadName);
                fui.nRemoteFileUpdatedDaysAgo = nRemoteFileUpdatedDaysAgo;
                fui.nLocaFileUpdatedDaysAgo = INT_MAX;
                fui.nFileSize = nFileSize;

                if (fui.wszFullFilePath.find(L"\\modloader\\") != std::string::npos && fui.wszFullFilePath.find(L"modloader\\modloader.asi") == std::string::npos)
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
    if (!GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&Init, &hm))
        std::wcout << L"GetModuleHandle returned " << GetLastError() << std::endl;
    GetModuleFileNameW(hm, buffer, sizeof(buffer));
    selfPath = buffer;
    modulePath = std::wstring(buffer).substr(0, std::wstring(buffer).rfind('\\') + 1);
    GetModuleFileNameW(NULL, buffer, sizeof(buffer));
    processPath = std::wstring(buffer);

    if (selfPath == processPath)
    {
        auto p = processPath.substr(0, processPath.rfind('\\') + 1) + L"..\\";
        if (std::experimental::filesystem::exists(p + L"gta_sa.exe") || std::experimental::filesystem::exists(p + L"gta-sa.exe") || 
            std::experimental::filesystem::exists(p + L"gta_vc.exe") || std::experimental::filesystem::exists(p + L"gta-vc.exe") ||
            std::experimental::filesystem::exists(p + L"gta3.exe"))
        {
            processPath = p;
        }
    }

    iniReader.SetIniPath();

    if (iniReader.ReadInteger("MISC", "ScanFromRootFolder", 0) != 0)
        modulePath = processPath.substr(0, processPath.rfind('\\') + 1);

    CreateThread(0, 0, (LPTHREAD_START_ROUTINE)&ProcessFiles, 0, 0, NULL);

    std::getchar();
}

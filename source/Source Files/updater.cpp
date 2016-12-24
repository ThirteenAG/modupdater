#include "stdafx.h"

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
std::vector<FileUpdateInfo> FilesToUpdate;
std::wstring modulePath, processPath;
std::wstring messagesBuffer;
HWND MainHwnd, DialogHwnd;
#define BUTTONID1 1001
#define BUTTONID2 1002
#define BUTTONID3 1003
#define BUTTONID4 1004
#define BUTTONID5 1005

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

void FindFilesRecursively(const std::wstring &directory, std::function<void(std::wstring, WIN32_FIND_DATAW)> callback)
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

		for (std::vector<std::wstring>::iterator iter = directories.begin(), end = directories.end(); iter != end; ++iter)
			FindFilesRecursively(*iter, callback);
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

	std::wstring modulePathWS; std::copy(modulePath.begin(), modulePath.end(), std::back_inserter(modulePathWS));
	FindFilesRecursively(modulePathWS, cb);
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

std::wstring GetLongestCommonSubstring(const std::wstring & first, const std::wstring & second)
{
	auto findSubstrings = [](const std::wstring& word, std::set<std::wstring>& substrings)->void
	{
		int l = word.length();
		for (int start = 0; start < l; start++) {
			for (int length = 1; length < l - start + 1; length++) {
				substrings.insert(word.substr(start, length));
			}
		}
	};

	std::set<std::wstring> firstSubstrings, secondSubstrings;
	findSubstrings(first, firstSubstrings);
	findSubstrings(second, secondSubstrings);
	std::set<std::wstring> common;
	std::set_intersection(firstSubstrings.begin(), firstSubstrings.end(), secondSubstrings.begin(), secondSubstrings.end(), std::inserter(common, common.begin()));
	std::vector<std::wstring> commonSubs(common.begin(), common.end());
	std::sort(commonSubs.begin(), commonSubs.end(), [](const std::wstring &s1, const std::wstring &s2) { return s1.length() > s2.length(); });
	return *(commonSubs.begin());
}

size_t find_nth(const std::string& haystack, size_t pos, const std::string& needle, size_t nth)
{
	size_t found_pos = haystack.find(needle, pos);
	if (0 == nth || std::string::npos == found_pos)  return found_pos;
	return find_nth(haystack, found_pos + 1, needle, nth - 1);
}

std::tuple<int32_t, std::string, std::string, std::string> GetRemoteFileInfo(std::wstring strFileName, std::wstring strExtension)
{
	auto pos = strFileName.find_last_of('.');
	if (pos != std::string::npos)
		strFileName.erase(pos);
	strFileName.append(L".zip");

	for (size_t iniCount = 0;; iniCount++)
	{
		std::string strURL("Url");
		std::string strToken("Token");

		if (iniCount > 0)
		{
			strURL = "Url" + std::to_string(iniCount);
			strToken = "Token" + std::to_string(iniCount);

			if (std::string(iniReader.ReadString("API", (char*)strURL.c_str(), "")).empty())
				break;
		}

		std::string szUrl = std::string(iniReader.ReadString("API", (char*)&strURL[0], (iniCount == 0) ? "https://api.github.com/repos/ThirteenAG/WidescreenFixesPack/releases?access_token=63c1f1cc5782c8f1dafad05448e308f0cf8c9198&per_page=100" : ""));

		if (szUrl.find("api.github.com") != std::string::npos)
		{
			std::cout << "Connecting to GitHub..." << std::endl;
			auto r = cpr::Get(cpr::Url{ szUrl });

			if (r.status_code == 200)
			{
				Json::Value parsedFromString;
				Json::Reader reader;
				bool parsingSuccessful = reader.parse(r.text, parsedFromString);

				if (parsingSuccessful)
				{
					std::cout << "GitHub's response parsed successfully." << std::endl;

					for (Json::ValueConstIterator it = parsedFromString.begin(); it != parsedFromString.end(); ++it)
					{
						const Json::Value& wsFix = *it;

						for (size_t i = 0; i < wsFix["assets"].size(); i++)
						{
							std::string str1, str2;
							std::string name(wsFix["assets"][i]["name"].asString());

							std::transform(strFileName.begin(), strFileName.end(), std::back_inserter(str1), ::tolower);
							std::transform(name.begin(), name.end(), std::back_inserter(str2), ::tolower);

							if (str1 == str2)
							{
								std::cout << "Found " << wsFix["assets"][i]["name"] << "on github" << std::endl;
								auto szDownloadURL = wsFix["assets"][i]["browser_download_url"].asString();
								auto szDownloadName = wsFix["assets"][i]["name"].asString();
								auto szFileSize = wsFix["assets"][i]["size"].asString();

								using namespace date;
								int32_t y, m, d; // "updated_at": "2016-08-16T11:42:53Z"
								sscanf_s(wsFix["assets"][i]["updated_at"].asCString(), "%d-%d-%d%*s", &y, &m, &d);
								auto nRemoteFileUpdateTime = date::year(y) / date::month(m) / date::day(d);
								auto now = floor<days>(std::chrono::system_clock::now());
								return std::make_tuple((sys_days{ now } -sys_days{ nRemoteFileUpdateTime }).count(), szDownloadURL, szDownloadName, szFileSize);
							}
						}
					}
					std::cout << "Nothing is found on GitHub." << std::endl;
				}
			}
			else
			{
				std::cout << "Something wrong! " << "Status code: " << r.status_code << std::endl;
			}
		}
		else
		{
			if (szUrl.find("node-js-geturl") != std::string::npos)
			{
				auto r = cpr::Get(cpr::Url{ szUrl });
				
				if (r.text.empty())
					continue;

				if (r.text.at(0) == '.' || r.text.at(0) == '/')
				{
					auto sstr1 = std::string("?url=(");
					auto sstr2 = szUrl.rfind(sstr1) + sstr1.length();
					szUrl = szUrl.substr(sstr2);
					szUrl = szUrl.substr(0, find_nth(szUrl, 0, std::string("/"), 2));
					szUrl += r.text.substr(r.text.find_first_of('/'));
				}
				else
					szUrl = r.text;
			}

			std::cout << "Connecting to " << szUrl << std::endl;

			auto rHead = cpr::Head(cpr::Url{ szUrl });

			std::cout << "Found " << rHead.header["Content-Type"] << std::endl;
			std::string szDownloadURL = rHead.url.c_str();
			std::string tempStr = szDownloadURL.substr(szDownloadURL.find_last_of('/') + 1);
			auto endPos = tempStr.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567890_.");
			std::string szDownloadName = tempStr.substr(0, (endPos == 0) ? std::string::npos : endPos);
			std::string szFileSize = rHead.header["Content-Length"];

			std::wstring str1, str2;
			std::transform(strFileName.begin(), strFileName.end(), std::back_inserter(str1), ::tolower);
			std::transform(szDownloadName.begin(), szDownloadName.end(), std::back_inserter(str2), ::tolower);
			str1 = str1.substr(0, str1.find_last_of(L"."));
			str2 = str2.substr(0, str2.find_last_of(L"."));

			auto lcs = GetLongestCommonSubstring(str1, str2);

			if (lcs.length() >= std::string("skygfx").length())
			{
				std::tm t;
				std::istringstream ss(rHead.header["Last-Modified"]); // Wed, 27 Jul 2016 18:43:42 GMT
				ss >> std::get_time(&t, "%a, %d %b %Y %H:%M:%S %Z");

				using namespace date;
				auto nRemoteFileUpdateTime = date::year(t.tm_year + 1900) / date::month(t.tm_mon + 1) / date::day(t.tm_mday);
				auto now = floor<days>(std::chrono::system_clock::now());
				return std::make_tuple((sys_days{ now } -sys_days{ nRemoteFileUpdateTime }).count(), szDownloadURL, szDownloadName, szFileSize);
			}
			else
			{
				std::wcout << L"Seems like this archive doesn't contain " << strFileName.substr(0, strFileName.find_last_of(L".")) << strExtension << std::endl;
			}
		}
	}
	return std::make_tuple(-1, "", "", "");
}

void UpdateFile(std::wstring wzsFileName, std::wstring wszFullFilePath, std::wstring wszDownloadURL, std::wstring wszDownloadName, bool bCheckboxChecked)
{
	messagesBuffer = L"Downloading " + wszDownloadName;
	std::wcout << messagesBuffer << std::endl;

	std::string szDownloadURL; std::copy(wszDownloadURL.begin(), wszDownloadURL.end(), std::back_inserter(szDownloadURL));

	auto r = cpr::Get(cpr::Url{ szDownloadURL });
	
	if (r.status_code == 200)
	{
		std::vector<uint8_t> buffer(r.text.begin(), r.text.end());

		messagesBuffer = L"Download complete.";
		std::wcout << messagesBuffer << std::endl;

		using namespace zipper;
		Unzipper unzipper(buffer);
		std::vector<ZipEntry> entries = unzipper.entries();
		std::wstring szFilePath, szFileName;

		auto itr = std::find_if(entries.begin(), entries.end(), [&wzsFileName, &szFilePath, &szFileName](auto &s)
		{
			auto s1 = s.name.substr(0, s.name.find_first_of('/') + 1);
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
			std::copy(s1.begin(), s1.end(), std::back_inserter(szFilePath));
			std::copy(s2.begin(), s2.end(), std::back_inserter(szFileName));
			return true;
		});

		if (itr != entries.end())
		{
			messagesBuffer = L"Processing " + wszDownloadName;
			std::wcout << messagesBuffer << std::endl;
		
			for (auto it = entries.begin(); it != entries.end(); it++)
			{
				std::wstring lowcsIt, lowcsFilePath, lowcsFileName, itFileName;
				std::transform(it->name.begin(), it->name.end(), std::back_inserter(lowcsIt), ::tolower);
				std::transform(szFilePath.begin(), szFilePath.end(), std::back_inserter(lowcsFilePath), ::tolower);
				std::transform(szFileName.begin(), szFileName.end(), std::back_inserter(lowcsFileName), ::tolower);
				std::copy(it->name.begin(), it->name.end(), std::back_inserter(itFileName));
				std::replace(itFileName.begin(), itFileName.end(), '/', '\\');
				itFileName.erase(0, szFilePath.length());
		
				if (!itFileName.empty() && (itFileName.back() != L'\\'))
				{
					if ((!bCheckboxChecked && lowcsIt.find(lowcsFileName) != std::string::npos) || (bCheckboxChecked && lowcsIt.find(lowcsFilePath) != std::string::npos))
					{
						std::wstring fullPath = wszFullFilePath.substr(0, wszFullFilePath.find_last_of('\\') + 1) + itFileName;
						std::string fullPathStr; std::copy(fullPath.begin(), fullPath.end(), std::back_inserter(fullPathStr));
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
							std::wcout << itFileName << L" is not locked. Overwriting..." << std::endl;
						}

						std::ofstream outputFile(fullPath, std::ios::binary);
						unzipper.extractEntryToStream(it->name, outputFile);
						outputFile.close();

						messagesBuffer = itFileName + L" was updated succesfully.";
						std::wcout << messagesBuffer << std::endl;
					}
				}
			}
		}
		unzipper.close();
	}
}

void ShowUpdateDialog()
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

	auto formatBytes = [](int32_t bytes, int32_t precision = 2)->std::string
	{
		if (bytes == 0)
			return std::string("0 Bytes");

		auto k = 1000;
		char* sizes[] = { "Bytes", "KB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB" };
		size_t i = (size_t)floor(log(bytes) / log(k));
		std::ostringstream out;
		out << std::fixed << std::setprecision(precision) << (bytes / pow(k, i));
		return std::string(out.str() + ' ' + sizes[i]);
	};

	auto formatBytesW = [&formatBytes](int32_t bytes, int32_t precision = 2)->std::wstring
	{
		auto str = formatBytes(bytes, precision);
		std::wstring ret; std::copy(str.begin(), str.end(), std::back_inserter(ret));
		return ret;
	};

	TASKDIALOGCONFIG tdc = { sizeof(TASKDIALOGCONFIG) };
	auto szTitle = L"modupdater";
	auto szCheckboxText = L"Update all downloaded files.";
	auto szHeader = std::wstring(L"Updates available:\n");
	std::wstring szBodyText;

	int32_t nTotalUpdateSize = 0;

	for (auto &it : FilesToUpdate)
	{
		szBodyText += L"<a href=\"" + it.wszDownloadURL + L"\">" + it.wszFileName + L"</a> ";
		//szBodyText += L"\n";
		szBodyText += L"(" + it.wszDownloadName + L" / " + formatBytesW(it.nFileSize) + L")" L"\n";
		szBodyText += L"Remote file updated " + std::to_wstring(it.nRemoteFileUpdatedDaysAgo) + L" days ago." L"\n";
		szBodyText += L"Local file updated " + std::to_wstring(it.nLocaFileUpdatedDaysAgo) + L" days ago." L"\n";
		szBodyText += L"\n";
		nTotalUpdateSize += it.nFileSize;
	}
	szBodyText += L"Do you want to download these updates?";

	auto szButton1Text = std::wstring(L"Download and install updates now\n" + formatBytesW(nTotalUpdateSize));

	std::string szURL = std::string(iniReader.ReadString("FILE", "WebUrl", "https://thirteenag.github.io/wfp"));
	auto szFooter = std::string("<a href=\"" + szURL + "\">" + szURL + "</a>");
	std::wstring szFooterws; std::copy(szFooter.begin(), szFooter.end(), std::back_inserter(szFooterws));
	
	TASKDIALOG_BUTTON aCustomButtons[] = {
		{ BUTTONID1, szButton1Text.c_str() },
		{ BUTTONID2, L"Cancel" },
	};
	
	tdc.hwndParent = NULL;
	tdc.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_USE_COMMAND_LINKS | TDF_ENABLE_HYPERLINKS | TDF_SIZE_TO_CONTENT | TDF_CAN_BE_MINIMIZED;
	tdc.pButtons = aCustomButtons;
	tdc.cButtons = _countof(aCustomButtons);
	//tdc.pszMainIcon = TD_WARNING_ICON;
	tdc.pszWindowTitle = szTitle;
	tdc.pszMainInstruction = szHeader.c_str();
	tdc.pszContent = szBodyText.c_str();
	tdc.pszVerificationText = szCheckboxText;
	tdc.pszFooter = szFooterws.c_str();
	tdc.pszFooterIcon = TD_INFORMATION_ICON;
	tdc.pfCallback = TaskDialogCallbackProc;
	tdc.lpCallbackData = 0;
	auto nClickedBtnID = -1;
	auto bCheckboxChecked = 0;
	auto hr = TaskDialogIndirect(&tdc, &nClickedBtnID, nullptr, &bCheckboxChecked);

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
		tdc.lpCallbackData = 1;

		bool bNotCanceledorError = false;
		std::thread t([&bCheckboxChecked, &bNotCanceledorError]
		{
			for (auto &it : FilesToUpdate)
			{
				UpdateFile(it.wszFileName, it.wszFullFilePath, it.wszDownloadURL, it.wszDownloadName, bCheckboxChecked != 0);
			}
			SendMessage(DialogHwnd, TDM_CLICK_BUTTON, static_cast<WPARAM>(TDCBF_OK_BUTTON), 0);
			bNotCanceledorError = true;
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
					SHELLEXECUTEINFOW ShExecInfo = { 0 };
					ShExecInfo.cbSize = sizeof(SHELLEXECUTEINFOW);
					ShExecInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
					ShExecInfo.hwnd = NULL;
					ShExecInfo.lpVerb = NULL;
					ShExecInfo.lpFile = processPath.c_str();
					ShExecInfo.lpParameters = L"";
					ShExecInfo.lpDirectory = NULL;
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
						SwitchToThisWindow(MainHwnd, TRUE);
					}
				}
			}
			else
			{
				std::wcout << L"Update cancelled or error occured." << std::endl;
			}
		}
	}
	else
	{
		std::wcout << L"Update cancelled." << std::endl;
	}
}

DWORD WINAPI ProcessFiles(LPVOID)
{
	CleanupLockedFiles();

	if (iniReader.ReadInteger("MISC", "OutputLogToFile", 0) != 0)
	{
		std::ofstream out(modulePath + L"modupdater");
		std::cout.rdbuf(out.rdbuf());
		std::wcout << "Current directory: " << modulePath << std::endl;
	}

	bool bAlwaysUpdate = iniReader.ReadInteger("DEBUG", "AlwaysUpdate", 0) != 0;
	std::string nameStr = std::string(iniReader.ReadString("FILE", "NameRegExp", ".*\\.WidescreenFix"));
	std::string extStr = std::string(iniReader.ReadString("FILE", "Extension", "asi"));

	std::wstring name; std::copy(nameStr.begin(), nameStr.end(), std::back_inserter(name));
	std::wstring ext;  std::copy(extStr.begin(), extStr.end(), std::back_inserter(ext));

	std::wstring name_ext(name + L"." + ext);
	std::wregex wregex(name_ext, std::regex::icase);

	auto cb = [&name, &ext, &wregex, &bAlwaysUpdate](std::wstring &s, WIN32_FIND_DATAW &fd)
	{
		static std::wstring const targetExtension(L"." + ext);
		if (s.size() >= targetExtension.size() && std::equal(s.end() - targetExtension.size(), s.end(), targetExtension.begin()))
		{
			auto strFileName = s.substr(s.rfind('\\') + 1);
			auto nameCheck = strFileName.substr(0, s.rfind('.'));

			if (std::regex_match(strFileName, wregex))
			{
				std::wcout << strFileName << " " << "found." << std::endl;
				int32_t nLocaFileUpdatedDaysAgo = GetLocalFileInfo(fd.ftCreationTime, fd.ftLastAccessTime, fd.ftLastWriteTime);
				auto RemoteInfo = GetRemoteFileInfo(strFileName, ext);
				auto nRemoteFileUpdatedDaysAgo = std::get<0>(RemoteInfo);
				auto szDownloadURL = std::get<1>(RemoteInfo);
				auto szDownloadName = std::get<2>(RemoteInfo);
				auto szFileSize = std::get<3>(RemoteInfo);

				if (nRemoteFileUpdatedDaysAgo != -1 && !szDownloadURL.empty())
				{
					if (nRemoteFileUpdatedDaysAgo < nLocaFileUpdatedDaysAgo || bAlwaysUpdate)
					{
						auto nFileSize = std::stoi(szFileSize);
						std::cout << "Download link: " << szDownloadURL << std::endl;
						std::cout << "Remote file updated " << nRemoteFileUpdatedDaysAgo << " days ago." << std::endl;
						std::cout << "Local file updated " << nLocaFileUpdatedDaysAgo << " days ago." << std::endl;
						std::cout << "File size: " << nFileSize << "KB." << std::endl;

						FileUpdateInfo fui;
						fui.wszFullFilePath = s;
						std::copy(strFileName.begin(), strFileName.end(), std::back_inserter(fui.wszFileName));
						std::copy(szDownloadURL.begin(), szDownloadURL.end(), std::back_inserter(fui.wszDownloadURL));
						std::copy(szDownloadName.begin(), szDownloadName.end(), std::back_inserter(fui.wszDownloadName));
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
		}
	};

	std::wstring modulePathWS; std::copy(modulePath.begin(), modulePath.end(), std::back_inserter(modulePathWS));
	FindFilesRecursively(modulePathWS, cb);

	if (!FilesToUpdate.empty())
		ShowUpdateDialog();
	else
		std::wcout << L"No files found to process." << std::endl;

	return 0;
}

void Init()
{
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
	EnumWindows(EnumWindowsProc, GetCurrentProcessId());

	wchar_t buffer[MAX_PATH];
	HMODULE hm = NULL;
	if (!GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&Init, &hm))
		std::wcout << L"GetModuleHandle returned " << GetLastError() << std::endl;
	GetModuleFileNameW(hm, buffer, sizeof(buffer));
	modulePath = std::wstring(buffer).substr(0, std::wstring(buffer).rfind('\\') + 1);
	GetModuleFileNameW(NULL, buffer, sizeof(buffer));
	processPath = std::wstring(buffer);

	iniReader.SetIniPath();

	if (iniReader.ReadInteger("MISC", "ScanFromRootFolder", 0) != 0)
		modulePath = processPath.substr(0, processPath.rfind('\\') + 1);

	CreateThread(0, 0, (LPTHREAD_START_ROUTINE)&ProcessFiles, 0, 0, NULL);

	std::getchar();
}

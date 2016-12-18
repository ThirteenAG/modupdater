#include "stdafx.h"

CIniReader iniReader;
std::string oldDir;
HWND DialogHwnd;
#define BUTTONID1 1001
#define BUTTONID2 1002

int32_t GetLocalFileInfo(FILETIME ftCreate, FILETIME ftLastAccess, FILETIME ftLastWrite)
{
	using namespace date;
	SYSTEMTIME stUTC;
	FileTimeToSystemTime(&ftLastWrite, &stUTC);
	auto nLocalFileUpdateTime = date::year(stUTC.wYear) / date::month(stUTC.wMonth) / date::day(stUTC.wDay);
	auto now = floor<days>(std::chrono::system_clock::now());
	return (sys_days{ now } - sys_days{ nLocalFileUpdateTime }).count();
}

std::tuple<int32_t, std::string, std::string, std::string> GetRemoteFileInfo(std::string strFileName, std::string strExtension)
{
	strFileName.erase(strFileName.find_last_of('.'));
	strFileName.append(".zip");

	std::string szUrl = std::string(iniReader.ReadString("API", "URL", "https://api.github.com/repos/ThirteenAG/WidescreenFixesPack/releases"));
	std::string szToken = std::string(iniReader.ReadString("API", "Token", "63c1f1cc5782c8f1dafad05448e308f0cf8c9198"));
	std::string szPerPage = std::string(iniReader.ReadString("API", "PerPage", "100"));

	std::cout << "Connecting to GitHub..." << std::endl;

	auto r = cpr::Get(cpr::Url{ szUrl + "?access_token=" + szToken + "&per_page=" + szPerPage });
	
	if (r.status_code == 200)
	{
		Json::Value parsedFromString;
		Json::Reader reader;
		bool parsingSuccessful = reader.parse(r.text, parsedFromString);
	
		if (parsingSuccessful)
		{
			std::cout << "GitHub's response parsed succesfully." << std::endl;
	
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
						return std::make_tuple((sys_days{ now } - sys_days{ nRemoteFileUpdateTime }).count(), szDownloadURL, szDownloadName, szFileSize);
					}
				}
			}
		}
	}
	else
	{
		std::cout << "Something wrong! " << "Status code: " << r.status_code << std::endl;
	}
	return std::make_tuple(-1, "", "", "");
}

void UpdateFile(std::string strFileName, std::string szDownloadURL, std::string szDownloadName, bool bCheckboxChecked)
{
	bool bSuccess = false;
	std::cout << "Downloading " + szDownloadName << std::endl;

	auto r = cpr::Get(cpr::Url{ szDownloadURL });

	if (r.status_code == 200)
	{
		std::ofstream DownloadedZip(szDownloadName, std::ios::binary | std::ios::out);
		DownloadedZip << r.text;
		DownloadedZip.close();
		std::cout << "Download complete." << std::endl;

		ziputils::unzipper zipFile;
		zipFile.open(szDownloadName.c_str());
		auto filenames = zipFile.getFilenames();
		std::string szFilePath, szFileName;

		auto itr = std::find_if(filenames.begin(), filenames.end(),	[&](auto &s) 
		{
			auto s1 = s.substr(0, s.rfind('/') + 1);
			auto s2 = s.substr(s.rfind('/') + 1);

			if (s2.size() != strFileName.size())
				return false;
			for (size_t i = 0; i < s2.size(); ++i)
				if (::tolower(s2[i]) == ::tolower(strFileName[i]))
				{
					szFilePath = s1;
					szFileName = s2;
					return true;
				}
			return false;
		});
		
		if (itr != filenames.end()) 
		{
			std::cout << "Processing archive..." << std::endl;

			for (auto it = filenames.begin(); it != filenames.end(); it++)
			{
				std::string lowcsIt, lowcsFilePath, lowcsFileName, itFileName;
				std::transform(it->begin(), it->end(), std::back_inserter(lowcsIt), ::tolower);
				std::transform(szFilePath.begin(), szFilePath.end(), std::back_inserter(lowcsFilePath), ::tolower);
				std::transform(szFileName.begin(), szFileName.end(), std::back_inserter(lowcsFileName), ::tolower);
				itFileName = it->substr(it->rfind('/') + 1);

				if ((!bCheckboxChecked && lowcsIt.find(lowcsFileName) != std::string::npos) || (bCheckboxChecked && lowcsIt.find(lowcsFilePath) != std::string::npos))
				{
					//zipFile.openEntry(it->c_str());
					//std::ofstream outputFile(itFileName, std::ios::binary);
					//zipFile >> outputFile;
					std::cout << itFileName << " was updated succesfully." << std::endl;
					bSuccess = true;
				}
			}
		}
	}

	if (bSuccess)
		SendMessage(DialogHwnd, TDM_UPDATE_ELEMENT_TEXT, TDE_MAIN_INSTRUCTION, (LPARAM)L"Update completed succesfully.");
	else
		SendMessage(DialogHwnd, TDM_UPDATE_ELEMENT_TEXT, TDE_MAIN_INSTRUCTION, (LPARAM)L"An error occured during the update.");
}

void ShowUpdateDialog(std::string strFileName, std::string szDownloadURL, std::string szDownloadName, int32_t nRemoteFileUpdatedDaysAgo, int32_t nLocaFileUpdatedDaysAgo, int32_t nFileSize)
{
	auto TaskDialogCallbackProc = [](HWND hwnd, UINT uNotification, WPARAM wParam, LPARAM lParam, LONG_PTR dwRefData)->HRESULT
	{
		switch (uNotification)
		{
		case TDN_DIALOG_CONSTRUCTED:
			DialogHwnd = hwnd;
			SendMessage(hwnd, TDM_SET_MARQUEE_PROGRESS_BAR, 1, 0);
			SendMessage(hwnd, TDM_SET_PROGRESS_BAR_MARQUEE, 1, 0);
			break;
		case TDN_BUTTON_CLICKED:
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

	TASKDIALOGCONFIG tdc = { sizeof(TASKDIALOGCONFIG) };
	auto szTitle = L"WFP.Updater";
	auto szCheckboxText = L"Update all downloaded files";
	auto szHeader = std::string("An update for " + strFileName + " is available");
	auto szBodyText = std::string("Local file updated " + std::to_string(nLocaFileUpdatedDaysAgo) + " days ago.\n" + 
		"Remote file updated " + std::to_string(nRemoteFileUpdatedDaysAgo) + " days ago.\n" + "\nDo you want to download this update?");
	auto szButton1Text = std::string("Download and install the update now\n" + szDownloadName + " / " + formatBytes(nFileSize));
	std::string szURL = std::string(iniReader.ReadString("FILE", "WebUrl", "https://thirteenag.github.io/wfp"));
	auto szFooter = std::string("<a href=\"" + szURL + "\">"+ szURL + "</a>");

	std::wstring szHeaderws; std::copy(szHeader.begin(), szHeader.end(), std::back_inserter(szHeaderws));
	std::wstring szBodyTextws; std::copy(szBodyText.begin(), szBodyText.end(), std::back_inserter(szBodyTextws));
	std::wstring szButton1ws; std::copy(szButton1Text.begin(), szButton1Text.end(), std::back_inserter(szButton1ws));
	std::wstring szFooterws; std::copy(szFooter.begin(), szFooter.end(), std::back_inserter(szFooterws));

	TASKDIALOG_BUTTON aCustomButtons[] = {
		{ BUTTONID1, szButton1ws.c_str() },
		{ BUTTONID2, L"Do not download the update." },
	};

	tdc.hwndParent = NULL;
	tdc.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_USE_COMMAND_LINKS | TDF_ENABLE_HYPERLINKS;
	tdc.pButtons = aCustomButtons;
	tdc.cButtons = _countof(aCustomButtons);
	//tdc.pszMainIcon = TD_INFORMATION_ICON;
	tdc.pszWindowTitle = szTitle;
	tdc.pszMainInstruction = szHeaderws.c_str();
	tdc.pszContent = szBodyTextws.c_str();
	tdc.pszVerificationText = szCheckboxText;
	tdc.pszFooter = szFooterws.c_str();
	tdc.pszFooterIcon = TD_INFORMATION_ICON;
	tdc.pfCallback = TaskDialogCallbackProc;
	auto nClickedBtnID = -1;
	auto bCheckboxChecked = 0;
	auto hr = TaskDialogIndirect(&tdc, &nClickedBtnID, nullptr, &bCheckboxChecked);

	if (SUCCEEDED(hr) && nClickedBtnID == BUTTONID1)
	{
		tdc.pButtons = NULL;
		tdc.cButtons = 0;
		//tdc.pszMainIcon = TD_INFORMATION_ICON;
		tdc.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_SHOW_MARQUEE_PROGRESS_BAR | TDF_ENABLE_HYPERLINKS;
		tdc.pszMainInstruction = L"Downloading Update...";
		tdc.pszContent = L"";
		tdc.pszVerificationText = L"";

		bool bSkipUpdateCompleteDialog = iniReader.ReadInteger("MISC", "SkipUpdateCompleteDialog", 0) != 0;

		std::thread t([&strFileName, &szDownloadURL, &szDownloadName, &bCheckboxChecked, &bSkipUpdateCompleteDialog]
		{
			UpdateFile(strFileName, szDownloadURL, szDownloadName, bCheckboxChecked != 0);
			SendMessage(DialogHwnd, TDM_SET_MARQUEE_PROGRESS_BAR, FALSE, 0);
			SendMessage(DialogHwnd, TDM_SET_PROGRESS_BAR_POS, 100, 0);
			//SendMessage(DialogHwnd, TDM_UPDATE_ELEMENT_TEXT, TDE_CONTENT, (LPARAM)L"._.");
			if (bSkipUpdateCompleteDialog)
				SendMessage(DialogHwnd, TDM_CLICK_BUTTON, static_cast<WPARAM>(TDCBF_OK_BUTTON), 0);
		});

		hr = TaskDialogIndirect(&tdc, nullptr, nullptr, nullptr);

		if (SUCCEEDED(hr))
		{
			t.join();
		}
	}
	else
	{
		std::cout << "Update cancelled." << std::endl;
	}
}

bool ProcessFiles()
{
	bool bRes = false;
	std::string name = std::string(iniReader.ReadString("FILE", "Name", ".*\\.WidescreenFix"));
	std::string ext = std::string(iniReader.ReadString("FILE", "Extension", "asi"));

	WIN32_FIND_DATA fd;
	HANDLE asiFile = FindFirstFile(std::string("*." + ext).c_str(), &fd);
	if (asiFile != INVALID_HANDLE_VALUE)
	{
		do 
		{
			if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) 
			{
				auto strFileName = std::string(fd.cFileName);
				std::regex regex(name + "." + ext, std::regex::icase);

				if (std::regex_match(strFileName, regex))
				{
					bRes = true;
					std::cout << fd.cFileName << " " << "found." << std::endl;
					int32_t nLocaFileUpdatedDaysAgo = GetLocalFileInfo(fd.ftCreationTime, fd.ftLastAccessTime, fd.ftLastWriteTime);
					auto RemoteInfo = GetRemoteFileInfo(strFileName, ext);
					auto nRemoteFileUpdatedDaysAgo = std::get<0>(RemoteInfo);
					auto szDownloadURL = std::get<1>(RemoteInfo);
					auto szDownloadName = std::get<2>(RemoteInfo);
					auto szFileSize = std::get<3>(RemoteInfo);

					if (nRemoteFileUpdatedDaysAgo != -1 && !szDownloadURL.empty())
					{
						if (nRemoteFileUpdatedDaysAgo < nLocaFileUpdatedDaysAgo)
						{
							auto nFileSize = std::stoi(szFileSize);
							std::cout << "Download link: " << szDownloadURL << std::endl;
							std::cout << "Remote file updated " << nRemoteFileUpdatedDaysAgo << " days ago." << std::endl;
							std::cout << "Local file updated " << nLocaFileUpdatedDaysAgo << " days ago." << std::endl;
							std::cout << "File size: " << nFileSize << "KB." << std::endl;

							ShowUpdateDialog(strFileName, szDownloadURL, szDownloadName, nRemoteFileUpdatedDaysAgo, nLocaFileUpdatedDaysAgo, nFileSize);
						}
						else
						{
							std::cout << "No updates available." << std::endl;
							//MessageBox(NULL, "No updates available.", "WFP.Updater", MB_OK | MB_ICONINFORMATION);
						}
					}
					else
					{
						std::cout << "Error." << std::endl;
					}
					std::cout << std::endl;
				}
			}

		} while (FindNextFile(asiFile, &fd));
		FindClose(asiFile);
	}
	return bRes;
}

void Init()
{
	char buffer[MAX_PATH];
	GetCurrentDirectory(MAX_PATH, buffer);
	oldDir = std::string(buffer);
	GetModuleFileName(NULL, buffer, MAX_PATH);
	std::string modulePath = std::string(buffer).substr(0, std::string(buffer).rfind('\\') + 1);
	SetCurrentDirectory(modulePath.c_str());
	//std::cout << "Current directory: " << modulePath << std::endl;

	iniReader.SetIniPath();

	if (!ProcessFiles())
	{
		std::cout << "No files found to process." << std::endl;
		//return;
	}

	SetCurrentDirectory(oldDir.c_str());
	std::getchar();
}

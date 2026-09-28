#include "stdafx.h"
#include "installer.h"
#include "archive.h"
#include "fileops.h"
#include "gamepaths.h"
#include "http.h"
#include "log.h"
#include "registry.h"
#include "remote.h"
#include "string_funcs.h"
#include "ui_common.h"
#include <shellapi.h>
#include <shobjidl.h>
#include <tlhelp32.h>

namespace mu::installer
{
    namespace
    {
        const wchar_t* const kDefaultStrings[MU_STR_COUNT] =
        {
            L"{title}",
            L"Install",
            L"Browse for another folder...",
            L"Select Installation Folder",
            L"Choose an installation folder...",
            L"Keep my current settings",
            L"INI files: replace all and keep settings",
            L"INI files: replace all and discard settings",
            L"INI files: don't replace",
            L"{exe} was not found in this folder",
            L"{exe} was not found in the selected folder:\n{path}\n\nInstall anyway?",
            L"Administrator permissions required",
            L"The installer does not have permission to write to the selected folder:\n\n{path}\n\nRestart the installer with administrator privileges to continue.",
            L"Restart with administrator privileges",
            L"Installing {title}...",
            L"Preparing...",
            L"Downloading {file}...",
            L"Installing files...",
            L"Cancel",
            L"Cancelling...",
            L"Do you want to cancel the installation?",
            L"Installation Complete",
            L"{title} has been installed to:\n{path}",
            L"Installation Failed",
            L"Installation Cancelled",
            L"Close",
            L"Try again",
            L"Launch {exe}",
            L"Open installation folder",
            L"View log",
            L"{exe} is running. Close the game before installing.",
            L"Install anyway",
            L"{exe} not found",
        };

        // defaults that would read oddly without a title
        const wchar_t* DefaultWithoutTitle(int id)
        {
            switch (id)
            {
            case MU_STR_HEADING: return L"";
            case MU_STR_INSTALLING: return L"Installing...";
            case MU_STR_COMPLETE_TEXT: return L"The installation has finished successfully.\n{path}";
            }
            return nullptr;
        }

        std::vector<std::wstring> Split(const std::wstring& text, const wchar_t* separators)
        {
            std::vector<std::wstring> result;
            size_t start = 0;
            while (start <= text.size())
            {
                auto end = text.find_first_of(separators, start);
                if (end == std::wstring::npos)
                    end = text.size();
                auto item = trimString(text.substr(start, end - start));
                if (!item.empty())
                    result.push_back(item);
                start = end + 1;
            }
            return result;
        }

        std::wstring ExpandEnvironment(const std::wstring& text)
        {
            DWORD size = ExpandEnvironmentStringsW(text.c_str(), nullptr, 0);
            if (!size)
                return text;
            std::wstring result(size, L'\0');
            ExpandEnvironmentStringsW(text.c_str(), result.data(), size);
            result.resize(wcsnlen(result.c_str(), result.size()));
            return result;
        }

        // Quotes an argument following the rules of CommandLineToArgvW
        std::wstring QuoteArgument(const std::wstring& arg)
        {
            if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos)
                return arg;

            std::wstring quoted = L"\"";
            size_t backslashes = 0;
            for (wchar_t c : arg)
            {
                if (c == L'\\')
                {
                    backslashes++;
                }
                else if (c == L'"')
                {
                    quoted.append(backslashes * 2 + 1, L'\\');
                    backslashes = 0;
                }
                else
                {
                    backslashes = 0;
                }
                quoted.push_back(c);
            }
            quoted.append(backslashes, L'\\');
            quoted.push_back(L'"');
            return quoted;
        }

        std::vector<std::wstring> GetArguments()
        {
            std::vector<std::wstring> args;
            int argc = 0;
            if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc))
            {
                for (int i = 0; i < argc; i++)
                    args.push_back(argv[i]);
                LocalFree(argv);
            }
            return args;
        }

        bool HasZipSignature(const std::filesystem::path& file)
        {
            std::ifstream stream(file, std::ios::binary);
            uint32_t signature = 0;
            stream.read(reinterpret_cast<char*>(&signature), sizeof(signature));
            return stream && (signature == 0x04034B50 || signature == 0x06054B50);
        }

        std::wstring DescribeErrors(const std::vector<std::wstring>& errors)
        {
            if (errors.size() == 1)
                return errors.front();
            return Format(L"{} files could not be installed.", errors.size());
        }

        bool IsInUseError(const std::wstring& error)
        {
            return error.find(L"(error 32)") != std::wstring::npos || error.find(L"(error 33)") != std::wstring::npos;
        }

        // A zip dropped onto the installer in Explorer: there is no console to report to
        bool StartedFromExplorer()
        {
            HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (snapshot == INVALID_HANDLE_VALUE)
                return false;

            DWORD parent = 0;
            PROCESSENTRY32W entry = { sizeof(entry) };
            for (BOOL ok = Process32FirstW(snapshot, &entry); ok; ok = Process32NextW(snapshot, &entry))
            {
                if (entry.th32ProcessID == GetCurrentProcessId())
                {
                    parent = entry.th32ParentProcessID;
                    break;
                }
            }

            bool explorer = false;
            entry = { sizeof(entry) };
            for (BOOL ok = Process32FirstW(snapshot, &entry); ok && parent; ok = Process32NextW(snapshot, &entry))
            {
                if (entry.th32ProcessID == parent)
                {
                    explorer = iequals(std::wstring_view(entry.szExeFile), L"explorer.exe");
                    break;
                }
            }
            CloseHandle(snapshot);
            return explorer;
        }
    }

    std::wstring Text(const Config& config, int id, const std::filesystem::path& path, const std::wstring& file, const std::wstring& error, const std::wstring& exe)
    {
        if (id < 0 || id >= MU_STR_COUNT)
            return {};

        std::wstring text;
        if (config.strings[id])
            text = *config.strings[id];
        else if (config.windowTitle.empty() && DefaultWithoutTitle(id))
            text = DefaultWithoutTitle(id);
        else
            text = kDefaultStrings[id];

        auto exeName = !exe.empty() ? exe : config.gameExecutables.empty() ? std::wstring(L"The game") : config.gameExecutables.front();
        string_replace_all(text, std::wstring(L"{title}"), config.windowTitle);
        string_replace_all(text, std::wstring(L"{path}"), path.wstring());
        string_replace_all(text, std::wstring(L"{file}"), file);
        string_replace_all(text, std::wstring(L"{exe}"), exeName);
        string_replace_all(text, std::wstring(L"{error}"), error);
        return text;
    }

    std::wstring WindowTitle(const Config& config)
    {
        return config.windowTitle.empty() ? L"Installer" : config.windowTitle;
    }

    Config LoadConfig(HMODULE module)
    {
        Config config;
        auto snapshot = RegistryGetSnapshot();

        const ModuleSettings* settings = nullptr;
        if (auto it = snapshot.find(module); it != snapshot.end())
        {
            settings = &it->second;
        }
        else
        {
            // settings made by another module of the process
            for (auto& [m, s] : snapshot)
            {
                bool installerSettings = std::any_of(s.begin(), s.end(), [](auto& kv) { return kv.first >= K(Key::InstallerIcon); });
                if (installerSettings || HasValue(s, K(Key::UpdateUrl)))
                {
                    settings = &s;
                    module = m;
                    break;
                }
            }
        }

        config.module = module;
        if (!settings)
            return config;

        auto& s = *settings;
        config.windowTitle = toWString(GetString(s, K(Key::InstallerWindowTitle)));
        config.mainInstruction = toWString(GetString(s, K(Key::InstallerMainInstruction)));
        config.content = toWString(GetString(s, K(Key::InstallerContent)));
        config.footer = toWString(GetString(s, K(Key::InstallerFooter)));
        config.icon = reinterpret_cast<HICON>(static_cast<intptr_t>(GetInt(s, K(Key::InstallerIcon))));
        config.updateUrl = GetString(s, K(Key::UpdateUrl));
        config.password = GetString(s, K(Key::ArchivePassword));
        config.steamAppId = GetString(s, K(Key::SteamAppId));
        config.steamSubfolder = GetString(s, K(Key::SteamSubfolder));
        config.rglAppId = GetString(s, K(Key::RglAppId));
        config.rglSubfolder = GetString(s, K(Key::RglSubfolder));
        config.ui = static_cast<int>(GetInt(s, K(Key::InstallerUI), MU_UI_CLASSIC));
        if (auto v = FindValue(s, K(Key::InstallerLogo)))
            config.logo = v->blob;
        if (auto v = FindValue(s, K(Key::InstallerBackground)))
            config.background = v->blob;
        config.backgroundOverlay = static_cast<int>(GetInt(s, K(Key::InstallerBackgroundOverlay), 70));
        if (auto v = FindValue(s, K(Key::InstallerFontData)))
            config.fontData = v->blob;
        config.fontFamily = toWString(GetString(s, K(Key::InstallerFontFamily)));
        config.width = static_cast<int>(GetInt(s, K(Key::InstallerWidth)));
        config.height = static_cast<int>(GetInt(s, K(Key::InstallerHeight)));

        for (int i = 0; i < MU_COLOR_COUNT; i++)
        {
            if (auto v = FindValue(s, Key::InstallerColorBase + i))
                config.colors[i] = static_cast<COLORREF>(v->num);
            if (auto v = FindValue(s, Key::InstallerLightColorBase + i))
                config.light.colors[i] = static_cast<COLORREF>(v->num);
            if (auto v = FindValue(s, Key::InstallerDarkColorBase + i))
                config.dark.colors[i] = static_cast<COLORREF>(v->num);
        }
        config.theme = static_cast<int>(std::clamp<int64_t>(GetInt(s, K(Key::InstallerTheme), MU_THEME_AUTO), MU_THEME_AUTO, MU_THEME_DARK));
        if (auto v = FindValue(s, K(Key::InstallerLightLogo)))
            config.light.logo = v->blob;
        if (auto v = FindValue(s, K(Key::InstallerDarkLogo)))
            config.dark.logo = v->blob;
        for (int i = 0; i < MU_STR_COUNT; i++)
        {
            if (auto v = FindValue(s, Key::InstallerStringBase + i))
                config.strings[i] = toWString(v->str);
        }

        config.gameExecutables = Split(toWString(GetString(s, K(Key::InstallerGameExecutables))), L";,|");
        for (auto& path : Split(toWString(GetString(s, K(Key::InstallerExtraPaths))), L"\n"))
            config.extraPaths.push_back(ExpandEnvironment(path));

        config.iniMode = static_cast<IniMode>(std::clamp<int64_t>(GetInt(s, K(Key::InstallerIniMode), MU_INI_MERGE), MU_INI_MERGE, MU_INI_SKIP));
        config.iniSelectable = GetInt(s, K(Key::InstallerIniSelectable)) != 0;
        config.logFile = toWString(GetString(s, K(Key::LogFile)));
        return config;
    }

    int ResolveTheme(const Config& config)
    {
        if (config.theme == MU_THEME_LIGHT || config.theme == MU_THEME_DARK)
            return config.theme;
        return ui::IsDarkModeEnabled() ? MU_THEME_DARK : MU_THEME_LIGHT;
    }

    std::filesystem::path FindGameExecutable(const Config& config, const std::filesystem::path& folder)
    {
        for (auto& name : config.gameExecutables)
        {
            auto path = folder / name;
            if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
                return path;
        }
        return {};
    }

    std::wstring LaunchText(const Config& config, const std::filesystem::path& folder)
    {
        auto exe = FindGameExecutable(config, folder);
        return exe.empty() ? std::wstring() : Text(config, MU_STR_LAUNCH, folder, {}, {}, exe.filename().wstring());
    }

    std::vector<Location> DetectLocations(const Config& config)
    {
        std::vector<Location> locations;
        auto add = [&](std::filesystem::path path, const wchar_t* source)
        {
            std::error_code ec;
            if (path.empty() || !std::filesystem::is_directory(path, ec))
                return;
            path = path.lexically_normal();
            for (auto& l : locations)
            {
                if (iequals(l.path.wstring(), path.wstring()) || std::filesystem::equivalent(l.path, path, ec))
                    return;
            }
            Log(L"Found install location {} ({})", path.wstring(), source);
            locations.push_back({ path, source });
        };

        add(FindSteamGame(config.steamAppId, config.steamSubfolder), L"Steam");
        add(FindRockstarGame(config.rglAppId, config.rglSubfolder), L"Rockstar Games Launcher");
        for (auto& path : config.extraPaths)
            add(path, L"");

        // the installer was placed into the game folder
        if (!config.gameExecutables.empty())
        {
            auto dir = GetModuleFilePath(nullptr).parent_path();
            if (!FindGameExecutable(config, dir).empty())
                add(dir, L"");
        }
        return locations;
    }

    std::vector<std::wstring> ShortLocationNames(const std::vector<Location>& locations)
    {
        std::vector<std::vector<std::wstring>> parts;
        for (auto& location : locations)
        {
            std::vector<std::wstring> folders;
            for (auto& part : location.path.relative_path())
            {
                if (!part.empty())
                    folders.push_back(part.wstring());
            }
            parts.push_back(std::move(folders));
        }

        auto ending = [&](size_t index, size_t count)
        {
            auto& folders = parts[index];
            std::wstring name;
            for (size_t i = folders.size() - std::min(count, folders.size()); i < folders.size(); i++)
                name += (name.empty() ? L"" : L"\\") + folders[i];
            return name;
        };

        std::vector<std::wstring> names;
        for (size_t i = 0; i < locations.size(); i++)
        {
            std::wstring name;
            for (size_t count = 1; count <= parts[i].size() && name.empty(); count++)
            {
                auto candidate = ending(i, count);
                bool unique = true;
                for (size_t j = 0; j < locations.size() && unique; j++)
                    unique = j == i || !iequals(ending(j, count), candidate);
                if (unique)
                    name = candidate;
            }
            // a drive root, or the same folders on another drive
            names.push_back(name.empty() ? locations[i].path.wstring() : name);
        }
        return names;
    }

    PathCheck CheckPath(const Config& config, const std::filesystem::path& path)
    {
        PathCheck check;
        check.writable = TestWriteAccess(path);
        if (!config.gameExecutables.empty())
        {
            check.exeName = config.gameExecutables.front();
            check.hasGameExe = !FindGameExecutable(config, path).empty();
        }
        return check;
    }

    std::wstring GetRunningGame(const Config& config, const std::filesystem::path& folder)
    {
        std::wstring running;
        if (!config.gameExecutables.empty())
            IsProcessRunningFrom(folder, config.gameExecutables, &running);
        return running;
    }

    bool ConfirmGameClosed(HWND owner, const Config& config, const std::filesystem::path& folder)
    {
        for (;;)
        {
            auto running = GetRunningGame(config, folder);
            if (running.empty())
                return true;

            auto content = Text(config, MU_STR_GAME_RUNNING, folder, {}, {}, running);
            auto installAnyway = Text(config, MU_STR_INSTALL_ANYWAY);
            auto title = WindowTitle(config);

            TASKDIALOG_BUTTON buttons[] = { { 100, installAnyway.c_str() } };
            TASKDIALOGCONFIG tdc = { sizeof(tdc) };
            tdc.hwndParent = owner;
            tdc.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
            tdc.dwCommonButtons = TDCBF_RETRY_BUTTON | TDCBF_CANCEL_BUTTON;
            tdc.pButtons = buttons;
            tdc.cButtons = _countof(buttons);
            tdc.nDefaultButton = IDRETRY;
            tdc.pszWindowTitle = title.c_str();
            tdc.pszMainIcon = TD_WARNING_ICON;
            tdc.pszMainInstruction = content.c_str();

            int clicked = IDCANCEL;
            if (FAILED(TaskDialogIndirect(&tdc, &clicked, nullptr, nullptr)) || clicked == IDCANCEL)
                return false;
            if (clicked == 100)
                return true;
        }
    }

    bool ConfirmMissingExecutable(HWND owner, const Config& config, const std::filesystem::path& folder, const std::wstring& exeName)
    {
        auto content = Text(config, MU_STR_EXE_MISSING_CONFIRM, folder, {}, {}, exeName);
        auto title = WindowTitle(config);

        TASKDIALOGCONFIG tdc = { sizeof(tdc) };
        tdc.hwndParent = owner;
        tdc.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
        tdc.dwCommonButtons = TDCBF_YES_BUTTON | TDCBF_NO_BUTTON;
        tdc.nDefaultButton = IDNO;
        tdc.pszWindowTitle = title.c_str();
        tdc.pszMainIcon = TD_WARNING_ICON;
        tdc.pszContent = content.c_str();

        int clicked = IDNO;
        return SUCCEEDED(TaskDialogIndirect(&tdc, &clicked, nullptr, nullptr)) && clicked == IDYES;
    }

    std::filesystem::path BrowseForFolder(HWND owner, const Config& config, const std::filesystem::path& initial)
    {
        std::filesystem::path result;
        IFileOpenDialog* dialog = nullptr;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
            return result;

        DWORD options = 0;
        if (SUCCEEDED(dialog->GetOptions(&options)))
            dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR | FOS_PATHMUSTEXIST);

        auto title = Text(config, MU_STR_BROWSE_TITLE);
        dialog->SetTitle(title.c_str());

        std::error_code ec;
        if (!initial.empty() && std::filesystem::is_directory(initial, ec))
        {
            IShellItem* folder = nullptr;
            if (SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(), nullptr, IID_PPV_ARGS(&folder))))
            {
                dialog->SetFolder(folder);
                folder->Release();
            }
        }

        if (SUCCEEDED(dialog->Show(owner)))
        {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item)))
            {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
                {
                    result = path;
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        dialog->Release();
        return result;
    }

    CommandLine ParseCommandLine()
    {
        CommandLine commandLine;
        auto args = GetArguments();
        for (size_t i = 1; i < args.size(); i++)
        {
            const auto& arg = args[i];
            auto value = [&](std::wstring_view name) -> std::optional<std::wstring>
            {
                if (starts_with(arg, std::wstring(name) + L"=", false))
                    return arg.substr(name.size() + 1);
                if (iequals(arg, name) && i + 1 < args.size())
                    return args[++i];
                return std::nullopt;
            };

            if (auto dir = value(L"--mu-install-dir"))
                commandLine.installDir = *dir;
            else if (iequals(arg, L"--mu-autostart"))
                commandLine.autoStart = true;
            else if (iequals(arg, L"--mu-silent"))
                commandLine.silent = true;
            else if (auto ui = value(L"--mu-ui"))
                commandLine.ui = iequals(*ui, L"modern") ? MU_UI_MODERN : MU_UI_CLASSIC;
            else if (auto ini = value(L"--mu-ini"))
                commandLine.iniMode = iequals(*ini, L"replace") ? IniMode::Replace : iequals(*ini, L"skip") ? IniMode::Skip : IniMode::Merge;
            else if (auto theme = value(L"--mu-theme"))
                commandLine.theme = iequals(*theme, L"light") ? MU_THEME_LIGHT : iequals(*theme, L"dark") ? MU_THEME_DARK : MU_THEME_AUTO;
            else if (auto page = value(L"--mu-screenshot-page"))
                commandLine.screenshotPage = *page;
            else if (auto dpi = value(L"--mu-screenshot-dpi"))
                commandLine.screenshotDpi = static_cast<UINT>(std::clamp(_wtoi(dpi->c_str()), 48, 480));
            else if (auto file = value(L"--mu-screenshot"))
                commandLine.screenshot = *file;
        }
        return commandLine;
    }

    bool RestartElevated(const std::filesystem::path& installDir, IniMode iniMode)
    {
        std::wstring arguments;
        auto args = GetArguments();
        for (size_t i = 1; i < args.size(); i++)
        {
            // drop our own switches, they are added again below
            if (starts_with(args[i], L"--mu-install-dir", false) || starts_with(args[i], L"--mu-autostart", false) || starts_with(args[i], L"--mu-ini", false))
                continue;
            arguments += QuoteArgument(args[i]) + L" ";
        }

        static const wchar_t* iniNames[] = { L"merge", L"replace", L"skip" };
        arguments += L"--mu-install-dir=" + QuoteArgument(installDir.wstring()) + L" --mu-autostart --mu-ini=" + iniNames[static_cast<int>(iniMode)];
        Log(L"Restarting as administrator: {}", arguments);
        return ui::RestartProcess(true, arguments);
    }

    Job::Job(const Config& config, std::filesystem::path target, IniMode iniMode)
        : config(config), target(std::move(target)), iniMode(iniMode)
    {
    }

    Job::~Job()
    {
        cancel = true;
        if (thread.joinable())
            thread.join();
    }

    void Job::Start()
    {
        thread = std::thread([this] { Run(); });
    }

    void Job::Cancel()
    {
        if (!cancel.exchange(true))
            Log(L"Cancelling the installation");
    }

    Progress Job::GetProgress() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return progress;
    }

    Outcome Job::GetOutcome() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return outcome;
    }

    std::wstring Job::StatusText() const
    {
        auto p = GetProgress();
        if (cancel)
            return Text(config, MU_STR_CANCELLING);
        switch (p.phase)
        {
        case Phase::Downloading: return Text(config, MU_STR_DOWNLOADING, target, p.file);
        case Phase::Extracting: return Text(config, MU_STR_EXTRACTING, target, p.file);
        case Phase::Finished: return {};
        default: return Text(config, MU_STR_PREPARING);
        }
    }

    void Job::SetProgress(Phase phase, uint64_t done, uint64_t total, const std::wstring& file)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (phase == Phase::Downloading)
        {
            auto now = GetTickCount64();
            if (progress.phase != Phase::Downloading || done < speedBytes)
            {
                speedTick = now;
                speedBytes = done;
                progress.bytesPerSecond = 0;
            }
            else if (now - speedTick >= 500)
            {
                double current = static_cast<double>(done - speedBytes) * 1000.0 / static_cast<double>(now - speedTick);
                progress.bytesPerSecond = progress.bytesPerSecond > 0 ? progress.bytesPerSecond * 0.7 + current * 0.3 : current;
                speedTick = now;
                speedBytes = done;
            }
        }
        progress.phase = phase;
        progress.done = done;
        progress.total = total;
        if (!file.empty() || phase != Phase::Extracting)
            progress.file = file;
    }

    void Job::Fail(const std::wstring& error, const std::vector<std::wstring>& details)
    {
        std::lock_guard<std::mutex> lock(mutex);
        outcome.result = Result::Failed;
        outcome.error = error;
        outcome.details = details;
        Log(L"Installation failed: {}", error);
        for (auto& d : details)
            Log(L"  {}", d);
    }

    bool Job::Extract(zip::Reader& reader, uint64_t base, uint64_t total, ExtractReport& report)
    {
        ExtractOptions options;
        options.iniMode = iniMode;
        options.password = config.password;
        return ExtractArchive(reader, target, options, [&](const ExtractProgress& p)
        {
            SetProgress(Phase::Extracting, base + p.done, total, p.currentFile);
            return !cancel.load();
        }, report);
    }

    void Job::Run()
    {
        try
        {
            Log(L"Installing to {}", target.wstring());
            SetProgress(Phase::Preparing, 0, 0, {});

            std::error_code ec;
            std::filesystem::create_directories(target, ec);
            CleanupLeftovers(target);

            if (!embedded::Find(config.package.empty() ? GetModuleFilePath(nullptr) : config.package).empty())
                RunOffline();
            else
                RunOnline();
        }
        catch (const std::exception& e)
        {
            Fail(L"Unexpected error: " + toWString(e.what()));
        }
        catch (...)
        {
            Fail(L"Unexpected error");
        }

        {
            std::lock_guard<std::mutex> lock(mutex);
            progress.phase = Phase::Finished;
            if (cancel && outcome.result != Result::Success)
                outcome.result = Result::Cancelled;
        }
        Log(L"Installation {}", outcome.result == Result::Success ? L"completed" : outcome.result == Result::Cancelled ? L"cancelled" : L"failed");
        finished = true;
    }

    void Job::RunOffline()
    {
        auto exe = config.package.empty() ? GetModuleFilePath(nullptr) : config.package;
        std::vector<std::unique_ptr<zip::Reader>> readers;
        uint64_t total = 0;

        for (auto& archive : embedded::Find(exe))
        {
            auto reader = std::make_unique<zip::Reader>();
            if (!reader->Open(exe, archive.offset, archive.size))
            {
                Fail(Format(L"The installation package {} is damaged. Download the installer again.", archive.name), { reader->Error() });
                return;
            }
            Log(L"Embedded archive {}: {} entries, {}", archive.name, reader->Entries().size(), formatBytesW(reader->TotalUncompressedSize()));
            total += reader->TotalUncompressedSize();
            readers.push_back(std::move(reader));
        }

        if (auto free = GetFreeDiskSpace(target); free && *free < total)
        {
            Fail(Format(L"Not enough disk space on {}. Required: {}, available: {}.", target.root_path().wstring(), formatBytesW(total), formatBytesW(*free)));
            return;
        }

        ExtractReport report;
        uint64_t base = 0;
        for (auto& reader : readers)
        {
            Extract(*reader, base, total, report);
            if (report.cancelled || !report.errors.empty())
                break;
            base += reader->TotalUncompressedSize();
        }

        std::lock_guard<std::mutex> lock(mutex);
        outcome.filesWritten = report.written;
        if (report.cancelled)
        {
            outcome.result = Result::Cancelled;
        }
        else if (!report.errors.empty())
        {
            outcome.result = Result::Failed;
            outcome.error = DescribeErrors(report.errors);
            if (report.errors.size() > 1)
                outcome.details = report.errors;
            if (std::any_of(report.errors.begin(), report.errors.end(), IsInUseError))
                outcome.error += L"\nClose the game and try again.";
        }
        else
        {
            outcome.result = Result::Success;
            if (report.inUse)
                outcome.warnings.push_back(Format(L"{} files were in use, restart the game to use the new version.", report.inUse));
            if (report.skipped)
                outcome.warnings.push_back(Format(L"{} existing ini files were kept.", report.skipped));
        }
    }

    void Job::RunOnline()
    {
        if (config.updateUrl.empty())
        {
            Fail(L"No download address is configured (muSetUpdateURL).");
            return;
        }

        auto info = GetInstallerDownloadInfo(config.updateUrl, {});
        if (cancel)
            return;
        if (!info.found())
        {
            Fail(L"Cannot find the download.", { Format(L"{}: {}", toWString(config.updateUrl), info.error) });
            return;
        }

        auto name = toWString(info.name.empty() ? std::string("download.zip") : info.name);
        auto temp = target / (name + L".modupdater");
        Log(L"Downloading {} ({})", toWString(info.url), info.size ? formatBytesW(info.size) : L"unknown size");

        if (auto free = GetFreeDiskSpace(target); free && info.size && *free < info.size)
        {
            Fail(Format(L"Not enough disk space on {}. Required: {}, available: {}.", target.root_path().wstring(), formatBytesW(info.size), formatBytesW(*free)));
            return;
        }

        SetProgress(Phase::Downloading, 0, info.size, name);
        auto downloaded = http::DownloadToFile(info.url, temp, [&](uint64_t done, uint64_t total)
        {
            SetProgress(Phase::Downloading, done, total ? total : info.size, name);
            return !cancel.load();
        });

        if (downloaded.cancelled || cancel)
            return;
        if (!downloaded.ok)
        {
            Fail(Format(L"Cannot download {}.", name), { downloaded.error });
            return;
        }

        ExtractReport report;
        {
            zip::Reader reader;
            if (!reader.Open(temp))
            {
                DeleteFileW(temp.c_str());
                Fail(L"The downloaded file is not a valid zip archive.", { reader.Error() });
                return;
            }
            Extract(reader, 0, reader.TotalUncompressedSize(), report);
        }
        DeleteFileW(temp.c_str());

        std::lock_guard<std::mutex> lock(mutex);
        outcome.filesWritten = report.written;
        if (report.cancelled)
        {
            outcome.result = Result::Cancelled;
        }
        else if (!report.errors.empty())
        {
            outcome.result = Result::Failed;
            outcome.error = DescribeErrors(report.errors);
            if (report.errors.size() > 1)
                outcome.details = report.errors;
            if (std::any_of(report.errors.begin(), report.errors.end(), IsInUseError))
                outcome.error += L"\nClose the game and try again.";
        }
        else
        {
            outcome.result = Result::Success;
            if (report.inUse)
                outcome.warnings.push_back(Format(L"{} files were in use, restart the game to use the new version.", report.inUse));
            if (report.skipped)
                outcome.warnings.push_back(Format(L"{} existing ini files were kept.", report.skipped));
        }
    }

    std::wstring DescribeOutcome(const Config& config, const Outcome& outcome, const std::filesystem::path& target)
    {
        std::wstring text;
        switch (outcome.result)
        {
        case Result::Success:
            text = Text(config, MU_STR_COMPLETE_TEXT, target);
            for (auto& warning : outcome.warnings)
                text += L"\n\n" + warning;
            break;
        case Result::Failed:
            text = outcome.error;
            for (size_t i = 0; i < outcome.details.size() && i < 6; i++)
                text += L"\n" + outcome.details[i];
            if (outcome.details.size() > 6)
                text += Format(L"\n...and {} more", outcome.details.size() - 6);
            break;
        case Result::Cancelled:
            break;
        }
        return text;
    }

    std::wstring ProgressLine(const Progress& p)
    {
        std::wstring line;
        if (p.phase == Phase::Downloading)
        {
            if (p.total)
                line = Format(L"{}% · {} of {}", std::min<uint64_t>(100, p.done * 100 / p.total), formatBytesW(p.done, 1), formatBytesW(p.total, 1));
            else
                line = formatBytesW(p.done, 1);
            if (p.bytesPerSecond > 1)
                line += Format(L" · {}/s", formatBytesW(static_cast<uint64_t>(p.bytesPerSecond), 1));
        }
        else if (p.phase == Phase::Extracting && p.total)
        {
            line = Format(L"{}%", std::min<uint64_t>(100, p.done * 100 / p.total));
        }
        return line;
    }

    bool LaunchGame(const Config& config, const std::filesystem::path& folder)
    {
        auto exe = FindGameExecutable(config, folder);
        if (exe.empty())
            return false;
        Log(L"Launching {}", exe.wstring());
        auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, folder.c_str(), SW_SHOWNORMAL));
        return result > 32;
    }

    int RunSilent(Config& config, const CommandLine& commandLine)
    {
        if (AttachConsole(ATTACH_PARENT_PROCESS))
            LogSetConsole(true);

        std::filesystem::path target = commandLine.installDir;
        if (target.empty())
        {
            auto locations = DetectLocations(config);
            if (locations.empty())
            {
                Log(L"No installation folder was found, use --mu-install-dir=<folder>");
                return MU_INSTALL_FAILED;
            }
            target = locations.front().path;
        }

        if (!TestWriteAccess(target, true))
        {
            Log(L"No permission to write to {}, run the installer as administrator", target.wstring());
            return MU_INSTALL_FAILED;
        }

        Job job(config, target, config.iniMode);
        job.Start();
        int lastPercent = -1;
        while (!job.IsFinished())
        {
            auto p = job.GetProgress();
            int percent = p.total ? static_cast<int>(p.done * 100 / p.total) : 0;
            if (percent / 10 != lastPercent / 10)
            {
                lastPercent = percent;
                Log(L"{} {}%", job.StatusText(), percent);
            }
            Sleep(100);
        }

        auto outcome = job.GetOutcome();
        if (outcome.result == Result::Success)
            return MU_INSTALL_SUCCESS;
        return outcome.result == Result::Cancelled ? MU_INSTALL_CANCELLED : MU_INSTALL_FAILED;
    }
}

using namespace mu;
using namespace mu::installer;

int muRunInstaller()
{
    HMODULE self = NULL;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&muRunInstaller), &self);

    auto config = LoadConfig(self);
    auto commandLine = ParseCommandLine();
    if (commandLine.iniMode)
        config.iniMode = *commandLine.iniMode;
    if (commandLine.theme)
        config.theme = *commandLine.theme;

    auto exe = GetModuleFilePath(nullptr);
    std::filesystem::path logFile = config.logFile;
    if (logFile.empty())
    {
        wchar_t temp[MAX_PATH + 1] = {};
        if (GetTempPathW(MAX_PATH + 1, temp))
            logFile = std::filesystem::path(temp) / (exe.stem().wstring() + L".log");
    }
    LogSetFile(logFile);
    Log(L"{} {}", exe.wstring(), ui::GetProcessArguments());

    HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    int result = MU_INSTALL_FAILED;
    try
    {
        if (!commandLine.screenshot.empty())
        {
            // pictures of the installer for documentation and tests
            bool rendered = RenderModernPreview(config, commandLine.screenshotPage, commandLine.screenshotDpi, commandLine.screenshot);
            Log(L"Screenshot {}: {}", commandLine.screenshot.wstring(), rendered ? L"saved" : L"failed");
            result = rendered ? MU_INSTALL_SUCCESS : MU_INSTALL_FAILED;
        }
        else if (commandLine.silent)
        {
            result = RunSilent(config, commandLine);
        }
        else if (commandLine.ui.value_or(config.ui) == MU_UI_MODERN)
        {
            result = RunModern(config, commandLine);
            if (result < 0)
            {
                Log(L"The modern installer UI is not available, using the classic one");
                result = RunClassic(config, commandLine);
            }
        }
        else
        {
            result = RunClassic(config, commandLine);
        }
    }
    catch (const std::exception& e)
    {
        Log(L"Unexpected error: {}", toWString(e.what()));
        auto message = L"Unexpected error: " + toWString(e.what());
        MessageBoxW(nullptr, message.c_str(), WindowTitle(config).c_str(), MB_OK | MB_ICONERROR);
    }

    if (SUCCEEDED(co))
        CoUninitialize();

    Log(L"Installer finished ({})", result);
    return result;
}

void muInitInstaller()
{
    muRunInstaller();
}

bool muAppendZipFile(int argc, char* argv[])
{
    if (argc < 2 || !argv || !argv[1])
        return false;

    // prefer the Unicode command line, argv is in the ANSI code page
    std::vector<std::wstring> args;
    auto wideArgs = GetArguments();
    if (static_cast<int>(wideArgs.size()) == argc)
        args = wideArgs;
    else
        for (int i = 0; i < argc; i++)
            args.push_back(argv[i] ? toWStringCP(argv[i], CP_ACP) : L"");

    std::filesystem::path zipPath = args[1];
    std::error_code ec;
    if (!std::filesystem::is_regular_file(zipPath, ec) || !HasZipSignature(zipPath))
        return false;

    auto exe = GetModuleFilePath(nullptr);
    std::filesystem::path output = (args.size() >= 3 && !args[2].empty()) ? std::filesystem::path(args[2])
        : exe.parent_path() / (exe.stem().wstring() + L"_with_" + zipPath.stem().wstring() + exe.extension().wstring());

    bool console = AttachConsole(ATTACH_PARENT_PROCESS) != FALSE;
    if (console)
        LogSetConsole(true);

    std::wstring error;
    std::wstring message;
    if (embedded::Append(exe, zipPath, output, &error))
        message = Format(L"Created {} ({})", output.wstring(), formatBytesW(std::filesystem::file_size(output, ec)));
    else
        message = Format(L"Cannot create the offline installer: {}", error);

    Log(L"{}", message);
    if (!console && StartedFromExplorer())
        MessageBoxW(nullptr, message.c_str(), L"modupdater", MB_OK | (error.empty() ? MB_ICONINFORMATION : MB_ICONERROR));

    // the command line was meant for this, don't start the installer
    return true;
}

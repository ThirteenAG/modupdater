#include "stdafx.h"
#include "libmodupdater.h"
#include "archive.h"
#include "fileops.h"
#include "string_funcs.h"
#include "ui_common.h"
#include "common/test_server.h"
#include "common/test_util.h"
#include "resource.h"
#include <shellapi.h>

// TestInstallerApp.exe                                   scenario picker, runs the scenarios below with a local test server
// TestInstallerApp.exe mod.zip [output.exe]              creates an offline installer (muAppendZipFile)
// TestInstallerApp.exe --test-url=<url> [options]        installer, like a mod's installer would be
//     --test-game=<folder>       suggested install folder (a fake game with TestGame.exe)
//     --test-locations=<n>       n more suggested folders, every third one without the game
//     --test-style=default|image|custom
//                                default: built-in light and dark look
//                                image:   background image, custom colors, always dark
//                                custom:  own light and dark gradients, font, texts and window width
//     --mu-ui=modern|classic --mu-theme=auto|light|dark --mu-silent --mu-install-dir=<folder> are handled by the library,
//     so are --mu-screenshot=<file.png> --mu-screenshot-page=main|locations|tooltip|progress|success|failure|cancelled --mu-screenshot-dpi=144

namespace
{
    struct Options
    {
        std::wstring url;
        std::wstring game;
        int locations = 0;
        std::wstring style = L"default";
        bool installer = false;
    };

    Options ParseOptions()
    {
        Options options;
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        for (int i = 1; argv && i < argc; i++)
        {
            std::wstring arg = argv[i];
            auto value = [&](const wchar_t* name) -> const wchar_t*
            {
                size_t length = wcslen(name);
                return arg.compare(0, length, name) == 0 ? argv[i] + length : nullptr;
            };

            if (auto v = value(L"--test-url="))
                options.url = v;
            else if (auto v = value(L"--test-game="))
                options.game = v;
            else if (auto v = value(L"--test-locations="))
                options.locations = _wtoi(v);
            else if (auto v = value(L"--test-style="))
                options.style = v;

            if (arg.starts_with(L"--test-") || arg.starts_with(L"--mu-"))
                options.installer = true;
        }
        if (argv)
            LocalFree(argv);
        return options;
    }

    HMODULE Self()
    {
        HMODULE hm = NULL;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&Self), &hm);
        return hm;
    }

    void Configure(const Options& options)
    {
        HMODULE hm = Self();
        muSetInstallerUI(hm, MU_UI_MODERN);
        muSetInstallerIcon(hm, LoadIconW(hm, MAKEINTRESOURCEW(IDI_TESTAPP)));
        muSetInstallerWindowTitle(hm, "Test Mod");
        muSetInstallerMainInstruction(hm, "Choose where to install Test Mod");
        muSetInstallerContent(hm,
            "Test Mod is a pretend modification that is used to try the installer. It copies a few files into the "
            "test game folder, including a settings file that keeps your changes when you install it again."
            "\n\n"
            "<a href=\"https://github.com/ThirteenAG/modupdater\">modupdater on GitHub</a>");
        muSetInstallerFooter(hm, "Test build · <a href=\"https://github.com/ThirteenAG/modupdater/issues\">Report an issue</a>");
        muSetUpdateURL(hm, toString(options.url).c_str());
        muSetInstallerGameExecutable(hm, "TestGame.exe");
        if (!options.game.empty())
            muAddInstallerPath(hm, toString(options.game).c_str());
        muSetInstallerIniMode(hm, MU_INI_MERGE, true);
        muSetInstallerLogoResource(hm, MAKEINTRESOURCEA(IDR_TEST_LOGO), "PNG");

        // more folders for the list of install locations
        auto libraries = std::filesystem::temp_directory_path() / L"modupdater-test-installer" / L"libraries";
        for (int i = 1; i <= options.locations; i++)
        {
            auto folder = libraries / std::format(L"Library {}", i) / L"Test Game";
            std::error_code ec;
            std::filesystem::create_directories(folder, ec);
            if (i % 3 != 0)
                test::WriteFile(folder / L"TestGame.exe", "MZ");
            muAddInstallerPath(hm, toString(folder.wstring()).c_str());
        }

        if (options.style == L"image")
        {
            // the artwork is a night scene: always dark, whatever Windows uses
            muSetInstallerTheme(hm, MU_THEME_DARK);
            muSetInstallerBackgroundResource(hm, MAKEINTRESOURCEA(IDR_TEST_BACKGROUND), "JPG");
            muSetInstallerBackgroundOverlay(hm, 55);
            muSetInstallerGradient(hm, RGB(0x10, 0x12, 0x1E), RGB(0x06, 0x07, 0x0B));
            muSetInstallerColor(hm, MU_COLOR_BUTTON, RGB(0xE0, 0x2F, 0x3A));
            muSetInstallerColor(hm, MU_COLOR_BUTTON_HOVER, RGB(0xF0, 0x45, 0x4F));
            muSetInstallerColor(hm, MU_COLOR_BUTTON_PRESSED, RGB(0xB8, 0x22, 0x2C));
            muSetInstallerColor(hm, MU_COLOR_PROGRESS, RGB(0x00, 0xDC, 0xDC));
            muSetInstallerColor(hm, MU_COLOR_LINK, RGB(0x00, 0xDC, 0xDC));
            muSetInstallerColor(hm, MU_COLOR_LINK_HOVER, RGB(0x8C, 0xF5, 0xF5));
        }
        else if (options.style == L"custom")
        {
            // follows Windows with an own gradient for each theme
            muSetInstallerThemeGradient(hm, MU_THEME_LIGHT, RGB(0xFA, 0xFB, 0xFD), RGB(0xE2, 0xE8, 0xF1));
            muSetInstallerThemeGradient(hm, MU_THEME_DARK, RGB(0x1D, 0x26, 0x21), RGB(0x0D, 0x12, 0x0F));
            muSetInstallerColor(hm, MU_COLOR_BUTTON, RGB(0x10, 0x7C, 0x10));
            muSetInstallerColor(hm, MU_COLOR_BUTTON_HOVER, RGB(0x1A, 0x92, 0x1A));
            muSetInstallerColor(hm, MU_COLOR_BUTTON_PRESSED, RGB(0x0C, 0x5E, 0x0C));
            muSetInstallerThemeColor(hm, MU_THEME_LIGHT, MU_COLOR_PROGRESS, RGB(0x10, 0x7C, 0x10));
            muSetInstallerThemeColor(hm, MU_THEME_DARK, MU_COLOR_PROGRESS, RGB(0x3C, 0xC8, 0x3C));
            muSetInstallerThemeColor(hm, MU_THEME_DARK, MU_COLOR_LINK, RGB(0x7C, 0xDC, 0x7C));
            muSetInstallerThemeColor(hm, MU_THEME_DARK, MU_COLOR_LINK_HOVER, RGB(0xB0, 0xF0, 0xB0));
            muSetInstallerFont(hm, "Segoe UI");
            muSetInstallerString(hm, MU_STR_INSTALL, "Install Test Mod");
            muSetInstallerString(hm, MU_STR_HEADING, "");
            muSetInstallerWindowSize(hm, 640, 0);
        }
    }

    int RunChild(const std::filesystem::path& exe, const std::wstring& arguments, bool newConsole = false)
    {
        std::wstring commandLine = test::Quote(exe.wstring()) + L" " + arguments;
        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi = {};
        if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, newConsole ? CREATE_NEW_CONSOLE : 0, nullptr, nullptr, &si, &pi))
            return -1;
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return static_cast<int>(code);
    }

    const wchar_t* ResultName(int code)
    {
        switch (code)
        {
        case MU_INSTALL_SUCCESS: return L"success";
        case MU_INSTALL_CANCELLED: return L"cancelled";
        case MU_INSTALL_FAILED: return L"failed";
        case MU_INSTALL_RESTARTED: return L"restarted as administrator";
        }
        return L"?";
    }

    int RunScenarioPicker()
    {
        mu::ui::ScopedDpiAwareness dpiAwareness;
        test::Server server;
        if (!server.Start())
        {
            MessageBoxW(nullptr, L"Cannot start the local test server", L"modupdater tests", MB_ICONERROR);
            return 1;
        }

        // content of the test mod
        std::string readme = "Test Mod\r\n\r\nInstalled by the modupdater test installer.\r\n";
        auto archive = test::CreateZipData({
            { "plugins/", "" },
            { "plugins/TestMod.asi", test::RandomData(300 * 1024, 11) },
            { "plugins/TestMod.ini", "[MAIN]\r\n; change a value and install again, your value is kept\r\nValue = 1\r\nColor = red\r\n" },
            { "update/TestMod/data.img", test::RandomData(12 * 1024 * 1024, 12) },
            { "TestMod-readme.txt", readme },
        });

        test::Resource normal;
        normal.body = archive;
        normal.contentDisposition = "attachment; filename=TestMod.zip";
        normal.lastModified = test::HttpDate(1);
        normal.bytesPerSecond = 6 * 1024 * 1024;
        server.Set("/TestMod.zip", normal);
        test::Resource slow = normal;
        slow.bytesPerSecond = 350 * 1024;
        server.Set("/slow/TestMod.zip", slow);

        auto temp = std::filesystem::temp_directory_path() / L"modupdater-test-installer";
        auto game = temp / L"Test Game";
        std::error_code ec;
        std::filesystem::create_directories(game, ec);
        if (!test::Exists(game / L"TestGame.exe"))
        {
            wchar_t system[MAX_PATH];
            GetSystemDirectoryW(system, MAX_PATH);
            CopyFileW((std::filesystem::path(system) / L"winver.exe").c_str(), (game / L"TestGame.exe").c_str(), FALSE);
        }

        // offline installer: this executable with the archive appended
        auto self = mu::GetModuleFilePath(nullptr);
        auto zipFile = temp / L"TestMod.zip";
        auto offline = temp / L"TestModOfflineInstaller.exe";
        test::WriteFile(zipFile, archive);
        mu::embedded::Append(self, zipFile, offline);

        const std::wstring url = L" --test-url=" + toWString(server.Url("/TestMod.zip"));
        const std::wstring slowUrl = L" --test-url=" + toWString(server.Url("/slow/TestMod.zip"));
        const std::wstring missingUrl = L" --test-url=" + toWString(server.Url("/missing/TestMod.zip"));
        const std::wstring gameArg = L" --test-game=" + test::Quote(game.wstring());

        std::wstring footer = L"Test game folder: <a href=\"open\">" + game.wstring() + L"</a>";
        int themeChoice = 201;
        for (;;)
        {
            TASKDIALOG_BUTTON buttons[] = {
                { 101, L"Modern UI: online install\nDownloads the test mod from a local server and installs it into the test game folder" },
                { 102, L"Modern UI: offline installer\nThe archive is embedded in the installer (TestModOfflineInstaller.exe)" },
                { 103, L"Modern UI: background image\nCustom gradient, colors, logo and background, always dark" },
                { 104, L"Modern UI: custom light and dark gradients, font and texts\nNo heading, narrower window" },
                { 105, L"Modern UI: many install locations\nScrolling list, folders without the game and the folder button" },
                { 106, L"Modern UI: slow download\nTry Cancel, closing the window during the installation, the taskbar progress" },
                { 107, L"Modern UI: download error\nThe server answers 404" },
                { 108, L"Classic UI: online install\nThe TaskDialog based installer" },
                { 109, L"Classic UI: offline installer" },
                { 110, L"Updater: game with two plugins (TestApp.exe)\nPlugins that use muInit, the update dialog appears after 5 seconds" },
                { 111, L"Render screenshots of the modern UI\nAll pages of the three styles in light and dark at 100% and 150% scale" },
            };
            TASKDIALOG_BUTTON themes[] = {
                { 201, L"Theme: follow the Windows app mode" },
                { 202, L"Theme: light" },
                { 203, L"Theme: dark" },
            };

            TASKDIALOGCONFIG tdc = { sizeof(tdc) };
            tdc.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION | TDF_ENABLE_HYPERLINKS | TDF_SIZE_TO_CONTENT;
            tdc.dwCommonButtons = TDCBF_CLOSE_BUTTON;
            tdc.pszWindowTitle = L"modupdater tests";
            tdc.pszMainInstruction = L"Choose a scenario";
            tdc.pszContent = L"Each scenario starts the installer the way a mod would use it. Run a scenario twice to see an update of an existing installation (the ini file keeps your changes).";
            tdc.pszFooter = footer.c_str();
            tdc.pszFooterIcon = TD_INFORMATION_ICON;
            tdc.pButtons = buttons;
            tdc.cButtons = _countof(buttons);
            tdc.pRadioButtons = themes;
            tdc.cRadioButtons = _countof(themes);
            tdc.nDefaultRadioButton = themeChoice;
            tdc.hInstance = GetModuleHandleW(nullptr);
            tdc.dwFlags |= TDF_USE_HICON_MAIN;
            tdc.hMainIcon = LoadIconW(tdc.hInstance, MAKEINTRESOURCEW(IDI_TESTAPP));
            tdc.lpCallbackData = reinterpret_cast<LONG_PTR>(&game);
            tdc.pfCallback = [](HWND, UINT notification, WPARAM, LPARAM lParam, LONG_PTR data) -> HRESULT
            {
                if (notification == TDN_HYPERLINK_CLICKED && std::wstring(reinterpret_cast<LPCWSTR>(lParam)) == L"open")
                    ShellExecuteW(nullptr, L"explore", reinterpret_cast<std::filesystem::path*>(data)->c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                return S_OK;
            };

            int choice = 0;
            if (FAILED(TaskDialogIndirect(&tdc, &choice, &themeChoice, nullptr)) || choice < 101)
                return 0;

            const std::wstring modern = std::wstring(L" --mu-ui=modern") + (themeChoice == 202 ? L" --mu-theme=light" : themeChoice == 203 ? L" --mu-theme=dark" : L"");
            int code = 0;
            switch (choice)
            {
            case 101: code = RunChild(self, url + gameArg + modern); break;
            case 102: code = RunChild(offline, gameArg + modern); break;
            case 103: code = RunChild(self, url + gameArg + L" --test-style=image" + modern); break;
            case 104: code = RunChild(self, url + gameArg + L" --test-style=custom" + modern); break;
            case 105: code = RunChild(self, url + gameArg + L" --test-locations=7" + modern); break;
            case 106: code = RunChild(self, slowUrl + gameArg + L" --test-style=image" + modern); break;
            case 107: code = RunChild(self, missingUrl + gameArg + modern); break;
            case 108: code = RunChild(self, url + gameArg + L" --mu-ui=classic"); break;
            case 109: code = RunChild(offline, gameArg + L" --mu-ui=classic"); break;
            case 110: code = RunChild(test::BinDir() / L"TestApp.exe", L"", true); break;
            case 111:
            {
                // made-up install folders (no --test-game) show every state of the location list
                auto shots = temp / L"screenshots";
                std::filesystem::create_directories(shots, ec);
                for (auto style : { L"default", L"image", L"custom" })
                    for (auto theme : { L"light", L"dark" })
                        for (auto page : { L"main", L"locations", L"tooltip", L"progress", L"success", L"failure", L"cancelled" })
                            for (auto dpi : { L"96", L"144" })
                            {
                                auto png = shots / std::format(L"{}-{}-{}-{}.png", style, theme, page, dpi);
                                RunChild(self, std::format(L"--test-style={} --mu-theme={} --mu-screenshot-page={} --mu-screenshot-dpi={} --mu-screenshot={}", style, theme, page, dpi, test::Quote(png.wstring())));
                            }
                ShellExecuteW(nullptr, L"explore", shots.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                break;
            }
            }

            if (choice <= 109)
                footer = std::format(L"Last installer result: {} ({})\nTest game folder: <a href=\"open\">{}</a>", code, ResultName(code), game.wstring());
        }
    }
}

int main(int argc, char** argv)
{
    if (muAppendZipFile(argc, argv))
        return 0; // an offline installer was created

    auto options = ParseOptions();
    if (options.installer)
    {
        Configure(options);
        return muRunInstaller();
    }

    return RunScenarioPicker();
}

int APIENTRY WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    return main(__argc, __argv);
}

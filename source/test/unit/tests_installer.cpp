#include "stdafx.h"
#include "test.h"
#include "installer.h"
#include "archive.h"
#include "fileops.h"
#include "string_funcs.h"
#include "ui_common.h"
#include "common/test_server.h"
#include "common/test_util.h"

using namespace mu;
using namespace mu::installer;

namespace
{
    std::string ModArchive()
    {
        return test::CreateZipData({
            { "plugins/", "" },
            { "plugins/TestMod.asi", "asi" },
            { "plugins/TestMod.ini", "[MAIN]\nValue = 1\n" },
            { "update/data/big.dat", test::RandomData(1024 * 1024) },
        });
    }

    Outcome RunJob(Job& job, int timeoutMs = 60000)
    {
        job.Start();
        for (int waited = 0; !job.IsFinished() && waited < timeoutMs; waited += 20)
            Sleep(20);
        return job.GetOutcome();
    }

    std::filesystem::path TestInstaller()
    {
        return test::BinDir() / L"TestInstallerApp.exe";
    }
}

TEST_CASE(Installer_JobOnline)
{
    test::Server server;
    REQUIRE(server.Start());
    test::Resource zip;
    zip.body = ModArchive();
    server.Set("/Mod.zip", zip);

    test::TempDir dir(L"online");
    Config config;
    config.updateUrl = server.Url("/Mod.zip");
    config.package = test::BinDir() / L"TestDLL1.asi"; // no embedded archives
    Job job(config, dir / L"game", IniMode::Merge);
    auto outcome = RunJob(job);
    CHECK(outcome.result == Result::Success);
    CHECK_EQ(test::ReadFile(dir / L"game" / L"plugins" / L"TestMod.asi"), std::string("asi"));
    CHECK(test::Exists(dir / L"game" / L"update" / L"data" / L"big.dat"));
    CHECK(!test::Exists(dir / L"game" / L"Mod.zip.modupdater"));
    CHECK_EQ(outcome.filesWritten, 3);
}

TEST_CASE(Installer_JobOffline)
{
    test::TempDir dir(L"offline");
    REQUIRE(test::WriteFile(dir / L"Mod.zip", ModArchive()));
    auto package = dir / L"installer.exe";
    REQUIRE(embedded::Append(test::BinDir() / L"TestDLL1.asi", dir / L"Mod.zip", package));
    REQUIRE(test::AddFakeSignature(package)); // released installers are signed

    Config config;
    config.package = package;
    config.updateUrl = "http://127.0.0.1:1/must-not-be-used.zip";
    Job job(config, dir / L"game", IniMode::Merge);
    auto outcome = RunJob(job);
    CHECK(outcome.result == Result::Success);
    CHECK_EQ(test::ReadFile(dir / L"game" / L"plugins" / L"TestMod.ini"), std::string("[MAIN]\nValue = 1\n"));
}

TEST_CASE(Installer_JobReportsErrors)
{
    test::Server server;
    REQUIRE(server.Start());
    test::TempDir dir(L"failure");
    Config config;
    config.package = test::BinDir() / L"TestDLL1.asi";

    config.updateUrl = server.Url("/missing.zip");
    Job missing(config, dir / L"game", IniMode::Merge);
    auto outcome = RunJob(missing);
    CHECK(outcome.result == Result::Failed);
    test::Note(toString(DescribeOutcome(config, outcome, dir / L"game")));
    CHECK(!outcome.error.empty());

    test::Resource html;
    html.body = "<html>not a zip</html>";
    server.Set("/page.zip", html);
    config.updateUrl = server.Url("/page.zip");
    Job invalid(config, dir / L"game", IniMode::Merge);
    outcome = RunJob(invalid);
    CHECK(outcome.result == Result::Failed);
    CHECK(outcome.error.find(L"not a valid zip") != std::wstring::npos);

    config.updateUrl.clear();
    Job nothing(config, dir / L"game", IniMode::Merge);
    CHECK(RunJob(nothing).result == Result::Failed);
}

TEST_CASE(Installer_JobCancel)
{
    test::Server server;
    REQUIRE(server.Start());
    test::Resource slow;
    slow.body = ModArchive();
    slow.bytesPerSecond = 100 * 1024;
    server.Set("/slow.zip", slow);

    test::TempDir dir(L"cancel");
    Config config;
    config.package = test::BinDir() / L"TestDLL1.asi";
    config.updateUrl = server.Url("/slow.zip");
    Job job(config, dir / L"game", IniMode::Merge);
    job.Start();
    for (int i = 0; i < 200 && job.GetProgress().phase != Phase::Downloading; i++)
        Sleep(20);
    Sleep(300);
    job.Cancel();
    for (int i = 0; i < 500 && !job.IsFinished(); i++)
        Sleep(20);
    REQUIRE(job.IsFinished());
    CHECK(job.GetOutcome().result == Result::Cancelled);
    CHECK(!test::Exists(dir / L"game" / L"slow.zip.modupdater"));
    CHECK(!test::Exists(dir / L"game" / L"plugins" / L"TestMod.asi"));
}

TEST_CASE(Installer_Texts)
{
    Config config;
    config.windowTitle = L"Fusion Fix";
    config.gameExecutables = { L"GTAIV.exe" };
    CHECK(Text(config, MU_STR_INSTALLING) == L"Installing Fusion Fix...");
    CHECK(Text(config, MU_STR_LAUNCH) == L"Launch GTAIV.exe");
    config.strings[MU_STR_INSTALL] = L"Installieren";
    CHECK(Text(config, MU_STR_INSTALL) == L"Installieren");
    CHECK(Text(config, MU_STR_COMPLETE_TEXT, L"C:\\Game") == L"Fusion Fix has been installed to:\nC:\\Game");
    config.windowTitle.clear();
    CHECK(Text(config, MU_STR_INSTALLING) == L"Installing...");
}

TEST_CASE(Installer_PathChecks)
{
    test::TempDir dir(L"paths");
    auto game = dir / L"game";
    test::WriteFile(game / L"Game.exe", "MZ");
    Config config;
    config.gameExecutables = { L"Missing.exe", L"Game.exe" };
    config.extraPaths = { game.wstring(), (dir / L"does not exist").wstring() };

    auto check = CheckPath(config, game);
    CHECK(check.writable);
    CHECK(check.hasGameExe);
    CHECK(!CheckPath(config, dir.Path()).hasGameExe);
    CHECK(FindGameExecutable(config, game).filename() == L"Game.exe");

    auto locations = DetectLocations(config);
    CHECK(std::any_of(locations.begin(), locations.end(), [&](const Location& l) { return l.path == game; }));
    CHECK(std::none_of(locations.begin(), locations.end(), [&](const Location& l) { return l.path.filename() == L"does not exist"; }));
}

TEST_CASE(Installer_MultipleExecutables)
{
    auto module = reinterpret_cast<HMODULE>(static_cast<uintptr_t>(0x12360000));
    muSetInstallerGameExecutable(module, "GTAIV.exe; EFLC.exe,PlayGTAIV.exe");
    auto config = LoadConfig(module);
    REQUIRE(config.gameExecutables.size() == 3);
    CHECK(config.gameExecutables[1] == L"EFLC.exe");
    CHECK(config.gameExecutables[2] == L"PlayGTAIV.exe");

    test::TempDir dir(L"exes");
    auto episodes = dir / L"episodes";
    auto complete = dir / L"complete";
    REQUIRE(test::WriteFile(episodes / L"EFLC.exe", "MZ"));
    REQUIRE(test::WriteFile(complete / L"PlayGTAIV.exe", "MZ"));
    REQUIRE(test::WriteFile(complete / L"GTAIV.exe", "MZ"));

    // any of them marks a game folder
    CHECK(CheckPath(config, episodes).hasGameExe);
    CHECK(CheckPath(config, complete).hasGameExe);
    CHECK(!CheckPath(config, dir.Path()).hasGameExe);
    CHECK(Text(config, MU_STR_EXE_MISSING) == L"GTAIV.exe was not found in this folder");

    // the button names the executable it starts: the first one found
    CHECK(LaunchText(config, episodes) == L"Launch EFLC.exe");
    CHECK(LaunchText(config, complete) == L"Launch GTAIV.exe");
    CHECK(LaunchText(config, dir.Path()).empty());
    CHECK(Text(config, MU_STR_GAME_RUNNING, {}, {}, {}, L"EFLC.exe") == L"EFLC.exe is running. Close the game before installing.");
}

TEST_CASE(Installer_ShortLocationNames)
{
    auto names = ShortLocationNames({
        { L"H:\\SteamLibrary\\steamapps\\common\\Grand Theft Auto IV\\GTAIV", L"Steam" },
        { L"C:\\Games\\Rockstar Games\\Grand Theft Auto IV", L"Rockstar Games Launcher" },
        { L"D:\\Library 1\\Test Game", L"" },
        { L"D:\\Library 2\\test game", L"" },   // same name, other case
        { L"E:\\Library 2\\Test Game", L"" },   // same folders on another drive
        { L"F:\\", L"" },
    });
    REQUIRE(names.size() == 6);
    CHECK(names[0] == L"GTAIV");
    CHECK(names[1] == L"Grand Theft Auto IV");
    CHECK(names[2] == L"Library 1\\Test Game");
    CHECK(names[3] == L"D:\\Library 2\\test game");
    CHECK(names[4] == L"E:\\Library 2\\Test Game");
    CHECK(names[5] == L"F:\\");
    CHECK(ShortLocationNames({}).empty());
}

TEST_CASE(Installer_SilentOnline)
{
    test::Server server;
    REQUIRE(server.Start());
    test::Resource zip;
    zip.body = ModArchive();
    server.Set("/Mod.zip", zip);

    test::TempDir dir(L"silent");
    auto target = dir / L"\u0418\u0433\u0440\u0430 folder"; // unicode + space
    auto cmd = test::Quote(TestInstaller().wstring()) + L" --test-url=" + toWString(server.Url("/Mod.zip")) +
               L" --mu-silent --mu-install-dir=" + test::Quote(target.wstring());
    CHECK_EQ(test::RunProcess(cmd), MU_INSTALL_SUCCESS);
    CHECK(test::Exists(target / L"plugins" / L"TestMod.asi"));

    auto failing = test::Quote(TestInstaller().wstring()) + L" --test-url=" + toWString(server.Url("/nothing.zip")) +
                   L" --mu-silent --mu-install-dir=" + test::Quote((dir / L"other").wstring());
    CHECK_EQ(test::RunProcess(failing), MU_INSTALL_FAILED);
}

TEST_CASE(Installer_OfflineInstallerEndToEnd)
{
    // what release scripts do: "installer.exe mod.zip" creates installer_with_mod.exe
    test::TempDir dir(L"e2e");
    auto installer = dir / L"Installer.exe";
    REQUIRE(CopyFileW(TestInstaller().c_str(), installer.c_str(), FALSE));
    REQUIRE(test::WriteFile(dir / L"Mod.zip", ModArchive()));

    CHECK_EQ(test::RunProcess(test::Quote(installer.wstring()) + L" " + test::Quote((dir / L"Mod.zip").wstring())), 0);
    auto offline = dir / L"Installer_with_Mod.exe";
    REQUIRE(test::Exists(offline));
    CHECK_EQ(embedded::Find(offline).size(), size_t(1));

    // explicit output name and a signature added afterwards
    auto named = dir / L"OfflineInstaller.exe";
    CHECK_EQ(test::RunProcess(test::Quote(installer.wstring()) + L" " + test::Quote((dir / L"Mod.zip").wstring()) + L" " + test::Quote(named.wstring())), 0);
    REQUIRE(test::Exists(named));
    REQUIRE(test::AddFakeSignature(named));

    auto target = dir / L"game";
    // --test-url points nowhere: the embedded archive must be used
    CHECK_EQ(test::RunProcess(test::Quote(named.wstring()) + L" --test-url=http://127.0.0.1:1/x.zip --mu-silent --mu-install-dir=" + test::Quote(target.wstring())), MU_INSTALL_SUCCESS);
    CHECK_EQ(test::ReadFile(target / L"plugins" / L"TestMod.asi"), std::string("asi"));
}

TEST_CASE(Installer_ModernPreviews)
{
    test::TempDir dir(L"preview");
    dir.Keep();
    Config config;
    config.windowTitle = L"Test Mod";
    config.mainInstruction = L"Choose where to install Test Mod";
    config.content = L"A test mod description with a <a href=\"https://example.com\">link</a>.";
    config.footer = L"<a href=\"https://github.com\">github.com</a>";
    config.gameExecutables = { L"Game.exe" };

    for (int theme : { MU_THEME_LIGHT, MU_THEME_DARK })
    {
        config.theme = theme;
        for (auto page : { L"main", L"locations", L"tooltip", L"progress", L"success", L"failure", L"cancelled" })
        {
            for (UINT dpi : { 96u, 144u })
            {
                auto png = dir / std::format(L"{}_{}_{}.png", theme == MU_THEME_DARK ? L"dark" : L"light", page, dpi);
                bool rendered = RenderModernPreview(config, page, dpi, png);
                if (!rendered)
                    test::Note("preview failed: " + toString(png.wstring()));
                CHECK(rendered);
                CHECK(test::Exists(png));
            }
        }
    }
    test::Note("previews: " + toString(dir.Path().wstring()));
}

TEST_CASE(Installer_ThemeSettings)
{
    auto module = reinterpret_cast<HMODULE>(static_cast<uintptr_t>(0x12350000));
    muSetInstallerGradient(module, RGB(1, 1, 1), RGB(2, 2, 2));
    muSetInstallerThemeGradient(module, MU_THEME_DARK, RGB(3, 3, 3), RGB(4, 4, 4));
    muSetInstallerThemeColor(module, MU_THEME_LIGHT, MU_COLOR_POPUP, RGB(5, 5, 5));
    muSetInstallerThemeColor(module, MU_THEME_AUTO, MU_COLOR_BUTTON, RGB(6, 6, 6));   // ignored, needs light or dark
    muSetInstallerThemeColor(module, MU_THEME_DARK, MU_COLOR_COUNT, RGB(7, 7, 7));    // ignored
    uint8_t logo[] = { 1, 2 };
    muSetInstallerThemeLogo(module, MU_THEME_DARK, logo, sizeof(logo));
    uint8_t background[] = { 1, 2, 3 };
    muSetInstallerThemeBackground(module, MU_THEME_LIGHT, background, sizeof(background));
    muSetInstallerThemeBackgroundOverlay(module, MU_THEME_LIGHT, 25);
    muSetInstallerTheme(module, MU_THEME_DARK);

    auto config = LoadConfig(module);
    CHECK_EQ(config.theme, MU_THEME_DARK);
    CHECK(config.colors[MU_COLOR_GRADIENT_TOP] == RGB(1, 1, 1));
    CHECK(config.dark.colors[MU_COLOR_GRADIENT_TOP] == RGB(3, 3, 3));
    CHECK(config.dark.colors[MU_COLOR_GRADIENT_BOTTOM] == RGB(4, 4, 4));
    CHECK(!config.light.colors[MU_COLOR_GRADIENT_TOP].has_value());
    CHECK(config.light.colors[MU_COLOR_POPUP] == RGB(5, 5, 5));
    CHECK(!config.light.colors[MU_COLOR_BUTTON].has_value());
    CHECK(!config.dark.colors[MU_COLOR_BUTTON].has_value());
    CHECK_EQ(config.dark.logo.size(), size_t(2));
    CHECK(config.light.logo.empty());
    CHECK_EQ(config.light.background.size(), size_t(3));
    CHECK(config.dark.background.empty());
    CHECK(config.light.backgroundOverlay == 25);
    CHECK(!config.dark.backgroundOverlay.has_value());
    CHECK(&config.Overrides(MU_THEME_DARK) == &config.dark);
    CHECK(&config.Overrides(MU_THEME_LIGHT) == &config.light);

    // forced themes win, "auto" follows Windows
    CHECK_EQ(ResolveTheme(config), MU_THEME_DARK);
    config.theme = MU_THEME_LIGHT;
    CHECK_EQ(ResolveTheme(config), MU_THEME_LIGHT);
    config.theme = MU_THEME_AUTO;
    CHECK_EQ(ResolveTheme(config), ui::IsDarkModeEnabled() ? MU_THEME_DARK : MU_THEME_LIGHT);
    test::Note(std::string("Windows app mode: ") + (ui::IsDarkModeEnabled() ? "dark" : "light"));
}

TEST_CASE(Installer_ThemeBackgrounds)
{
    // the picture of the theme in use: white for light, black for dark
    auto white = test::SolidImage(0xFFFFFFFF, 64, 36);
    auto black = test::SolidImage(0xFF000000, 64, 36);
    REQUIRE(!white.empty() && !black.empty());

    test::TempDir dir(L"theme-backgrounds");
    Config config;
    config.windowTitle = L"Test Mod";
    config.backgroundOverlay = 0; // only the picture, no gradient over it
    config.light.background.assign(white.begin(), white.end());
    config.dark.background.assign(black.begin(), black.end());
    auto brightness = [&](int theme, const wchar_t* name)
    {
        config.theme = theme;
        auto png = dir / name;
        REQUIRE(RenderModernPreview(config, L"main", 96, png));
        double luminance = test::AverageLuminance(png);
        test::Note(std::format("{}: {:.2f}", toString(name), luminance));
        return luminance;
    };
    CHECK(brightness(MU_THEME_LIGHT, L"light.png") > 0.8);
    CHECK(brightness(MU_THEME_DARK, L"dark.png") < 0.2);

    // a theme without its own picture shows the one for both themes
    config.background.assign(black.begin(), black.end());
    config.light.background.clear();
    CHECK(brightness(MU_THEME_LIGHT, L"shared.png") < 0.35);

    // the overlay of one theme: the light gradient covers the black picture completely, the dark theme still shows it
    config.light.backgroundOverlay = 100;
    CHECK(brightness(MU_THEME_LIGHT, L"light-overlay.png") > 0.6);
    CHECK(brightness(MU_THEME_DARK, L"dark-overlay.png") < 0.2);
}

TEST_CASE(Installer_BackgroundBlur)
{
    // black on the left, white on the right: blurred, the edge turns gray
    auto split = test::PngImage(160, 90, [](int x, int) { return x < 80 ? 0xFF000000u : 0xFFFFFFFFu; });
    REQUIRE(!split.empty());

    test::TempDir dir(L"background-blur");
    Config config;
    config.theme = MU_THEME_LIGHT;
    config.backgroundOverlay = 0;
    config.background.assign(split.begin(), split.end());
    auto nearEdge = [&](int blur, UINT dpi, const wchar_t* name)
    {
        config.backgroundBlur = blur;
        auto png = dir / name;
        REQUIRE(RenderModernPreview(config, L"main", dpi, png));
        // a strip left of the middle of the window, above everything else on the page
        const LONG scale = static_cast<LONG>(dpi), middle = 360 * scale / 96;
        RECT area = { middle - 14 * scale / 96, 4, middle - 6 * scale / 96, 24 };
        double luminance = test::AverageLuminance(png, &area);
        test::Note(std::format("{}: {:.2f}", toString(name), luminance));
        return luminance;
    };
    CHECK(nearEdge(0, 96, L"sharp.png") < 0.05);
    CHECK(nearEdge(16, 96, L"blurred.png") > 0.15);
    CHECK(nearEdge(16, 144, L"blurred-144.png") > 0.15);
}

TEST_CASE(Installer_TextBackdropBlur)
{
    // black on the left, white on the right, lines of text over the edge in the middle
    auto split = test::PngImage(160, 90, [](int x, int) { return x < 80 ? 0xFF000000u : 0xFFFFFFFFu; });
    REQUIRE(!split.empty());

    test::TempDir dir(L"text-backdrop");
    Config config;
    config.theme = MU_THEME_LIGHT;
    config.backgroundOverlay = 0;
    config.background.assign(split.begin(), split.end());
    config.content = L"First line\nSecond line\nThird line\nFourth line";
    auto render = [&](int blur, const wchar_t* name)
    {
        config.textBackdropBlur = blur;
        auto png = dir / name;
        REQUIRE(RenderModernPreview(config, L"main", 96, png));
        return png;
    };
    auto sharp = render(0, L"sharp.png");
    auto backdrop = render(12, L"backdrop.png");

    // the texts and everything else are drawn the same way: only the picture behind the texts changes
    RECT above = { 346, 4, 354, 24 };
    RECT beside = { 346, 40, 354, 400 };
    double aboveSharp = test::AverageLuminance(sharp, &above), aboveBackdrop = test::AverageLuminance(backdrop, &above);
    double besideSharp = test::AverageLuminance(sharp, &beside), besideBackdrop = test::AverageLuminance(backdrop, &beside);
    test::Note(std::format("above the texts {:.3f} -> {:.3f}, beside the edge {:.3f} -> {:.3f}", aboveSharp, aboveBackdrop, besideSharp, besideBackdrop));
    CHECK(std::abs(aboveBackdrop - aboveSharp) < 0.005);
    CHECK(besideBackdrop > besideSharp + 0.02);
}

TEST_CASE(Installer_ThemeRendering)
{
    // what a mod's installer shows, rendered by the test installer with the library's --mu-theme switch
    test::TempDir dir(L"themes");
    auto brightness = [&](const std::wstring& args, const std::wstring& name)
    {
        auto png = dir / (name + L".png");
        CHECK_EQ(test::RunProcess(test::Quote(TestInstaller().wstring()) + L" " + args + L" --mu-screenshot=" + test::Quote(png.wstring())), 0);
        double luminance = test::AverageLuminance(png);
        test::Note(std::format("{}: {:.2f}", toString(name), luminance));
        return luminance;
    };

    // built-in looks
    CHECK(brightness(L"--mu-theme=light", L"default-light") > 0.55);
    CHECK(brightness(L"--mu-theme=dark", L"default-dark") < 0.25);
    CHECK(brightness(L"--mu-theme=dark --mu-screenshot-page=locations", L"default-dark-locations") < 0.25);

    // an own gradient for each theme
    CHECK(brightness(L"--test-style=custom --mu-theme=light", L"custom-light") > 0.75);
    CHECK(brightness(L"--test-style=custom --mu-theme=dark", L"custom-dark") < 0.25);

    // a gradient set for both themes is used by both, the text stays readable on it
    CHECK(brightness(L"--test-style=image --mu-theme=light", L"image-light") < 0.3);
}

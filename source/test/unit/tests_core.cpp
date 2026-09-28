#include "stdafx.h"
#include "test.h"
#include "string_funcs.h"
#include "fileops.h"
#include "gamepaths.h"
#include "registry.h"
#include "remote.h"
#include "ui_common.h"
#include "libmodupdater.h"
#include <json/json.h>

using namespace mu;

TEST_CASE(Strings_FormatBytes)
{
    CHECK_EQ(formatBytes(0), std::string("0 Bytes"));
    CHECK_EQ(formatBytes(999), std::string("999 Bytes"));
    CHECK_EQ(formatBytes(1000), std::string("1.00 KB"));
    CHECK_EQ(formatBytes(206711636), std::string("206.71 MB"));
    CHECK_EQ(formatBytes(5ull * 1000 * 1000 * 1000), std::string("5.00 GB")); // did not fit into int32 before
}

TEST_CASE(Strings_RemoveQuotes)
{
    std::string a = "\"https://example.com\"";
    removeQuotesFromString(a);
    CHECK_EQ(a, std::string("https://example.com"));

    std::string empty;
    removeQuotesFromString(empty); // used to throw std::out_of_range
    CHECK(empty.empty());

    std::string single = "\"";
    removeQuotesFromString(single);
    CHECK(single.empty());
}

TEST_CASE(Strings_CaseInsensitiveUnicode)
{
    CHECK(iequals(std::wstring(L"GTAIV.EXE"), std::wstring(L"gtaiv.exe")));
    CHECK(starts_with(std::wstring(L"\u041F\u0420\u0418\u0412\u0415\u0422.asi"), std::wstring(L"\u043F\u0440\u0438"), false)); // ПРИВЕТ / при
    CHECK(ends_with(std::wstring(L"Mod.ASI"), std::wstring(L".asi"), false));
    CHECK(!ends_with(std::wstring(L"asi"), std::wstring(L".asi"), false));
    CHECK_EQ(toLowerWStr(std::wstring(L"C:\\\u0418\u0433\u0440\u044B\\GTA")), std::wstring(L"c:\\\u0438\u0433\u0440\u044B\\gta"));
}

TEST_CASE(Strings_Utf8RoundTrip)
{
    std::wstring text = L"C:\\Users\\\u0418\u0432\u0430\u043D\\\u6E38\u620F\\\U0001F3AE";
    CHECK(toWString(toString(text)) == text);
}

TEST_CASE(Strings_VisibleTextLength)
{
    CHECK_EQ(ui::GetVisibleTextLength(L"plain"), size_t(5));
    CHECK_EQ(ui::GetVisibleTextLength(L"<a href=\"https://example.com\">link</a> text"), size_t(9));
    CHECK_EQ(ui::GetVisibleTextLength(L"a < b"), size_t(5));
}

TEST_CASE(Paths_SafeJoinAcceptsNormalEntries)
{
    std::filesystem::path root = L"C:\\Games\\GTAIV";
    auto a = SafeJoin(root, L"plugins/GTAIV.EFLC.FusionFix.asi");
    REQUIRE(a.has_value());
    CHECK(*a == std::filesystem::path(L"C:\\Games\\GTAIV\\plugins\\GTAIV.EFLC.FusionFix.asi"));
    CHECK(SafeJoin(root, L"./update//data\\file.img").has_value());
    CHECK(SafeJoin(root, L"\u0444\u0430\u0439\u043B.txt").has_value());
    CHECK(SafeJoin(root, L"folder/").has_value());
}

TEST_CASE(Paths_SafeJoinRejectsZipSlip)
{
    std::filesystem::path root = L"C:\\Games\\GTAIV";
    for (auto bad : { L"../evil.dll", L"plugins/../../evil.dll", L"..\\evil.dll", L"C:\\Windows\\evil.dll", L"\\evil.dll", L"/evil.dll",
                      L"file.txt:stream", L"CON", L"aux.txt", L"folder/nul/x.txt", L"..", L". .", L"a\x01z", L"a|b", L"", L"./" })
    {
        std::wstring error;
        bool rejected = !SafeJoin(root, bad, &error).has_value();
        if (!rejected)
            test::Note("accepted: " + toString(bad));
        CHECK(rejected);
    }
}

TEST_CASE(Paths_IsPathInside)
{
    CHECK(IsPathInside(L"C:\\Games\\GTAIV", L"C:\\Games\\GTAIV\\plugins\\a.asi"));
    CHECK(IsPathInside(L"C:\\Games\\GTAIV\\", L"c:\\games\\gtaiv\\x"));
    CHECK(!IsPathInside(L"C:\\Games\\GTAIV", L"C:\\Games\\GTAIV2\\x"));
    CHECK(!IsPathInside(L"C:\\Games\\GTAIV", L"C:\\Games\\GTAIV"));
    CHECK(!IsPathInside(L"C:\\Games\\GTAIV", L"C:\\Games\\GTAIV\\..\\x"));
}

TEST_CASE(Vdf_LibraryFoldersCurrentFormat)
{
    std::string vdf = R"("libraryfolders"
{
    "0"
    {
        "path"      "C:\\Program Files (x86)\\Steam"
        "label"     ""
        "apps"
        {
            "12210"     "15731254271"
        }
    }
    "1"
    {
        "path"      "D:\\SteamLibrary"
    }
    // comment
    "2" { "path" "E:\\)" "\xD0\x98\xD0\xB3\xD1\x80\xD1\x8B" R"(" }
})";
    auto folders = ParseSteamLibraryFolders(vdf);
    REQUIRE(folders.size() == 3);
    CHECK(folders[0] == std::filesystem::path(L"C:\\Program Files (x86)\\Steam"));
    CHECK(folders[1] == std::filesystem::path(L"D:\\SteamLibrary"));
    CHECK(folders[2] == std::filesystem::path(L"E:\\\u0418\u0433\u0440\u044B"));
}

TEST_CASE(Vdf_LibraryFoldersLegacyFormat)
{
    std::string vdf = "\"LibraryFolders\"\n{\n\t\"TimeNextStatsReport\"\t\t\"1560000000\"\n\t\"ContentStatsID\"\t\t\"-123\"\n\t\"1\"\t\t\"D:\\\\Games\\\\Steam\"\n}\n";
    auto folders = ParseSteamLibraryFolders(vdf);
    REQUIRE(folders.size() == 1);
    CHECK(folders[0] == std::filesystem::path(L"D:\\Games\\Steam"));
}

TEST_CASE(Vdf_AppManifest)
{
    std::string acf = "\"AppState\"\n{\n\t\"appid\"\t\t\"12210\"\n\t\"name\"\t\t\"Grand Theft Auto IV: The Complete Edition\"\n\t\"installdir\"\t\t\"Grand Theft Auto IV\"\n\t\"UserConfig\"\n\t{\n\t\t\"language\"\t\t\"english\"\n\t}\n}\n";
    SteamAppManifest manifest;
    REQUIRE(ParseSteamAppManifest(acf, manifest));
    CHECK_EQ(manifest.appId, std::string("12210"));
    CHECK_EQ(manifest.installDir, std::string("Grand Theft Auto IV"));
    CHECK(!ParseSteamAppManifest("not a vdf {", manifest));
}

TEST_CASE(Remote_ParseGitHubRepo)
{
    std::string user, repo;
    CHECK(ParseGitHubRepo("https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix", user, repo));
    CHECK_EQ(user, std::string("ThirteenAG"));
    CHECK_EQ(repo, std::string("GTAIV.EFLC.FusionFix"));
    CHECK(ParseGitHubRepo("https://github.com/ThirteenAG/modupdater/releases/latest", user, repo) && repo == "modupdater");
    CHECK(ParseGitHubRepo("https://api.github.com/repos/ThirteenAG/modupdater/releases", user, repo) && user == "ThirteenAG" && repo == "modupdater");
    CHECK(ParseGitHubRepo("https://github.com/u/r.git", user, repo) && repo == "r");
    CHECK(ParseGitHubRepo("https://github.com/u/r?tab=readme", user, repo) && repo == "r");
    CHECK(!ParseGitHubRepo("https://example.com/u/r", user, repo));
    CHECK(!ParseGitHubRepo("https://github.com/onlyuser", user, repo));
}

TEST_CASE(Remote_ContentDisposition)
{
    CHECK_EQ(ParseContentDispositionFileName("attachment; filename=GTAIV.EFLC.FusionFix.zip"), std::string("GTAIV.EFLC.FusionFix.zip"));
    CHECK_EQ(ParseContentDispositionFileName("attachment; filename=\"My Mod.zip\"; size=10"), std::string("My Mod.zip"));
    CHECK_EQ(ParseContentDispositionFileName("attachment; filename*=UTF-8''My%20Mod.zip"), std::string("My Mod.zip"));
    CHECK_EQ(ParseContentDispositionFileName("attachment; filename=\"fallback.zip\"; filename*=UTF-8''real.zip"), std::string("real.zip"));
    CHECK_EQ(ParseContentDispositionFileName("attachment; filename=\"../../evil.dll\""), std::string("evil.dll"));
    CHECK_EQ(ParseContentDispositionFileName(""), std::string(""));
}

TEST_CASE(Remote_LinkHeader)
{
    CHECK_EQ(ParseLastPageFromLinkHeader("<https://api.github.com/repositories/1/releases?per_page=100&page=2>; rel=\"next\", <https://api.github.com/repositories/1/releases?per_page=100&page=5>; rel=\"last\""), 5);
    CHECK_EQ(ParseLastPageFromLinkHeader(""), 1);
    CHECK_EQ(ParseLastPageFromLinkHeader("<https://x/releases?page=3>; rel=\"last\""), 3);
    CHECK_EQ(ParseLastPageFromLinkHeader("garbage"), 1); // used to throw in std::stoi
}

TEST_CASE(Remote_Dates)
{
    std::chrono::system_clock::time_point t;
    REQUIRE(ParseHttpDate("Tue, 12 May 2026 07:29:48 GMT", t));
    std::chrono::system_clock::time_point iso;
    REQUIRE(ParseIsoDate("2026-05-12T07:29:48Z", iso));
    CHECK(t == iso);
    CHECK(!ParseHttpDate("yesterday", t));
    CHECK_EQ(HoursSince(iso, iso + std::chrono::hours(5) + std::chrono::minutes(59)), 5);
    CHECK_EQ(HoursSince(iso, iso - std::chrono::hours(3)), 0); // clock skew
}

TEST_CASE(Remote_SelectGitHubAsset)
{
    auto now = std::chrono::system_clock::now();
    auto iso = [&](int hoursAgo)
    {
        auto t = std::chrono::system_clock::to_time_t(now - std::chrono::hours(hoursAgo));
        std::tm tm;
        gmtime_s(&tm, &t);
        char buffer[32];
        strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
        return std::string(buffer);
    };

    Json::Value releases(Json::arrayValue);
    auto addRelease = [&](bool draft, bool prerelease, std::vector<std::pair<std::string, int>> assets)
    {
        Json::Value release;
        release["draft"] = draft;
        release["prerelease"] = prerelease;
        release["assets"] = Json::Value(Json::arrayValue);
        for (auto& [name, hours] : assets)
        {
            Json::Value asset;
            asset["name"] = name;
            asset["updated_at"] = iso(hours);
            asset["browser_download_url"] = "https://github.com/u/r/releases/download/v/" + name;
            asset["size"] = 1234;
            release["assets"].append(asset);
        }
        releases.append(release);
    };

    addRelease(true, false, { { "Mod.zip", 1 } });                          // draft, ignored
    addRelease(false, false, { { "Mod_x64.zip", 10 }, { "Mod_x86.zip", 12 }, { "Other.zip", 2 } });
    addRelease(false, false, { { "Mod.zip", 50 } });

    auto x86 = SelectGitHubAsset(releases, L"Mod.asi", L"x86", now);
    CHECK_EQ(x86.name, std::string("Mod_x86.zip"));
    CHECK_EQ(x86.hoursAgo, 12);
    CHECK_EQ(x86.size, uint64_t(1234));

    auto x64 = SelectGitHubAsset(releases, L"mod.asi", L"x64", now);
    CHECK_EQ(x64.name, std::string("Mod_x64.zip"));

    auto any = SelectGitHubAsset(releases, L"Mod.asi", L"", now);
    CHECK_EQ(any.name, std::string("Mod_x64.zip")); // newest match

    // nothing matches the name: the latest release is used when it has a single asset
    Json::Value single(Json::arrayValue);
    Json::Value release;
    release["draft"] = false;
    release["prerelease"] = false;
    release["assets"] = Json::Value(Json::arrayValue);
    Json::Value asset;
    asset["name"] = "Something.zip";
    asset["updated_at"] = iso(3);
    asset["browser_download_url"] = "https://example.com/Something.zip";
    asset["size"] = 1;
    release["assets"].append(asset);
    single.append(release);
    auto fallback = SelectGitHubAsset(single, L"Mod.asi", L"", now);
    CHECK_EQ(fallback.name, std::string("Something.zip"));
    CHECK(fallback.found());

    CHECK(!SelectGitHubAsset(Json::Value(Json::arrayValue), L"Mod.asi", L"", now).found());
}

TEST_CASE(Registry_SettingsAndSnapshot)
{
    auto module = reinterpret_cast<HMODULE>(static_cast<uintptr_t>(0x12340000));
    muSetUpdateURL(module, "https://example.com/Mod.zip");
    muSetAlwaysUpdate(module, true);
    muSetInstallerColor(module, MU_COLOR_BUTTON, RGB(1, 2, 3));
    muSetInstallerColor(module, 999, RGB(1, 2, 3)); // ignored
    muSetInstallerString(module, MU_STR_INSTALL, "Install now");
    muAddInstallerPath(module, "C:\\A");
    muAddInstallerPath(module, "C:\\B");
    uint8_t blob[] = { 1, 2, 3 };
    muSetInstallerLogo(module, blob, sizeof(blob));

    auto snapshot = RegistryGetSnapshot();
    REQUIRE(snapshot.contains(module));
    auto& s = snapshot[module];
    CHECK_EQ(GetString(s, K(Key::UpdateUrl)), std::string("https://example.com/Mod.zip"));
    CHECK_EQ(GetInt(s, K(Key::AlwaysUpdate)), int64_t(1));
    CHECK_EQ(GetInt(s, Key::InstallerColorBase + MU_COLOR_BUTTON), int64_t(RGB(1, 2, 3)));
    CHECK(!HasValue(s, Key::InstallerColorBase + 999));
    CHECK_EQ(GetString(s, Key::InstallerStringBase + MU_STR_INSTALL), std::string("Install now"));
    CHECK_EQ(GetString(s, K(Key::InstallerExtraPaths)), std::string("C:\\A\nC:\\B"));
    REQUIRE(FindValue(s, K(Key::InstallerLogo)) != nullptr);
    CHECK_EQ(FindValue(s, K(Key::InstallerLogo))->blob.size(), size_t(3));
}

TEST_CASE(Registry_ClaimOnce)
{
    constexpr uint32_t what = 0x7E57;
    CHECK(RegistryClaim(what));
    CHECK(!RegistryClaim(what));
    CHECK(!RegistryClaim(what));
}

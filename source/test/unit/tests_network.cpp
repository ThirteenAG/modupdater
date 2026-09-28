#include "stdafx.h"
#include "test.h"
#include "http.h"
#include "remote.h"
#include "updater.h"
#include "fileops.h"
#include "string_funcs.h"
#include "common/test_server.h"
#include "common/test_util.h"

using namespace mu;

TEST_CASE(Http_HeadGetProbe)
{
    test::Server server;
    REQUIRE(server.Start());
    test::Resource file;
    file.body = "hello world";
    file.lastModified = test::HttpDate(5);
    file.contentDisposition = "attachment; filename=Hello.zip";
    server.Set("/file", file);

    auto head = http::Head(server.Url("/file"));
    CHECK(head.ok());
    CHECK_EQ(head.Header("content-length"), std::string("11"));
    CHECK_EQ(head.Header("Content-Disposition"), file.contentDisposition);
    CHECK(head.body.empty());

    auto get = http::Get(server.Url("/file"));
    CHECK_EQ(get.body, std::string("hello world"));

    auto missing = http::Get(server.Url("/missing"));
    CHECK_EQ(missing.status, long(404));
    CHECK(missing.Describe() == L"HTTP 404 Not Found");

    // servers that refuse HEAD
    file.headStatus = 405;
    server.Set("/nohead", file);
    auto probe = http::Probe(server.Url("/nohead"));
    CHECK(probe.ok());
    CHECK_EQ(probe.Header("Content-Length"), std::string("11"));

    auto unreachable = http::Head("http://127.0.0.1:1/nothing");
    CHECK(!unreachable.ok());
    CHECK(!unreachable.error.empty());
}

TEST_CASE(Http_TokenOnlyForGitHub)
{
    CHECK(http::IsGitHubHost("https://github.com/u/r"));
    CHECK(http::IsGitHubHost("https://api.github.com/repos/u/r/releases"));
    CHECK(!http::IsGitHubHost("https://github.com.evil.example/u/r"));
    CHECK(!http::IsGitHubHost("https://example.com/github.com/u/r"));
    CHECK(!http::IsGitHubHost("https://objects.githubusercontent.com/x"));
}

TEST_CASE(Http_DownloadToFile)
{
    test::Server server;
    REQUIRE(server.Start());
    test::Resource file;
    file.body = test::RandomData(3 * 1024 * 1024);
    server.Set("/big.zip", file);

    test::TempDir dir(L"download");
    uint64_t lastDone = 0, lastTotal = 0;
    auto result = http::DownloadToFile(server.Url("/big.zip"), dir / L"big.zip", [&](uint64_t done, uint64_t total)
    {
        lastDone = done;
        lastTotal = total;
        return true;
    });
    CHECK(result.ok);
    CHECK_EQ(result.bytes, uint64_t(file.body.size()));
    CHECK_EQ(lastTotal, uint64_t(file.body.size()));
    CHECK(test::ReadFile(dir / L"big.zip") == file.body);
}

TEST_CASE(Http_DownloadResumesAfterDisconnect)
{
    test::Server server;
    REQUIRE(server.Start());
    test::Resource file;
    file.body = test::RandomData(2 * 1024 * 1024, 3);
    file.dropAfter = 700 * 1024; // the first connection breaks
    server.Set("/drop.zip", file);

    test::TempDir dir(L"resume");
    auto result = http::DownloadToFile(server.Url("/drop.zip"), dir / L"drop.zip", {});
    CHECK(result.ok);
    CHECK(test::ReadFile(dir / L"drop.zip") == file.body);

    auto log = server.RequestLog();
    bool resumed = std::any_of(log.begin(), log.end(), [](const std::string& l) { return l.find("Range: bytes=") != std::string::npos; });
    CHECK(resumed);
    for (auto& l : log)
        test::Note(l);
}

TEST_CASE(Http_DownloadRestartsWhenRangeIsIgnored)
{
    test::Server server;
    REQUIRE(server.Start());
    test::Resource file;
    file.body = test::RandomData(1024 * 1024, 4);
    file.dropAfter = 300 * 1024;
    file.ranges = false;
    server.Set("/norange.zip", file);

    test::TempDir dir(L"norange");
    auto result = http::DownloadToFile(server.Url("/norange.zip"), dir / L"norange.zip", {});
    CHECK(result.ok);
    CHECK(test::ReadFile(dir / L"norange.zip") == file.body);
}

TEST_CASE(Http_DownloadErrorsAndCancel)
{
    test::Server server;
    REQUIRE(server.Start());
    test::TempDir dir(L"errors");

    auto missing = http::DownloadToFile(server.Url("/missing.zip"), dir / L"missing.zip", {});
    CHECK(!missing.ok);
    CHECK(missing.error == L"HTTP 404 Not Found");
    CHECK(!test::Exists(dir / L"missing.zip"));

    test::Resource slow;
    slow.body = test::RandomData(512 * 1024);
    slow.bytesPerSecond = 64 * 1024;
    server.Set("/slow.zip", slow);
    int calls = 0;
    auto cancelled = http::DownloadToFile(server.Url("/slow.zip"), dir / L"slow.zip", [&](uint64_t, uint64_t) { return ++calls < 5; });
    CHECK(!cancelled.ok);
    CHECK(cancelled.cancelled);
    CHECK(!test::Exists(dir / L"slow.zip"));
}

TEST_CASE(Remote_DirectLink)
{
    test::Server server;
    REQUIRE(server.Start());
    test::Resource file;
    file.body = "zipdata";
    file.lastModified = test::HttpDate(30);
    file.contentDisposition = "attachment; filename=\"Mod v2.zip\"";
    server.Set("/download", file);

    auto info = GetRemoteFileInfo(L"Mod.asi", server.Url("/download"), L"", "");
    REQUIRE(info.found());
    CHECK_EQ(info.name, std::string("Mod v2.zip"));
    CHECK_EQ(info.size, uint64_t(7));
    CHECK(info.hoursAgo >= 29 && info.hoursAgo <= 31);

    // redirected download, name from the final URL
    test::Resource redirect;
    redirect.redirect = "/files/Mod.zip";
    server.Set("/latest", redirect);
    test::Resource target;
    target.body = "abc";
    server.Set("/files/Mod.zip", target);
    auto redirected = GetRemoteFileInfo(L"Mod.asi", server.Url("/latest"), L"", "");
    REQUIRE(redirected.found());
    CHECK_EQ(redirected.name, std::string("Mod.zip"));
    CHECK_EQ(redirected.hoursAgo, kRemoteDateUnknown);

    auto broken = GetRemoteFileInfo(L"Mod.asi", server.Url("/nothing"), L"", "");
    CHECK(!broken.found());
    CHECK(!broken.error.empty());
}

TEST_CASE(Remote_GitHubReleases)
{
    test::Server server;
    REQUIRE(server.Start());
    SetGitHubApiBase(server.Url("/api"));

    // the repository page answers with HTML, the API with the releases
    test::Resource page;
    page.contentType = "text/html; charset=utf-8";
    page.body = "<html></html>";
    server.Set("/github.com/User/Repo", page);

    test::Resource releases;
    releases.contentType = "application/json";
    releases.body = R"([
        { "draft": false, "prerelease": false, "assets": [
            { "name": "Mod_x64.zip", "updated_at": ")" + test::IsoDate(4) + R"(", "browser_download_url": "https://example.com/Mod_x64.zip", "size": 100 },
            { "name": "Mod_x86.zip", "updated_at": ")" + test::IsoDate(5) + R"(", "browser_download_url": "https://example.com/Mod_x86.zip", "size": 90 } ] }
    ])";
    server.Set("/api/repos/User/Repo/releases", releases);

    auto info = GetRemoteFileInfo(L"Mod.asi", server.Url("/github.com/User/Repo"), L"x86", "");
    REQUIRE(info.found());
    CHECK_EQ(info.name, std::string("Mod_x86.zip"));
    CHECK_EQ(info.url, std::string("https://example.com/Mod_x86.zip"));
    CHECK_EQ(info.size, uint64_t(90));

    auto installer = GetInstallerDownloadInfo(server.Url("/github.com/User/Repo"), "");
    REQUIRE(installer.found());
    CHECK_EQ(installer.name, std::string("Mod_x64.zip")); // first zip of the latest release

    SetGitHubApiBase("https://api.github.com");
}

TEST_CASE(Updater_CheckForUpdates)
{
    test::Server server;
    REQUIRE(server.Start());
    test::TempDir dir(L"check");
    auto plugin = dir / L"game" / L"plugins" / L"Mod.asi";
    test::WriteFile(plugin, "old");
    test::SetFileTimeHoursAgo(plugin, 48);

    test::Resource newer;
    newer.body = "zip";
    newer.lastModified = test::HttpDate(2);
    server.Set("/newer.zip", newer);
    test::Resource older;
    older.body = "zip";
    older.lastModified = test::HttpDate(100);
    server.Set("/older.zip", older);

    FILETIME lastWrite = {};
    WIN32_FILE_ATTRIBUTE_DATA data;
    REQUIRE(GetFileAttributesExW(plugin.c_str(), GetFileExInfoStandard, &data));
    lastWrite = data.ftLastWriteTime;

    auto candidate = [&](const std::string& url, const std::string& dev = {}, bool always = false)
    {
        updater::Candidate c;
        c.path = plugin;
        c.url = toWString(url);
        c.devUrl = toWString(dev);
        c.lastWriteTime = lastWrite;
        c.alwaysUpdate = always;
        return c;
    };

    CHECK_EQ(updater::CheckForUpdates({ candidate(server.Url("/newer.zip")) }, "").size(), size_t(1));
    CHECK_EQ(updater::CheckForUpdates({ candidate(server.Url("/older.zip")) }, "").size(), size_t(0));
    CHECK_EQ(updater::CheckForUpdates({ candidate(server.Url("/older.zip"), {}, true) }, "").size(), size_t(1));
    // a broken dev url must not hide the release url (it did before)
    CHECK_EQ(updater::CheckForUpdates({ candidate(server.Url("/newer.zip"), server.Url("/broken.zip")) }, "").size(), size_t(1));
    CHECK_EQ(updater::CheckForUpdates({ candidate(server.Url("/missing.zip")) }, "").size(), size_t(0));
}

TEST_CASE(Updater_ApplyUpdate)
{
    test::Server server;
    REQUIRE(server.Start());
    test::TempDir dir(L"apply");
    auto game = dir / L"game";
    auto plugin = game / L"plugins" / L"Mod.asi";
    test::WriteFile(plugin, "old asi");
    test::WriteFile(game / L"plugins" / L"Mod.ini", "[MAIN]\nSetting = 7\n");

    test::Resource zip;
    zip.body = test::CreateZipData({
        { "plugins/Mod.asi", "new asi" },
        { "plugins/Mod.ini", "[MAIN]\nSetting = 1\nAdded = 2\n" },
        { "update/data.img", test::RandomData(100000) },
    });
    zip.contentDisposition = "attachment; filename=Mod.zip";
    server.Set("/Mod.zip", zip);

    FileUpdateInfo update;
    update.wszFullFilePath = plugin.wstring();
    update.wszFileName = L"Mod.asi";
    update.wszDownloadURL = toWString(server.Url("/Mod.zip"));
    update.wszDownloadName = L"Mod.zip";
    update.nFileSize = zip.body.size();

    std::vector<std::wstring> statuses;
    auto result = updater::ApplyUpdate(update, IniMode::Merge, "", [&](const updater::Status& s)
    {
        if (!s.text.empty())
            statuses.push_back(s.text);
        return true;
    });
    CHECK(result.ok());
    for (auto& e : result.errors)
        test::Note(toString(e));
    CHECK_EQ(test::ReadFile(plugin), std::string("new asi"));
    auto ini = test::ReadFile(game / L"plugins" / L"Mod.ini");
    CHECK(ini.find("Setting = 7") != std::string::npos);
    CHECK(ini.find("Added = 2") != std::string::npos);
    CHECK(test::Exists(game / L"update" / L"data.img"));      // extracted relative to the game folder
    CHECK(!test::Exists(game / L"plugins" / L"Mod.zip.modupdater"));
    CHECK(!statuses.empty());

    // a download that fails is reported instead of "Update completed successfully"
    update.wszDownloadURL = toWString(server.Url("/gone.zip"));
    auto failed = updater::ApplyUpdate(update, IniMode::Merge, "", {});
    CHECK(!failed.ok());
    CHECK_EQ(failed.errors.size(), size_t(1));

    // an archive without the file is not extracted somewhere random
    test::Resource wrong;
    wrong.body = test::CreateZipData({ { "Other.dll", "x" } });
    server.Set("/Wrong.zip", wrong);
    update.wszDownloadURL = toWString(server.Url("/Wrong.zip"));
    update.wszDownloadName = L"Wrong.zip";
    auto mismatch = updater::ApplyUpdate(update, IniMode::Merge, "", {});
    CHECK(!mismatch.ok());
    CHECK(!test::Exists(game / L"plugins" / L"Other.dll"));

    // a single file download replaces the file directly
    test::Resource single;
    single.body = "single file";
    server.Set("/Mod.asi", single);
    update.wszDownloadURL = toWString(server.Url("/Mod.asi"));
    update.wszDownloadName = L"Mod.asi";
    CHECK(updater::ApplyUpdate(update, IniMode::Merge, "", {}).ok());
    CHECK_EQ(test::ReadFile(plugin), std::string("single file"));
}

// The real thing, only with "UnitTests.exe Online": TLS through Schannel, the GitHub API,
// release downloads redirected to GitHub's download servers
TEST_CASE(Online_GitHubHttps)
{
    auto api = http::Get("https://api.github.com/repos/curl/curl/releases/latest");
    test::Note(std::format("GitHub API: {}, {} bytes", toString(api.Describe()), api.body.size()));
    CHECK_EQ(api.status, 200L);
    CHECK(api.body.find("\"tag_name\"") != std::string::npos);

    auto info = GetInstallerDownloadInfo("https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix/releases/latest/download/GTAIV.EFLC.FusionFix.zip", "");
    auto host = info.url.substr(0, info.url.find('/', info.url.find("://") + 3));
    test::Note(std::format("Fusion Fix: {} bytes from {}", info.size, host));
    CHECK(info.found());
    CHECK(info.size > 1024 * 1024);

    test::TempDir dir(L"online");
    auto file = dir / L"curl.tar.gz";
    auto result = http::DownloadToFile("https://github.com/curl/curl/releases/download/curl-8_22_0/curl-8.22.0.tar.gz", file, {});
    test::Note(std::format("download: {} bytes {}", result.bytes, toString(result.error)));
    CHECK(result.ok);
    CHECK(result.bytes > 1024 * 1024);
    CHECK(test::ReadFile(file).starts_with("\x1f\x8b")); // gzip
}

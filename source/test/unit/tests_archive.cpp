#include "stdafx.h"
#include "test.h"
#include "archive.h"
#include "extract.h"
#include "fileops.h"
#include "string_funcs.h"
#include "common/test_util.h"

using namespace mu;
using test::ZipItem;

namespace
{
    std::string ExtractString(zip::Reader& reader, const std::wstring& name, const std::string& password = {})
    {
        auto entry = reader.Find(name);
        if (!entry)
            return "<missing>";
        std::vector<uint8_t> data;
        if (!reader.ExtractToMemory(*entry, password, data))
            return "<error>";
        return std::string(data.begin(), data.end());
    }

    std::filesystem::path SmallExecutable()
    {
        // any PE file works as the base of an offline installer
        return test::BinDir() / L"TestDLL1.asi";
    }
}

TEST_CASE(Zip_ReadEntries)
{
    test::TempDir dir(L"zip");
    auto file = dir / L"тест.zip"; // unicode archive path
    auto big = test::RandomData(3 * 1024 * 1024, 7);
    REQUIRE(test::CreateZip(file, {
        { "plugins/", "" },
        { "plugins/Mod.asi", std::string("binary\0data", 11) },
        { "plugins/Mod.ini", "[MAIN]\nValue=1\n" },
        { "empty.txt", "" },
        { "data/big.bin", big },
        { "\xD1\x84\xD0\xB0\xD0\xB9\xD0\xBB.txt", "unicode" }, // файл.txt
    }));

    zip::Reader reader;
    REQUIRE(reader.Open(file));
    CHECK_EQ(reader.Entries().size(), size_t(6));
    CHECK(reader.Find(L"plugins/")->isDirectory);
    CHECK(reader.Find(L"PLUGINS\\MOD.ASI") != nullptr);
    CHECK_EQ(ExtractString(reader, L"plugins/Mod.ini"), std::string("[MAIN]\nValue=1\n"));
    CHECK_EQ(ExtractString(reader, L"empty.txt"), std::string());
    CHECK(ExtractString(reader, L"data/big.bin") == big);
    CHECK_EQ(ExtractString(reader, L"файл.txt"), std::string("unicode"));
    CHECK_EQ(reader.TotalUncompressedSize(), uint64_t(big.size() + 11 + 15 + 7));
}

TEST_CASE(Zip_DetectsCorruption)
{
    test::TempDir dir(L"zip");
    auto file = dir / L"corrupt.zip";
    REQUIRE(test::CreateZip(file, { { "a.txt", std::string(100000, 'x') + test::RandomData(1000) } }));

    // flip a byte in the middle of the compressed data
    auto data = test::ReadFile(file);
    data[data.size() / 3] ^= 0x55;
    REQUIRE(test::WriteFile(file, data));

    zip::Reader reader;
    REQUIRE(reader.Open(file));
    std::vector<uint8_t> out;
    CHECK(!reader.ExtractToMemory(reader.Entries()[0], {}, out));
    CHECK(!reader.Error().empty());
    test::Note(toString(reader.Error()));
}

TEST_CASE(Zip_NotAZip)
{
    test::TempDir dir(L"zip");
    auto file = dir / L"page.zip";
    REQUIRE(test::WriteFile(file, "<html>404</html>"));
    zip::Reader reader;
    CHECK(!reader.Open(file));
    CHECK(!reader.Error().empty());
    CHECK(!reader.Open(dir / L"missing.zip"));
}

TEST_CASE(Zip_Password)
{
    test::TempDir dir(L"zip");
    auto file = dir / L"secret.zip";
    REQUIRE(test::CreateZip(file, { { "secret.txt", "top secret content" } }, "pa55"));

    zip::Reader reader;
    REQUIRE(reader.Open(file));
    CHECK(reader.HasEncryptedEntries());
    CHECK_EQ(ExtractString(reader, L"secret.txt"), std::string("<error>"));          // no password
    CHECK_EQ(ExtractString(reader, L"secret.txt", "wrong"), std::string("<error>")); // wrong password
    CHECK_EQ(ExtractString(reader, L"secret.txt", "pa55"), std::string("top secret content"));
}

TEST_CASE(Embedded_AppendAndFind)
{
    test::TempDir dir(L"embedded");
    auto zip1 = dir / L"First.zip";
    auto zip2 = dir / L"Второй.zip";
    REQUIRE(test::CreateZip(zip1, { { "one.txt", "1" } }));
    REQUIRE(test::CreateZip(zip2, { { "two.txt", "2" } }));

    auto installer1 = dir / L"installer1.exe";
    auto installer2 = dir / L"installer2.exe";
    std::wstring error;
    REQUIRE(embedded::Append(SmallExecutable(), zip1, installer1, &error));
    REQUIRE(embedded::Append(installer1, zip2, installer2, &error));

    auto archives = embedded::Find(installer2);
    REQUIRE(archives.size() == 2);
    CHECK(archives[0].name == L"First.zip");       // in the order they were appended
    CHECK(archives[1].name == L"Второй.zip");

    zip::Reader reader;
    REQUIRE(reader.Open(installer2, archives[1].offset, archives[1].size));
    CHECK_EQ(ExtractString(reader, L"two.txt"), std::string("2"));
    REQUIRE(reader.Open(installer2, archives[0].offset, archives[0].size));
    CHECK_EQ(ExtractString(reader, L"one.txt"), std::string("1"));

    CHECK(embedded::Find(SmallExecutable()).empty());
    CHECK(!embedded::Append(SmallExecutable(), dir / L"missing.zip", dir / L"x.exe", &error));
}

TEST_CASE(Embedded_SignedInstaller)
{
    // Signing an offline installer puts the certificate table (and alignment padding) after the
    // archives. Old versions looked for the marker at the very end of the file and fell back to
    // downloading everything.
    test::TempDir dir(L"signed");
    auto zip = dir / L"Mod.zip";
    REQUIRE(test::CreateZip(zip, { { "plugins/Mod.asi", "asi" } }));
    auto installer = dir / L"installer.exe";
    REQUIRE(embedded::Append(SmallExecutable(), zip, installer));
    REQUIRE(test::AddFakeSignature(installer));

    auto archives = embedded::Find(installer);
    REQUIRE(archives.size() == 1);
    zip::Reader reader;
    REQUIRE(reader.Open(installer, archives[0].offset, archives[0].size));
    CHECK_EQ(ExtractString(reader, L"plugins/Mod.asi"), std::string("asi"));

    // appending to a signed installer removes the signature and keeps the chain intact
    auto zip2 = dir / L"Extra.zip";
    REQUIRE(test::CreateZip(zip2, { { "extra.txt", "extra" } }));
    auto installer2 = dir / L"installer2.exe";
    REQUIRE(embedded::Append(installer, zip2, installer2));
    auto chained = embedded::Find(installer2);
    REQUIRE(chained.size() == 2);
    CHECK(chained[0].name == L"Mod.zip");

    HANDLE h = CreateFileW(installer2.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    REQUIRE(h != INVALID_HANDLE_VALUE);
    LARGE_INTEGER size;
    GetFileSizeEx(h, &size);
    CHECK_EQ(embedded::GetDataEnd(h, size.QuadPart), static_cast<uint64_t>(size.QuadPart)); // no certificate any more
    CloseHandle(h);
}

TEST_CASE(Extract_WritesFilesAndFolders)
{
    test::TempDir dir(L"extract");
    auto zip = dir / L"Mod.zip";
    REQUIRE(test::CreateZip(zip, {
        { "plugins/", "" },
        { "plugins/Mod.asi", "new asi" },
        { "update/data/empty.dat", "" },
        { "readme.txt", "readme" },
    }));

    auto game = dir / L"game";
    test::WriteFile(game / L"plugins" / L"Mod.asi", "old asi");
    SetFileAttributesW((game / L"plugins" / L"Mod.asi").c_str(), FILE_ATTRIBUTE_READONLY);

    zip::Reader reader;
    REQUIRE(reader.Open(zip));
    ExtractReport report;
    std::vector<uint64_t> progress;
    bool ok = ExtractArchive(reader, game, {}, [&](const ExtractProgress& p) { progress.push_back(p.done); return true; }, report);
    CHECK(ok);
    CHECK(report.errors.empty());
    CHECK_EQ(report.written, 3);
    CHECK_EQ(test::ReadFile(game / L"plugins" / L"Mod.asi"), std::string("new asi")); // read-only file replaced
    CHECK(test::Exists(game / L"update" / L"data" / L"empty.dat"));                // zero byte files are created
    CHECK_EQ(test::ReadFile(game / L"readme.txt"), std::string("readme"));
    CHECK(!progress.empty() && progress.back() == reader.TotalUncompressedSize());
    CHECK(!test::Exists(game / L"plugins" / L"Mod.asi.mu-tmp"));
}

TEST_CASE(Extract_RejectsZipSlipBeforeWriting)
{
    test::TempDir dir(L"zipslip");
    auto zip = dir / L"evil.zip";
    REQUIRE(test::CreateZip(zip, { { "good.txt", "good" }, { "../evil.txt", "evil" } }));

    zip::Reader reader;
    REQUIRE(reader.Open(zip));
    ExtractReport report;
    auto game = dir / L"game";
    CHECK(!ExtractArchive(reader, game, {}, {}, report));
    REQUIRE(!report.errors.empty());
    test::Note(toString(report.errors.front()));
    CHECK(!test::Exists(game / L"good.txt"));   // nothing is written
    CHECK(!test::Exists(dir / L"evil.txt"));
}

TEST_CASE(Extract_IniModes)
{
    test::TempDir dir(L"ini");
    auto zip = dir / L"Mod.zip";
    REQUIRE(test::CreateZip(zip, { { "Mod.ini", "; new file\n[MAIN]\nA = 10\nB = 20\nNewKey = 3\n\n[EXTRA]\nC = 5\n" } }));

    auto run = [&](IniMode mode) -> std::string
    {
        auto game = dir / (L"game" + std::to_wstring(static_cast<int>(mode)));
        test::WriteFile(game / L"Mod.ini", "[MAIN]\nA = 1\nB = 2\nUserKey = user\n");
        zip::Reader reader;
        if (!reader.Open(zip))
            return "<open failed>";
        ExtractOptions options;
        options.iniMode = mode;
        ExtractReport report;
        ExtractArchive(reader, game, options, {}, report);
        return test::ReadFile(game / L"Mod.ini");
    };

    // the layout of the new file with the user's values, new keys, the user's own keys at the end of their section
    auto merged = run(IniMode::Merge);
    CHECK_EQ(merged, std::string("; new file\n[MAIN]\nA = 1\nB = 2\nNewKey = 3\nUserKey = user\n\n[EXTRA]\nC = 5\n"));

    auto replaced = run(IniMode::Replace);
    CHECK(replaced.find("A = 10") != std::string::npos);
    CHECK(replaced.find("UserKey") == std::string::npos);

    auto skipped = run(IniMode::Skip);
    CHECK_EQ(skipped, std::string("[MAIN]\nA = 1\nB = 2\nUserKey = user\n"));
}

TEST_CASE(Extract_ReplacesLoadedModule)
{
    // A loaded dll cannot be overwritten, but it can be renamed
    test::TempDir dir(L"inuse");
    auto game = dir / L"game";
    std::filesystem::create_directories(game);
    auto plugin = game / L"Plugin.asi";
    REQUIRE(CopyFileW((test::BinDir() / L"TestDLL2.asi").c_str(), plugin.c_str(), FALSE));
    HMODULE loaded = LoadLibraryExW(plugin.c_str(), nullptr, LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    REQUIRE(loaded != nullptr);

    auto zip = dir / L"Mod.zip";
    REQUIRE(test::CreateZip(zip, { { "Plugin.asi", "new plugin" } }));
    zip::Reader reader;
    REQUIRE(reader.Open(zip));
    ExtractReport report;
    CHECK(ExtractArchive(reader, game, {}, {}, report));
    CHECK_EQ(report.inUse, 1);
    CHECK_EQ(test::ReadFile(plugin), std::string("new plugin"));
    CHECK(test::Exists(game / L"Plugin.asi.deleteonnextlaunch"));

    FreeLibrary(loaded);
    CleanupLeftovers(game);
    CHECK(!test::Exists(game / L"Plugin.asi.deleteonnextlaunch"));
}

TEST_CASE(Extract_ReportsLockedFiles)
{
    test::TempDir dir(L"locked");
    auto game = dir / L"game";
    auto locked = game / L"locked.img";
    test::WriteFile(locked, "old");
    // the game keeps its archives open without sharing delete access
    HANDLE h = CreateFileW(locked.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    REQUIRE(h != INVALID_HANDLE_VALUE);

    auto zip = dir / L"Mod.zip";
    REQUIRE(test::CreateZip(zip, { { "locked.img", "new" }, { "other.txt", "other" } }));
    zip::Reader reader;
    REQUIRE(reader.Open(zip));
    ExtractReport report;
    CHECK(!ExtractArchive(reader, game, {}, {}, report));
    CloseHandle(h);

    REQUIRE(report.errors.size() == 1);
    test::Note(toString(report.errors.front()));
    CHECK(report.errors.front().find(L"locked.img") != std::wstring::npos);
    CHECK_EQ(test::ReadFile(locked), std::string("old"));
    CHECK_EQ(test::ReadFile(game / L"other.txt"), std::string("other")); // the rest is still installed
    CHECK(!test::Exists(game / L"locked.img.mu-tmp"));
}

TEST_CASE(Extract_PendingLockedFiles)
{
    // The updater runs inside the game: an archive it keeps open waits next to it for the next launch
    test::TempDir dir(L"pending");
    auto game = dir / L"game";
    auto locked = game / L"locked.img";
    auto pending = game / L"locked.img.mu-pending";
    test::WriteFile(locked, "old");
    HANDLE h = CreateFileW(locked.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    REQUIRE(h != INVALID_HANDLE_VALUE);

    auto zip = dir / L"Mod.zip";
    REQUIRE(test::CreateZip(zip, { { "locked.img", "new" }, { "other.txt", "other" } }));
    zip::Reader reader;
    REQUIRE(reader.Open(zip));
    ExtractOptions options;
    options.allowPending = true;
    ExtractReport report;
    CHECK(ExtractArchive(reader, game, options, {}, report));
    CHECK(report.errors.empty());
    REQUIRE(report.pending.size() == 1);
    CHECK(report.pending.front().name == L"locked.img");
    CHECK(report.pending.front().file == pending);
    CHECK_EQ(test::ReadFile(locked), std::string("old"));
    CHECK_EQ(test::ReadFile(pending), std::string("new"));
    CHECK_EQ(test::ReadFile(game / L"other.txt"), std::string("other"));
    CHECK(!test::Exists(game / L"locked.img.mu-tmp"));

    // still open: it keeps waiting
    auto list = dir / L"modupdater.pending";
    REQUIRE(AddPendingFiles(list, { pending }));
    REQUIRE(AddPendingFiles(list, { pending })); // no duplicates
    auto busy = ApplyPendingFiles(list);
    CHECK(busy.applied.empty());
    REQUIRE(busy.remaining.size() == 1);
    CHECK_EQ(test::ReadFile(list), toString(pending.wstring()) + "\r\n");
    CHECK_EQ(test::ReadFile(locked), std::string("old"));

    // closed: it is put in place and the list goes away
    CloseHandle(h);
    auto done = ApplyPendingFiles(list);
    REQUIRE(done.applied.size() == 1);
    CHECK(done.applied.front() == locked);
    CHECK(done.remaining.empty());
    CHECK_EQ(test::ReadFile(locked), std::string("new"));
    CHECK(!test::Exists(pending));
    CHECK(!test::Exists(list));

    // the list only replaces "x" with an existing "x.mu-pending"
    test::WriteFile(list, toString((game / L"other.txt").wstring()) + "\r\n" + toString((game / L"gone.mu-pending").wstring()) + "\r\n");
    auto ignored = ApplyPendingFiles(list);
    CHECK(ignored.applied.empty());
    CHECK(ignored.remaining.empty());
    CHECK_EQ(test::ReadFile(game / L"other.txt"), std::string("other"));
    CHECK(!test::Exists(list));
}

TEST_CASE(Pending_AppliedWhenTheGameStartsAgain)
{
    // "Restart the game to apply changes": the new game process starts while the old one still has the archive
    // open, the library waits for the old process when the new one loads it and puts the archive in place
    test::TempDir dir(L"restart");
    auto game = dir / L"game";
    std::filesystem::create_directories(game / L"update");
    auto exe = game / L"Game.exe";
    REQUIRE(CopyFileW((test::BinDir() / L"UnitTests.exe").c_str(), exe.c_str(), FALSE));
    auto archive = game / L"update" / L"data.img";
    test::WriteFile(archive, "old");

    std::wstring command = test::Quote(exe.wstring()) + L" --hold " + test::Quote(archive.wstring()) + L" 2500";
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION oldGame = {};
    REQUIRE(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &oldGame));
    auto held = archive;
    held += L".held";
    for (int i = 0; i < 200 && !test::Exists(held); i++)
        Sleep(25);
    REQUIRE(test::Exists(held));

    // what the update left behind
    auto pending = archive;
    pending += kPendingSuffix;
    test::WriteFile(pending, "new");
    REQUIRE(AddPendingFiles(game / kPendingListName, { pending }));

    // started by someone else while the archive is open: it waits for the next launch
    CHECK_EQ(test::RunProcess(test::Quote(exe.wstring()) + L" --startup", 30000), 0);
    CHECK_EQ(test::ReadFile(archive), std::string("old"));
    CHECK(test::Exists(game / kPendingListName));

    // restarted by the updater
    SetEnvironmentVariableW(L"MODUPDATER_WAIT_PID", std::to_wstring(oldGame.dwProcessId).c_str());
    int code = test::RunProcess(test::Quote(exe.wstring()) + L" --startup", 30000);
    SetEnvironmentVariableW(L"MODUPDATER_WAIT_PID", nullptr);
    WaitForSingleObject(oldGame.hProcess, 10000);
    CloseHandle(oldGame.hThread);
    CloseHandle(oldGame.hProcess);

    CHECK_EQ(code, 0);
    CHECK_EQ(test::ReadFile(archive), std::string("new"));
    CHECK(!test::Exists(pending));
    CHECK(!test::Exists(game / kPendingListName));
}

TEST_CASE(Extract_Cancel)
{
    test::TempDir dir(L"cancel");
    auto zip = dir / L"Mod.zip";
    REQUIRE(test::CreateZip(zip, { { "a.bin", test::RandomData(2 * 1024 * 1024) }, { "b.bin", test::RandomData(2 * 1024 * 1024, 2) } }));
    zip::Reader reader;
    REQUIRE(reader.Open(zip));
    ExtractReport report;
    int calls = 0;
    CHECK(!ExtractArchive(reader, dir / L"game", {}, [&](const ExtractProgress&) { return ++calls < 3; }, report));
    CHECK(report.cancelled);
    CHECK(!test::Exists(dir / L"game" / L"b.bin"));
    CHECK(!test::Exists(dir / L"game" / L"a.bin.mu-tmp"));
}

#include <windows.h>
#include <cstdio>
#include <string>
#include "common/test_server.h"
#include "common/test_util.h"

// Simulates a game that loads two plugins using the updater. The updates are served from a
// local server: each archive contains the plugin itself (so the loaded file has to be replaced
// while it is in use) and an ini file (run it twice to see the settings being kept).
int main()
{
    SetConsoleOutputCP(CP_UTF8);
    printf("modupdater test game\n\n");

    test::Server server;
    if (!server.Start())
    {
        printf("Cannot start the local test server\n");
        return 1;
    }

    auto bin = test::BinDir();
    for (std::string name : { "TestDLL1", "TestDLL2" })
    {
        auto plugin = test::ReadFile(bin / (std::wstring(name.begin(), name.end()) + L".asi"));
        test::Resource update;
        update.body = test::CreateZipData({
            { name + ".asi", plugin },
            { name + ".ini", "[MAIN]\r\n; change the value, it is kept by the next update\r\nOption = 1\r\nNewOption = 2\r\n" },
        });
        update.contentDisposition = "attachment; filename=" + name + ".zip";
        update.lastModified = test::HttpDate(0);
        update.bytesPerSecond = 2 * 1024 * 1024;
        server.Set("/" + name + ".zip", update);

        auto url = server.Url("/" + name + ".zip");
        SetEnvironmentVariableA(("MODUPDATER_TEST_URL_" + name).c_str(), url.c_str());
        printf("%s updates from %s\n", name.c_str(), url.c_str());
    }

    auto log = (bin / L"TestApp-modupdater.log").string();
    SetEnvironmentVariableA("MODUPDATER_TEST_LOG", log.c_str());

    LoadLibraryW((bin / L"TestDLL1.asi").c_str());
    LoadLibraryW((bin / L"TestDLL2.asi").c_str());

    printf("\nPlugins loaded, the update dialog appears in about 5 seconds.\n");
    printf("Log: %s\n", log.c_str());
    printf("Press Enter to quit.\n");
    getchar();
    return 0;
}

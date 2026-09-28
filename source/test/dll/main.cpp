#include <windows.h>
#include <string>
#include "libmodupdater.h"

// A plugin that uses the updater. TestApp.exe tells it where its updates are with
// MODUPDATER_TEST_URL_<name>, without it the plugin checks the historic test archive.
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        char value[2048] = {};
        std::string variable = std::string("MODUPDATER_TEST_URL_") + TESTDLL_NAME;
        if (GetEnvironmentVariableA(variable.c_str(), value, sizeof(value)))
        {
            muSetUpdateURL(hModule, value);
            muSetAlwaysUpdate(hModule, true);
        }
        else
        {
            muSetDevUpdateURL(hModule, "https://github.com/user-attachments/files/15524169/TestDLL1.zip");
            muSetAlwaysUpdate(hModule, true);
        }

        if (GetEnvironmentVariableA("MODUPDATER_TEST_LOG", value, sizeof(value)))
            muSetLogFile(hModule, value);

        muInit();
    }
    return TRUE;
}

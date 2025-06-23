#pragma once
#include <WinDef.h>

extern "C"
{
    void muSetUpdateURL(HMODULE hModule, const char* url);
    void muSetDevUpdateURL(HMODULE hModule, const char* url);
    void muSetArchivePassword(HMODULE hModule, const char* password);
    void muSetSkipUpdateCompleteDialog(HMODULE hModule, bool skipcompletedialog);
    void muSetAlwaysUpdate(HMODULE hModule, bool alwaysupdate);
    void muInit();


    void muSetInstallerIcon(HMODULE hModule, HICON icon);
    void muSetInstallerWindowTitle(HMODULE hModule, const char* title);
    void muSetInstallerMainInstruction(HMODULE hModule, const char* maininstr);
    void muSetInstallerContent(HMODULE hModule, const char* content);
    void muSetInstallerFooter(HMODULE hModule, const char* footer);
    void muSetRGLAppID(HMODULE hModule, const char* id, const char* subfolder);
    void muSetSteamAppID(HMODULE hModule, const char* id, const char* subfolder);
    void muInitInstaller();
    bool muAppendZipFile(int argc, char* argv[]);
}

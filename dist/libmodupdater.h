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
}

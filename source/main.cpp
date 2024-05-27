#include "stdafx.h"
extern void InitModupdater();

int main()
{
    InitModupdater();

    return 0;
}

BOOL APIENTRY DllMain(HMODULE /*hModule*/, DWORD reason, LPVOID /*lpReserved*/)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        InitModupdater();
    }
    return TRUE;
}
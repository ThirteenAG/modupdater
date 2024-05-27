#include "Windows.h"
#include "libmodupdater.h"

int main()
{
    // if modupdater asi is present, then static lib usage is not necessary
    //LoadLibrary(L"modupdaterx86_64.asi");
    LoadLibrary(L"TestDLL1.asi");
    LoadLibrary(L"TestDLL2.asi");

    while (true)
    {
        Sleep(0);
    }

    return 0;
}

BOOL APIENTRY DllMain(HMODULE /*hModule*/, DWORD reason, LPVOID /*lpReserved*/)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        HMODULE hm = NULL;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&DllMain, &hm);
        //muSetUpdateURL(hm, "https://github.com/ThirteenAG/modupdater");
        muSetDevUpdateURL(hm, "https://github.com/user-attachments/files/15524169/TestDLL1.zip");
        muSetAlwaysUpdate(hm, true);
        //muSetSkipUpdateCompleteDialog(hm, true);
        muInit();
    }
    return TRUE;
}
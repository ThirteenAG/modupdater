#include "Windows.h"
#include "libmodupdater.h"

int main(int argc, char** argv)
{
#ifdef MUINSTALLER
    HMODULE hm = NULL;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&main, &hm);

    if (muAppendZipFile(argc, argv))
        return 0; // if zip file is appended, exiting

    //HICON icon = LoadIconW(hm, MAKEINTRESOURCEW(101));
    //muSetInstallerIcon(hm, icon);
    muSetInstallerWindowTitle(hm, "Fusion Fix for GTAIV: The Complete Edition");
    muSetInstallerMainInstruction(hm, "Choose where to install Fusion Fix");
    muSetInstallerContent(hm,
        "Fusion Fix is a comprehensive modification for Grand Theft Auto IV: The Complete Edition that aims "
        "to fix a wide range of technical issues, bugs, and limitations in the game that were left unaddressed in "
        "official updates. This project represents a community-driven effort to restore and enhance the Grand Theft "
        "Auto IV experience for modern systems."
        "\n\n"
        "<a href=\"https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix\">GitHub repository</a>\n"
        "<a href=\"https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix/issues\">Report an issue</a>"
    );

    muSetInstallerFooter(hm, "<a href=\"https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix\">https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix</a>");
    muSetRGLAppID(hm, "Grand Theft Auto IV", "");
    muSetSteamAppID(hm, "12210", "GTAIV");

    muSetUpdateURL(hm, "https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix/releases/latest/download/GTAIV.EFLC.FusionFix.zip");
    //muSetDevUpdateURL(hm, "https://github.com/user-attachments/files/15524169/TestDLL1.zip");
    //muSetAlwaysUpdate(hm, true);
    muInitInstaller();
#else

    // if modupdater asi is present, then static lib usage is not necessary
    //LoadLibrary(L"modupdaterx86_64.asi");
    LoadLibrary(L"TestDLL1.asi");
    LoadLibrary(L"TestDLL2.asi");

    while (true)
    {
        Sleep(0);
    }

#endif
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

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    return main(__argc, __argv);
}
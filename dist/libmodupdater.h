#pragma once
#include <WinDef.h>

// modupdater - update checker and installer for game mods
// https://github.com/ThirteenAG/modupdater
//
// All strings are UTF-8. hModule identifies the calling module, e.g.:
//     HMODULE hm = NULL;
//     GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&someFunction, &hm);
// Text passed to the installer (content, footer) may contain links: <a href="https://...">text</a>

#define MODUPDATER_API_VERSION 2

// muSetInstallerUI
#define MU_UI_CLASSIC 0                     // TaskDialog based installer (default)
#define MU_UI_MODERN  1                     // custom high-DPI window with logo, gradient background, colors and fonts

// muSetInstallerIniMode
#define MU_INI_MERGE   0                    // replace all and keep settings (default)
#define MU_INI_REPLACE 1                    // replace all and discard settings
#define MU_INI_SKIP    2                    // don't replace existing ini files

// muSetInstallerTheme (MU_UI_MODERN)
#define MU_THEME_AUTO  0                    // follow the Windows light/dark app mode, also while the installer is open (default)
#define MU_THEME_LIGHT 1
#define MU_THEME_DARK  2

// muRunInstaller results (also usable as the installer's exit code)
#define MU_INSTALL_SUCCESS   0
#define MU_INSTALL_CANCELLED 1
#define MU_INSTALL_FAILED    2
#define MU_INSTALL_RESTARTED 3              // the installer was restarted with administrator rights, exit this instance

// muSetInstallerColor elements (MU_UI_MODERN), colors are COLORREF values: RGB(r, g, b).
// Unset colors are picked to match the brightness of the gradient.
#define MU_COLOR_GRADIENT_TOP        0      // background gradient
#define MU_COLOR_GRADIENT_BOTTOM     1
#define MU_COLOR_TEXT                2      // title and main text
#define MU_COLOR_TEXT_SECONDARY      3      // descriptions, status lines, footer
#define MU_COLOR_LINK                4
#define MU_COLOR_LINK_HOVER          5
#define MU_COLOR_BUTTON              6      // main button (Install, Close...)
#define MU_COLOR_BUTTON_HOVER        7
#define MU_COLOR_BUTTON_PRESSED      8
#define MU_COLOR_BUTTON_TEXT         9
#define MU_COLOR_PROGRESS            10     // progress bar
#define MU_COLOR_PROGRESS_BACKGROUND 11
#define MU_COLOR_PANEL               12     // install location box, checkboxes, secondary buttons
#define MU_COLOR_PANEL_BORDER        13
#define MU_COLOR_ERROR               14     // warnings and the failure icon
#define MU_COLOR_SUCCESS             15     // completion icon
#define MU_COLOR_POPUP               16     // install location list and tooltips
#define MU_COLOR_COUNT               17

// muSetInstallerString ids: replace built-in texts (localization). Placeholders: {title} {path} {file} {exe} {error}
// {exe} is the executable the text is about (found, running) or the first one of muSetInstallerGameExecutable
#define MU_STR_HEADING               0      // modern UI title above the description, default "{title}", "" hides it
#define MU_STR_INSTALL               1      // "Install"
#define MU_STR_BROWSE                2      // "Browse for another folder..." (modern UI: tooltip of the folder button)
#define MU_STR_BROWSE_TITLE          3      // "Select Installation Folder"
#define MU_STR_CHOOSE_FOLDER         4      // "Choose an installation folder..."
#define MU_STR_KEEP_SETTINGS         5      // "Keep my current settings"
#define MU_STR_INI_MERGE             6      // "INI files: replace all and keep settings"
#define MU_STR_INI_REPLACE           7      // "INI files: replace all and discard settings"
#define MU_STR_INI_SKIP              8      // "INI files: don't replace"
#define MU_STR_EXE_MISSING           9      // "{exe} was not found in this folder"
#define MU_STR_EXE_MISSING_CONFIRM   10     // "{exe} was not found in the selected folder:\n{path}\n\nInstall anyway?"
#define MU_STR_ADMIN_REQUIRED        11     // "Administrator permissions required"
#define MU_STR_ADMIN_TEXT            12     // text of the elevation prompt
#define MU_STR_ADMIN_BUTTON          13     // "Restart with administrator privileges"
#define MU_STR_INSTALLING            14     // "Installing {title}..."
#define MU_STR_PREPARING             15     // "Preparing..."
#define MU_STR_DOWNLOADING           16     // "Downloading {file}..."
#define MU_STR_EXTRACTING            17     // "Installing files..."
#define MU_STR_CANCEL                18     // "Cancel"
#define MU_STR_CANCELLING            19     // "Cancelling..."
#define MU_STR_CONFIRM_CANCEL        20     // "Do you want to cancel the installation?"
#define MU_STR_COMPLETE              21     // "Installation Complete"
#define MU_STR_COMPLETE_TEXT         22     // "{title} has been installed to:\n{path}"
#define MU_STR_FAILED                23     // "Installation Failed"
#define MU_STR_CANCELLED             24     // "Installation Cancelled"
#define MU_STR_CLOSE                 25     // "Close"
#define MU_STR_RETRY                 26     // "Try again"
#define MU_STR_LAUNCH                27     // "Launch {exe}"
#define MU_STR_OPEN_FOLDER           28     // "Open installation folder"
#define MU_STR_VIEW_LOG              29     // "View log"
#define MU_STR_GAME_RUNNING          30     // "{exe} is running. Close the game before installing."
#define MU_STR_INSTALL_ANYWAY        31     // "Install anyway"
#define MU_STR_EXE_NOT_FOUND         32     // "{exe} not found", modern UI install location list
#define MU_STR_COUNT                 33

extern "C"
{
    // Updater: checks for updates of the calling module in the background, muInit starts the check
    void muSetUpdateURL(HMODULE hModule, const char* url);
    void muSetDevUpdateURL(HMODULE hModule, const char* url);
    void muSetArchivePassword(HMODULE hModule, const char* password);
    void muSetSkipUpdateCompleteDialog(HMODULE hModule, bool skipcompletedialog);
    void muSetAlwaysUpdate(HMODULE hModule, bool alwaysupdate);
    void muInit();

    // Installer: downloads muSetUpdateURL, or installs the archives appended to the executable
    void muSetInstallerIcon(HMODULE hModule, HICON icon);
    void muSetInstallerWindowTitle(HMODULE hModule, const char* title);
    void muSetInstallerMainInstruction(HMODULE hModule, const char* maininstr);
    void muSetInstallerContent(HMODULE hModule, const char* content);
    void muSetInstallerFooter(HMODULE hModule, const char* footer);
    void muSetRGLAppID(HMODULE hModule, const char* id, const char* subfolder);
    void muSetSteamAppID(HMODULE hModule, const char* id, const char* subfolder);
    void muInitInstaller();
    // "installer.exe archive.zip [output.exe]" creates an offline installer, returns true when it handled the command line
    bool muAppendZipFile(int argc, char* argv[]);

    // Installer: optional settings (API version 2)
    int  muRunInstaller();                                                         // muInitInstaller that returns MU_INSTALL_*
    void muSetInstallerUI(HMODULE hModule, int ui);                                // MU_UI_CLASSIC or MU_UI_MODERN
    void muSetInstallerLogo(HMODULE hModule, const void* image, unsigned int size); // PNG, JPEG, BMP, GIF, ICO or TIFF data
    void muSetInstallerLogoResource(HMODULE hModule, const char* name, const char* type); // image stored in hModule's resources
    void muSetInstallerBackground(HMODULE hModule, const void* image, unsigned int size);   // optional image behind the gradient
    void muSetInstallerBackgroundResource(HMODULE hModule, const char* name, const char* type);
    void muSetInstallerBackgroundOverlay(HMODULE hModule, int opacityPercent);     // gradient opacity over the background image (default 70)
    void muSetInstallerColor(HMODULE hModule, int element, COLORREF color);        // MU_COLOR_*
    void muSetInstallerGradient(HMODULE hModule, COLORREF top, COLORREF bottom);
    void muSetInstallerFont(HMODULE hModule, const char* family);                  // e.g. "Segoe UI"
    void muSetInstallerFontData(HMODULE hModule, const void* font, unsigned int size); // private TTF/OTF, select it with muSetInstallerFont
    void muSetInstallerWindowSize(HMODULE hModule, int width, int height);         // in 96 DPI pixels, 0 = automatic
    void muSetInstallerString(HMODULE hModule, int id, const char* text);          // MU_STR_*
    void muSetInstallerGameExecutable(HMODULE hModule, const char* exeNames);      // one or more names, e.g. "GTAIV.exe;EFLC.exe;PlayGTAIV.exe": a folder with any of them is a game folder,
                                                                                   // any of them running is "the game is running", the first one found is started by the "Launch" button
    void muAddInstallerPath(HMODULE hModule, const char* path);                    // extra suggested install folder, environment variables are expanded
    void muSetInstallerIniMode(HMODULE hModule, int mode, bool userSelectable);    // MU_INI_*, userSelectable shows the choice in the installer
    void muSetLogFile(HMODULE hModule, const char* path);                          // log file for the updater/installer, NULL or "" disables it

    // Installer: light and dark theme (MU_UI_MODERN). Colors and logos set above are used by both themes,
    // these functions set them for MU_THEME_LIGHT or MU_THEME_DARK only, e.g. a dark gradient and a white logo for the dark theme.
    void muSetInstallerTheme(HMODULE hModule, int theme);                          // MU_THEME_AUTO (default), MU_THEME_LIGHT or MU_THEME_DARK
    void muSetInstallerThemeColor(HMODULE hModule, int theme, int element, COLORREF color); // MU_COLOR_*
    void muSetInstallerThemeGradient(HMODULE hModule, int theme, COLORREF top, COLORREF bottom);
    void muSetInstallerThemeLogo(HMODULE hModule, int theme, const void* image, unsigned int size);
    void muSetInstallerThemeLogoResource(HMODULE hModule, int theme, const char* name, const char* type);
}

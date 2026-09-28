#include "stdafx.h"
#include "installer.h"
#include "fileops.h"
#include "log.h"
#include "string_funcs.h"
#include "ui_common.h"
#include <shellapi.h>
#include <shobjidl.h>

// TaskDialog based installer UI (MU_UI_CLASSIC)
namespace mu::installer
{
    namespace
    {
        constexpr int kFirstPathButton = 1001;
        constexpr int kRadioMerge = 1011;
        constexpr int kRadioReplace = 1012;
        constexpr int kRadioSkip = 1013;
        constexpr int kButtonElevate = 1101;
        constexpr int kButtonCancel = 1102;
        constexpr int kButtonLaunch = 1103;
        constexpr int kButtonRetry = 1104;
        constexpr int kButtonClose = 1105;

        std::wstring DisplayPath(const std::filesystem::path& path)
        {
            auto s = path.wstring();
            if (!s.empty() && s.back() != std::filesystem::path::preferred_separator)
                s += std::filesystem::path::preferred_separator;
            return s;
        }

        void ApplyIcon(TASKDIALOGCONFIG& tdc, const Config& config)
        {
            if (config.icon)
            {
                tdc.dwFlags |= TDF_USE_HICON_MAIN;
                tdc.hMainIcon = ui::GetDpiScaledIcon(config.icon);
            }
        }

        struct PathDialogState
        {
            const Config* config;
            std::vector<Location> locations;
            std::filesystem::path selected;
            bool browsed = false;
        };

        HRESULT CALLBACK PathDialogCallback(HWND hwnd, UINT notification, WPARAM wParam, LPARAM lParam, LONG_PTR refData)
        {
            auto state = reinterpret_cast<PathDialogState*>(refData);
            switch (notification)
            {
            case TDN_BUTTON_CLICKED:
            {
                int id = static_cast<int>(wParam);
                int count = static_cast<int>(state->locations.size());
                if (id >= kFirstPathButton && id < kFirstPathButton + count)
                {
                    state->selected = state->locations[id - kFirstPathButton].path;
                    return S_OK;
                }
                if (id == kFirstPathButton + count)
                {
                    // "Browse...": the folder is added to the list and the dialog is shown again
                    auto folder = BrowseForFolder(hwnd, *state->config, state->locations.empty() ? std::filesystem::path() : state->locations.back().path);
                    if (folder.empty())
                        return S_FALSE;

                    std::error_code ec;
                    for (auto& l : state->locations)
                    {
                        if (iequals(l.path.wstring(), folder.wstring()) || std::filesystem::equivalent(l.path, folder, ec))
                            return S_FALSE;
                    }
                    state->locations.push_back({ folder, L"" });
                    state->browsed = true;
                    return S_OK;
                }
                return S_OK;
            }
            case TDN_HYPERLINK_CLICKED:
                ui::OpenUrl(hwnd, reinterpret_cast<LPCWSTR>(lParam));
                break;
            default:
                break;
            }
            return S_OK;
        }

        std::filesystem::path ShowPathSelectionDialog(const Config& config, PathDialogState& state, IniMode& iniMode)
        {
            auto title = WindowTitle(config);
            auto mainInstruction = config.mainInstruction.empty() ? std::wstring(L"Select Installation Path") : config.mainInstruction;
            auto content = config.content.empty() ? std::wstring(L"Choose where to install the mod:") : config.content;
            auto browse = Text(config, MU_STR_BROWSE);
            std::wstring radioTexts[] = { Text(config, MU_STR_INI_MERGE), Text(config, MU_STR_INI_REPLACE), Text(config, MU_STR_INI_SKIP) };

            for (;;)
            {
                state.browsed = false;
                state.selected.clear();

                std::vector<std::wstring> texts;
                texts.reserve(state.locations.size());
                for (auto& location : state.locations)
                {
                    std::wstring note = location.source;
                    auto check = CheckPath(config, location.path);
                    if (!check.hasGameExe)
                        note += (note.empty() ? L"" : L" · ") + Text(config, MU_STR_EXE_MISSING, {}, {}, {}, check.exeName);
                    texts.push_back(DisplayPath(location.path) + (note.empty() ? L"" : L"\n" + note));
                }

                std::vector<TASKDIALOG_BUTTON> buttons;
                for (size_t i = 0; i < texts.size(); i++)
                    buttons.push_back({ kFirstPathButton + static_cast<int>(i), texts[i].c_str() });
                buttons.push_back({ kFirstPathButton + static_cast<int>(texts.size()), browse.c_str() });

                TASKDIALOG_BUTTON radios[] = {
                    { kRadioMerge, radioTexts[0].c_str() },
                    { kRadioReplace, radioTexts[1].c_str() },
                    { kRadioSkip, radioTexts[2].c_str() },
                };

                TASKDIALOGCONFIG tdc = { sizeof(TASKDIALOGCONFIG) };
                tdc.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_USE_COMMAND_LINKS | TDF_CAN_BE_MINIMIZED | TDF_SIZE_TO_CONTENT | TDF_ENABLE_HYPERLINKS;
                tdc.pszWindowTitle = title.c_str();
                tdc.pszMainInstruction = mainInstruction.c_str();
                tdc.pszContent = content.c_str();
                tdc.pszFooter = config.footer.c_str();
                tdc.pButtons = buttons.data();
                tdc.cButtons = static_cast<UINT>(buttons.size());
                if (config.iniSelectable)
                {
                    tdc.pRadioButtons = radios;
                    tdc.cRadioButtons = _countof(radios);
                    tdc.nDefaultRadioButton = kRadioMerge + static_cast<int>(iniMode);
                }
                tdc.pfCallback = PathDialogCallback;
                tdc.lpCallbackData = reinterpret_cast<LONG_PTR>(&state);
                ApplyIcon(tdc, config);

                int clicked = 0, radio = 0;
                if (FAILED(TaskDialogIndirect(&tdc, &clicked, &radio, nullptr)))
                    return {};
                if (state.browsed)
                    continue;
                if (config.iniSelectable && radio >= kRadioMerge && radio <= kRadioSkip)
                    iniMode = static_cast<IniMode>(radio - kRadioMerge);
                return state.selected;
            }
        }

        bool ShowElevationDialog(const Config& config, const std::filesystem::path& path)
        {
            auto title = WindowTitle(config);
            auto mainInstruction = Text(config, MU_STR_ADMIN_REQUIRED);
            auto content = Text(config, MU_STR_ADMIN_TEXT, DisplayPath(path));
            auto elevate = Text(config, MU_STR_ADMIN_BUTTON) + L"\nIf you grant permission via User Account Control, the installer\nwill be able to write to the selected folder.";
            auto cancel = Text(config, MU_STR_CANCEL);

            TASKDIALOG_BUTTON buttons[] = {
                { kButtonElevate, elevate.c_str() },
                { kButtonCancel, cancel.c_str() }
            };

            TASKDIALOGCONFIG tdc = { sizeof(TASKDIALOGCONFIG) };
            tdc.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION;
            tdc.pszWindowTitle = title.c_str();
            tdc.pszMainInstruction = mainInstruction.c_str();
            tdc.pszContent = content.c_str();
            tdc.pButtons = buttons;
            tdc.cButtons = _countof(buttons);
            tdc.pfCallback = [](HWND hwnd, UINT notification, WPARAM, LPARAM, LONG_PTR) -> HRESULT
            {
                if (notification == TDN_CREATED)
                    SendMessage(hwnd, TDM_SET_BUTTON_ELEVATION_REQUIRED_STATE, kButtonElevate, TRUE);
                return S_OK;
            };
            ApplyIcon(tdc, config);

            int clicked = 0;
            return SUCCEEDED(TaskDialogIndirect(&tdc, &clicked, nullptr, nullptr)) && clicked == kButtonElevate;
        }

        void ShowMessage(const Config& config, const std::wstring& mainInstruction, const std::wstring& content, PCWSTR icon)
        {
            auto title = WindowTitle(config);
            TASKDIALOGCONFIG tdc = { sizeof(TASKDIALOGCONFIG) };
            tdc.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION;
            tdc.dwCommonButtons = TDCBF_CLOSE_BUTTON;
            tdc.pszWindowTitle = title.c_str();
            tdc.pszMainInstruction = mainInstruction.c_str();
            tdc.pszContent = content.c_str();
            tdc.pszMainIcon = icon;
            TaskDialogIndirect(&tdc, nullptr, nullptr, nullptr);
        }

        struct ProgressDialogState
        {
            const Config* config;
            Job* job;
            std::wstring text;
            ITaskbarList3* taskbar = nullptr;
        };

        std::wstring PadContent(std::wstring text)
        {
            // always two lines so the dialog does not change its height
            if (text.find(L'\n') == std::wstring::npos)
                text += L"\n ";
            return text;
        }

        HRESULT CALLBACK ProgressDialogCallback(HWND hwnd, UINT notification, WPARAM wParam, LPARAM lParam, LONG_PTR refData)
        {
            auto state = reinterpret_cast<ProgressDialogState*>(refData);
            switch (notification)
            {
            case TDN_CREATED:
                SendMessage(hwnd, TDM_SET_PROGRESS_BAR_RANGE, 0, MAKELPARAM(0, 1000));
                SendMessage(hwnd, TDM_SET_PROGRESS_BAR_POS, 0, 0);
                if (SUCCEEDED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&state->taskbar))))
                    state->taskbar->HrInit();
                break;
            case TDN_TIMER:
            {
                // switching the bar to marquee mode at runtime breaks the layout of the footer, stay at 0 instead
                auto progress = state->job->GetProgress();
                bool indeterminate = progress.total == 0 || progress.phase == Phase::Preparing;
                int permille = progress.total ? static_cast<int>(std::min<uint64_t>(1000, progress.done * 1000 / progress.total)) : 0;
                SendMessage(hwnd, TDM_SET_PROGRESS_BAR_POS, permille, 0);

                if (state->taskbar)
                {
                    state->taskbar->SetProgressState(hwnd, indeterminate ? TBPF_INDETERMINATE : TBPF_NORMAL);
                    if (!indeterminate)
                        state->taskbar->SetProgressValue(hwnd, permille, 1000);
                }

                if (!state->job->IsCancelling())
                {
                    auto line = ProgressLine(progress);
                    auto text = PadContent(state->job->StatusText() + (line.empty() ? L"" : L"  " + line) + L"\n" + (progress.phase == Phase::Extracting ? progress.file : L""));
                    if (text != state->text)
                    {
                        state->text = text;
                        SendMessage(hwnd, TDM_UPDATE_ELEMENT_TEXT, TDE_CONTENT, reinterpret_cast<LPARAM>(state->text.c_str()));
                    }
                }

                if (state->job->IsFinished())
                    SendMessage(hwnd, TDM_CLICK_BUTTON, IDOK, 0);
                break;
            }
            case TDN_BUTTON_CLICKED:
                if (wParam == IDOK || state->job->IsFinished())
                    return S_OK;
                // Cancel, Esc or the close button: stop the job and wait for it
                state->job->Cancel();
                state->text = PadContent(Text(*state->config, MU_STR_CANCELLING));
                SendMessage(hwnd, TDM_UPDATE_ELEMENT_TEXT, TDE_CONTENT, reinterpret_cast<LPARAM>(state->text.c_str()));
                SendMessage(hwnd, TDM_ENABLE_BUTTON, kButtonCancel, FALSE);
                return S_FALSE;
            case TDN_HYPERLINK_CLICKED:
                ui::OpenUrl(hwnd, reinterpret_cast<LPCWSTR>(lParam));
                break;
            case TDN_DESTROYED:
                if (state->taskbar)
                {
                    state->taskbar->Release();
                    state->taskbar = nullptr;
                }
                break;
            default:
                break;
            }
            return S_OK;
        }

        void ShowProgressDialog(const Config& config, Job& job)
        {
            auto title = WindowTitle(config);
            auto mainInstruction = Text(config, MU_STR_INSTALLING);
            auto cancel = Text(config, MU_STR_CANCEL);

            ProgressDialogState state{ &config, &job };
            state.text = PadContent(Text(config, MU_STR_PREPARING));

            TASKDIALOG_BUTTON buttons[] = { { kButtonCancel, cancel.c_str() } };

            // a fixed width: with TDF_SIZE_TO_CONTENT the footer below the progress bar gets cut off
            TASKDIALOGCONFIG tdc = { sizeof(TASKDIALOGCONFIG) };
            tdc.dwFlags = TDF_ENABLE_HYPERLINKS | TDF_SHOW_PROGRESS_BAR | TDF_CALLBACK_TIMER | TDF_ALLOW_DIALOG_CANCELLATION | TDF_CAN_BE_MINIMIZED;
            tdc.cxWidth = 300;
            tdc.pButtons = buttons;
            tdc.cButtons = _countof(buttons);
            tdc.pszWindowTitle = title.c_str();
            tdc.pszMainInstruction = mainInstruction.c_str();
            tdc.pszContent = state.text.c_str();
            tdc.pszFooter = config.footer.c_str();
            tdc.pfCallback = ProgressDialogCallback;
            tdc.lpCallbackData = reinterpret_cast<LONG_PTR>(&state);
            ApplyIcon(tdc, config);

            if (FAILED(TaskDialogIndirect(&tdc, nullptr, nullptr, nullptr)))
                job.Cancel();
        }

        struct ResultDialogState
        {
            const Config* config;
            std::filesystem::path target;
        };

        HRESULT CALLBACK ResultDialogCallback(HWND hwnd, UINT notification, WPARAM, LPARAM lParam, LONG_PTR refData)
        {
            auto state = reinterpret_cast<ResultDialogState*>(refData);
            if (notification == TDN_HYPERLINK_CLICKED)
            {
                std::wstring link = reinterpret_cast<LPCWSTR>(lParam);
                if (link == L"open:")
                    ui::OpenInExplorer(state->target.wstring());
                else if (link == L"log:")
                    ShellExecuteW(hwnd, L"open", LogGetFile().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                else
                    ui::OpenUrl(hwnd, link);
            }
            return S_OK;
        }

        // Returns the id of the clicked button
        int ShowResultDialog(const Config& config, const Outcome& outcome, const std::filesystem::path& target)
        {
            auto title = WindowTitle(config);
            bool success = outcome.result == Result::Success;
            auto mainInstruction = Text(config, success ? MU_STR_COMPLETE : MU_STR_FAILED);
            auto content = DescribeOutcome(config, outcome, DisplayPath(target));

            std::wstring links;
            if (success)
                links += L"<a href=\"open:\">" + Text(config, MU_STR_OPEN_FOLDER) + L"</a>";
            if (!LogGetFile().empty() && !success)
                links += (links.empty() ? L"" : L"   ") + std::wstring(L"<a href=\"log:\">") + Text(config, MU_STR_VIEW_LOG) + L"</a>";
            if (!links.empty())
                content += L"\n\n" + links;

            auto launch = LaunchText(config, target);
            auto retry = Text(config, MU_STR_RETRY);
            auto close = Text(config, MU_STR_CLOSE);

            std::vector<TASKDIALOG_BUTTON> buttons;
            if (success && !launch.empty())
                buttons.push_back({ kButtonLaunch, launch.c_str() });
            if (!success)
                buttons.push_back({ kButtonRetry, retry.c_str() });
            buttons.push_back({ kButtonClose, close.c_str() });

            ResultDialogState state{ &config, target };

            TASKDIALOGCONFIG tdc = { sizeof(TASKDIALOGCONFIG) };
            tdc.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_ENABLE_HYPERLINKS | TDF_SIZE_TO_CONTENT | TDF_CAN_BE_MINIMIZED;
            tdc.pszWindowTitle = title.c_str();
            tdc.pszMainInstruction = mainInstruction.c_str();
            tdc.pszContent = content.c_str();
            tdc.pButtons = buttons.data();
            tdc.cButtons = static_cast<UINT>(buttons.size());
            tdc.nDefaultButton = buttons.front().nButtonID;
            tdc.pfCallback = ResultDialogCallback;
            tdc.lpCallbackData = reinterpret_cast<LONG_PTR>(&state);
            if (success)
            {
                tdc.pszMainIcon = TD_INFORMATION_ICON;
                ApplyIcon(tdc, config);
            }
            else
            {
                tdc.pszMainIcon = TD_ERROR_ICON;
            }

            int clicked = kButtonClose;
            if (FAILED(TaskDialogIndirect(&tdc, &clicked, nullptr, nullptr)))
                return kButtonClose;
            return clicked;
        }
    }

    int RunClassic(Config& config, const CommandLine& commandLine)
    {
        ui::ScopedDpiAwareness dpiAwareness;

        IniMode iniMode = config.iniMode;
        std::filesystem::path target = commandLine.installDir;
        const bool fromCommandLine = !target.empty();
        PathDialogState state{ &config, DetectLocations(config) };

        for (;;)
        {
            if (target.empty())
            {
                target = ShowPathSelectionDialog(config, state, iniMode);
                if (target.empty())
                    return MU_INSTALL_CANCELLED;
            }

            auto check = CheckPath(config, target);
            if (!check.hasGameExe && !commandLine.autoStart && !ConfirmMissingExecutable(nullptr, config, target, check.exeName))
            {
                if (fromCommandLine)
                    return MU_INSTALL_CANCELLED;
                target.clear();
                continue;
            }

            if (!check.writable)
            {
                if (ShowElevationDialog(config, target))
                {
                    if (RestartElevated(target, iniMode))
                        return MU_INSTALL_RESTARTED;
                    ShowMessage(config, Text(config, MU_STR_FAILED), L"Administrator permissions were not granted.", TD_ERROR_ICON);
                }
                if (fromCommandLine)
                    return MU_INSTALL_CANCELLED;
                target.clear();
                continue;
            }

            if (!ConfirmGameClosed(nullptr, config, target))
            {
                if (fromCommandLine)
                    return MU_INSTALL_CANCELLED;
                target.clear();
                continue;
            }
            break;
        }

        for (;;)
        {
            Job job(config, target, iniMode);
            job.Start();
            ShowProgressDialog(config, job);
            while (!job.IsFinished())
                Sleep(20);

            auto outcome = job.GetOutcome();
            if (outcome.result == Result::Cancelled)
                return MU_INSTALL_CANCELLED;

            int clicked = ShowResultDialog(config, outcome, target);
            if (outcome.result == Result::Success)
            {
                if (clicked == kButtonLaunch)
                    LaunchGame(config, target);
                return MU_INSTALL_SUCCESS;
            }
            if (clicked != kButtonRetry)
                return MU_INSTALL_FAILED;
        }
    }
}

#include "CodexUsage.cpp"
#include <cstdio>
#include <thread>
#include <atomic>

HWND OwnOptionsWindow()
{
    HWND window = nullptr;
    EnumWindows([](HWND candidate, LPARAM context) -> BOOL {
        DWORD process = 0;
        GetWindowThreadProcessId(candidate, &process);
        wchar_t name[128]{};
        GetClassNameW(candidate, name, 128);
        if (process == GetCurrentProcessId() && wcscmp(name, L"CodexUsageOptionsWnd") == 0)
        {
            *reinterpret_cast<HWND*>(context) = candidate;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&window));
    return window;
}

int wmain()
{
    ITMPlugin* p = TMPluginGetInstance();
    std::atomic<bool> layout_ok{ false };
    std::atomic<bool> time_settings_first{ false };
    const std::wstring label_before = CCodexUsagePlugin::Instance().DisplayLabel(true);
    const std::wstring item_label_before = p->GetItem(0)->GetItemLableText();
    POINT cursor{}; GetCursorPos(&cursor);
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &monitor);
    HWND edge_parent = CreateWindowExW(0, L"STATIC", L"Synthetic taskbar parent", WS_POPUP,
        monitor.rcWork.left, monitor.rcWork.bottom - 30, 240, 30,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);

    std::thread clicker([&] {
        Sleep(1500);
        HWND h = OwnOptionsWindow();
        if (h == nullptr)
        {
            wprintf(L"dialog not found\n");
            return;
        }
        wprintf(L"dialog found\n");
        HWND ok = GetDlgItem(h, IDOK);
        RECT cr{}, wr{};
        GetClientRect(h, &cr);
        MapWindowPoints(h, nullptr, reinterpret_cast<POINT*>(&cr), 2);
        if (ok != nullptr)
            GetWindowRect(ok, &wr);
        RECT dialog{}; GetWindowRect(h, &dialog);
        RECT reset{}, size{}, graph{};
        GetWindowRect(GetDlgItem(h, IDC_SHOW_RESET_TIME), &reset);
        GetWindowRect(GetDlgItem(h, IDC_TIME_FONT_SIZE), &size);
        GetWindowRect(GetDlgItem(h, IDC_GRAPH_ENABLE), &graph);
        time_settings_first = reset.bottom <= size.top && size.bottom < graph.top;
        int center_x = (monitor.rcWork.left + monitor.rcWork.right - (dialog.right - dialog.left)) / 2;
        int center_y = (monitor.rcWork.top + monitor.rcWork.bottom - (dialog.bottom - dialog.top)) / 2;
        layout_ok = std::abs(dialog.left - center_x) <= 2 && std::abs(dialog.top - center_y) <= 2 &&
            dialog.left >= monitor.rcWork.left && dialog.right <= monitor.rcWork.right &&
            dialog.top >= monitor.rcWork.top && dialog.bottom <= monitor.rcWork.bottom &&
            cr.bottom >= wr.bottom;
        wprintf(L"client=(%ld,%ld)-(%ld,%ld) okbtn=(%ld,%ld)-(%ld,%ld) visible=%d\n",
            cr.left, cr.top, cr.right, cr.bottom, wr.left, wr.top, wr.right, wr.bottom,
            ok != nullptr && IsWindowVisible(ok));
        SendMessageW(GetDlgItem(h, IDC_GRAPH_ENABLE), BM_CLICK, 0, 0);
        SendMessageW(GetDlgItem(h, IDC_GRAPH_USED), BM_CLICK, 0, 0);
        SendMessageW(GetDlgItem(h, IDC_SHOW_RESET_TIME), BM_CLICK, 0, 0);
        SendMessageW(GetDlgItem(h, IDC_TIME_FONT_SPIN), UDM_SETPOS32, 0, 9);
        PostMessageW(h, WM_COMMAND, IDOK, 0);
    });

    ITMPlugin::OptionReturn r = p->ShowOptionsDialog(edge_parent);
    clicker.join();
    DestroyWindow(edge_parent);

    CCodexUsagePlugin& plugin = CCodexUsagePlugin::Instance();
    wprintf(L"result=%d graph_enabled=%d graph_remaining=%d show_reset_time=%d\n",
        (int)r, (int)plugin.GraphEnabled(), (int)plugin.GraphRemaining(), (int)plugin.ShowResetTime());
    wprintf(L"centered_and_visible=%d\n", (int)layout_ok.load());
    wprintf(L"time_font_size=%d\n", plugin.TimeFontSize());
    bool labels_unchanged = plugin.DisplayLabel(true) == label_before && p->GetItem(0)->GetItemLableText() == item_label_before;
    wprintf(L"time_settings_first=%d labels_unchanged=%d\n", (int)time_settings_first.load(), (int)labels_unchanged);
    if (r != ITMPlugin::OR_OPTION_CHANGED || !plugin.GraphEnabled() || plugin.GraphRemaining() || !plugin.ShowResetTime() || plugin.TimeFontSize() != 9 || !layout_ok || !time_settings_first || !labels_unchanged) return 1;

    // 再次打开并点取消，设置应保持不变
    std::thread canceler([] {
        Sleep(1200);
        HWND h = OwnOptionsWindow();
        if (h != nullptr)
        {
            SendMessageW(GetDlgItem(h, IDC_SHOW_RESET_TIME), BM_CLICK, 0, 0);
            SetDlgItemInt(h, IDC_TIME_FONT_SIZE, 12, FALSE);
            PostMessageW(h, WM_COMMAND, IDCANCEL, 0);
        }
    });
    ITMPlugin::OptionReturn r2 = p->ShowOptionsDialog(nullptr);
    canceler.join();
    wprintf(L"cancel result=%d graph_enabled=%d graph_remaining=%d\n",
        (int)r2, (int)plugin.GraphEnabled(), (int)plugin.GraphRemaining());
    if (r2 != ITMPlugin::OR_OPTION_UNCHANGED || !plugin.ShowResetTime() || plugin.TimeFontSize() != 9) return 1;
    for (const wchar_t* input : {L"99", L"1", L""}) {
        std::thread saver([input] {
            Sleep(300);
            HWND h = OwnOptionsWindow();
            if (h) {
                SetDlgItemTextW(h, IDC_TIME_FONT_SIZE, input);
                PostMessageW(h, WM_COMMAND, IDOK, 0);
            }
        });
        const auto result = p->ShowOptionsDialog(nullptr);
        saver.join();
        const int expected = wcscmp(input, L"99") == 0 ? 99 : 1;
        if (result != ITMPlugin::OR_OPTION_CHANGED || plugin.TimeFontSize() != expected) return 1;
    }
    wprintf(L"free_font_size_without_popup=1\n");
    return 0;
}

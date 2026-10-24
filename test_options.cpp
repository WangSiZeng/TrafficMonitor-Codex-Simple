#include "CodexUsage.cpp"
#include <cstdio>
#include <thread>
#include <atomic>
#include <fstream>
#include <afxribbonres.h>
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

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

HWND OwnColorDialog() {
    HWND result = nullptr;
    EnumWindows([](HWND window, LPARAM context) -> BOOL {
        DWORD process = 0; GetWindowThreadProcessId(window, &process);
        wchar_t title[64]{}, name[64]{};
        GetWindowTextW(window, title, 64); GetClassNameW(window, name, 64);
        if (process == GetCurrentProcessId() && wcscmp(name, L"#32770") == 0 && wcscmp(title, L"颜色") == 0) {
            *reinterpret_cast<HWND*>(context) = window;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    return result;
}

bool SaveOwnWindow(HWND window, const wchar_t* path) {
    DWORD process = 0; GetWindowThreadProcessId(window, &process);
    if (process != GetCurrentProcessId()) return false;
    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_FRAME | RDW_UPDATENOW);
    Sleep(250); // Let themed radio-button animations finish before capture.
    RECT rect{}; GetWindowRect(window, &rect);
    const int width = rect.right - rect.left, height = rect.bottom - rect.top;
    HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    void* pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    HGDIOBJ old = SelectObject(dc, bitmap);
    const bool painted = PrintWindow(window, dc, PW_RENDERFULLCONTENT) != FALSE;
    GdiFlush();
    BITMAPFILEHEADER header{};
    header.bfType = 0x4d42; header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
    header.bfSize = header.bfOffBits + width * height * 4;
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    file.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(BITMAPINFOHEADER));
    file.write(static_cast<const char*>(pixels), width * height * 4);
    const bool success = painted && file.good();
    SelectObject(dc, old); DeleteObject(bitmap); DeleteDC(dc);
    return success;
}

HWND OwnChild(HWND parent, int id, const wchar_t* class_name = nullptr) {
    struct Query { int id; const wchar_t* name; HWND result; } query{id, class_name, nullptr};
    EnumChildWindows(parent, [](HWND child, LPARAM context) -> BOOL {
        auto& item = *reinterpret_cast<Query*>(context);
        wchar_t name[64]{}; GetClassNameW(child, name, 64);
        if (item.name ? wcscmp(name, item.name) == 0 : GetDlgCtrlID(child) == item.id) {
            item.result = child;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&query));
    return query.result;
}

bool ChooseTestColor(HWND options, COLORREF color, bool capture) {
    std::atomic<bool> selected{false};
    std::thread responder([&] {
        const ULONGLONG deadline = GetTickCount64() + 6000;
        while (GetTickCount64() < deadline) {
            HWND dialog = OwnColorDialog();
            if (dialog) {
                Sleep(150);
                if (capture) SaveOwnWindow(dialog, L"build\\graph-color-dialog-preview.bmp");
                HWND tab = OwnChild(dialog, 0, WC_TABCONTROLW);
                if (tab) SendMessageW(GetParent(tab), PSM_SETCURSEL, 1, 0);
                HWND red = OwnChild(dialog, IDC_AFXBARRES_R), green = OwnChild(dialog, IDC_AFXBARRES_G), blue = OwnChild(dialog, IDC_AFXBARRES_B);
                if (red && green && blue) {
                    SetWindowTextW(red, std::to_wstring(GetRValue(color)).c_str());
                    SetWindowTextW(green, std::to_wstring(GetGValue(color)).c_str());
                    SetWindowTextW(blue, std::to_wstring(GetBValue(color)).c_str());
                    selected = true;
                }
                PostMessageW(dialog, WM_COMMAND, selected ? IDOK : IDCANCEL, 0);
                return;
            }
            Sleep(50);
        }
    });
    SendMessageW(GetDlgItem(options, IDC_GRAPH_COLOR), BM_CLICK, 0, 0);
    responder.join();
    return selected;
}

int wmain()
{
    ITMPlugin* p = TMPluginGetInstance();
    wchar_t working[32768]{}; GetCurrentDirectoryW(32768, working);
    const std::wstring settings_directory = JoinPath(working, L"build\\options-fixture");
    CreateDirectoryW(settings_directory.c_str(), nullptr);
    const std::wstring settings_path = JoinPath(settings_directory, L"CodexUsage.ini");
    { std::ofstream initial(settings_path, std::ios::binary); initial << "[config]\r\n"; }
    WritePrivateProfileStringW(L"config", L"codex_path", JoinPath(settings_directory, L"missing-codex-test.exe").c_str(), settings_path.c_str());
    p->OnExtenedInfo(ITMPlugin::EI_CONFIG_DIR, settings_directory.c_str());
    std::atomic<bool> layout_ok{ false };
    std::atomic<bool> time_settings_first{ false };
    std::atomic<bool> time_scope_controls_ok{ false };
    std::atomic<bool> graph_controls_ok{ false }, color_dialog_ok{ false };
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
        RECT reset{}, size{}, graph{}, all{}, five{}, seven{};
        GetWindowRect(GetDlgItem(h, IDC_SHOW_RESET_TIME), &reset);
        GetWindowRect(GetDlgItem(h, IDC_TIME_FONT_SIZE), &size);
        GetWindowRect(GetDlgItem(h, IDC_GRAPH_ENABLE), &graph);
        GetWindowRect(GetDlgItem(h, IDC_TIME_ALL), &all);
        GetWindowRect(GetDlgItem(h, IDC_TIME_5H), &five);
        GetWindowRect(GetDlgItem(h, IDC_TIME_7D), &seven);
        time_settings_first = reset.bottom <= all.top && all.bottom <= size.top && size.bottom < graph.top;
        time_scope_controls_ok = all.top == five.top && five.top == seven.top && all.right <= five.left &&
            five.right <= seven.left && SendDlgItemMessageW(h, IDC_TIME_ALL, BM_GETCHECK, 0, 0) == BST_CHECKED &&
            !IsWindowEnabled(GetDlgItem(h, IDC_TIME_ALL)) && !IsWindowEnabled(GetDlgItem(h, IDC_TIME_5H)) &&
            !IsWindowEnabled(GetDlgItem(h, IDC_TIME_7D));
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
        wchar_t logic[32]{}; GetDlgItemTextW(h, IDC_GRAPH_LOGIC_LABEL, logic, 32);
        graph_controls_ok = wcscmp(logic, L"数据逻辑：") == 0 &&
            IsWindowEnabled(GetDlgItem(h, IDC_GRAPH_COLOR)) && IsWindowEnabled(GetDlgItem(h, IDC_GRAPH_SCROLL));
        color_dialog_ok = ChooseTestColor(h, RGB(20, 80, 140), true);
        SendMessageW(GetDlgItem(h, IDC_GRAPH_THEME), BM_CLICK, 0, 0);
        graph_controls_ok = graph_controls_ok && !IsWindowEnabled(GetDlgItem(h, IDC_GRAPH_COLOR));
        SendMessageW(GetDlgItem(h, IDC_GRAPH_SCROLL), BM_CLICK, 0, 0);
        SendMessageW(GetDlgItem(h, IDC_GRAPH_USED), BM_CLICK, 0, 0);
        SendMessageW(GetDlgItem(h, IDC_SHOW_RESET_TIME), BM_CLICK, 0, 0);
        time_scope_controls_ok = time_scope_controls_ok && IsWindowEnabled(GetDlgItem(h, IDC_TIME_ALL)) &&
            IsWindowEnabled(GetDlgItem(h, IDC_TIME_5H)) && IsWindowEnabled(GetDlgItem(h, IDC_TIME_7D));
        SendMessageW(GetDlgItem(h, IDC_TIME_5H), BM_CLICK, 0, 0);
        SendMessageW(GetDlgItem(h, IDC_TIME_FONT_SPIN), UDM_SETPOS32, 0, 9);
        SaveOwnWindow(h, L"build\\graph-options-preview.bmp");
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
    wprintf(L"graph_controls_ok=%d mfc_color_dialog_ok=%d\n", (int)graph_controls_ok.load(), (int)color_dialog_ok.load());
    wprintf(L"time_scope_controls_side_by_side=%d\n", (int)time_scope_controls_ok.load());
    if (r != ITMPlugin::OR_OPTION_CHANGED || !plugin.GraphEnabled() || plugin.GraphRemaining() || !plugin.ShowResetTime(true) || plugin.ShowResetTime(false) || plugin.TimeFontSize() != 9 || !layout_ok || !time_settings_first || !time_scope_controls_ok || !labels_unchanged || !graph_controls_ok || !color_dialog_ok || !plugin.GraphScrolling() || !plugin.GraphFollowsTheme() || plugin.CustomGraphColor() != RGB(20, 80, 140)) return 1;
    if (GetPrivateProfileIntW(L"config", L"graph_color", 0, settings_path.c_str()) != RGB(20, 80, 140) ||
        GetPrivateProfileIntW(L"config", L"graph_follow_theme", 0, settings_path.c_str()) != 1) return 1;
    wchar_t saved_type[32]{};
    GetPrivateProfileStringW(L"config", L"graph_type", L"", saved_type, 32, settings_path.c_str());
    if (wcscmp(saved_type, L"scroll") != 0) return 1;

    // 再次打开并点取消，设置应保持不变
    std::thread canceler([] {
        Sleep(1200);
        HWND h = OwnOptionsWindow();
        if (h != nullptr)
        {
            SendMessageW(GetDlgItem(h, IDC_TIME_7D), BM_CLICK, 0, 0);
            SendMessageW(GetDlgItem(h, IDC_SHOW_RESET_TIME), BM_CLICK, 0, 0);
            SetDlgItemInt(h, IDC_TIME_FONT_SIZE, 12, FALSE);
            SendMessageW(GetDlgItem(h, IDC_GRAPH_THEME), BM_CLICK, 0, 0);
            ChooseTestColor(h, RGB(140, 70, 20), false);
            SendMessageW(GetDlgItem(h, IDC_GRAPH_BAR), BM_CLICK, 0, 0);
            PostMessageW(h, WM_COMMAND, IDCANCEL, 0);
        }
    });
    ITMPlugin::OptionReturn r2 = p->ShowOptionsDialog(nullptr);
    canceler.join();
    wprintf(L"cancel result=%d graph_enabled=%d graph_remaining=%d\n",
        (int)r2, (int)plugin.GraphEnabled(), (int)plugin.GraphRemaining());
    if (r2 != ITMPlugin::OR_OPTION_UNCHANGED || !plugin.ShowResetTime(true) || plugin.ShowResetTime(false) || plugin.TimeFontSize() != 9 ||
        !plugin.GraphScrolling() || !plugin.GraphFollowsTheme() || plugin.CustomGraphColor() != RGB(20, 80, 140) ||
        GetPrivateProfileIntW(L"config", L"graph_color", 0, settings_path.c_str()) != RGB(20, 80, 140)) return 1;
    wchar_t saved_scope[16]{};
    GetPrivateProfileStringW(L"config", L"reset_time_scope", L"", saved_scope, 16, settings_path.c_str());
    if (wcscmp(saved_scope, L"5h") != 0) return 1;
    struct ScopeChoice { int control; const wchar_t* value; bool five, seven; };
    for (const auto choice : {ScopeChoice{IDC_TIME_ALL, L"all", true, true},
        ScopeChoice{IDC_TIME_5H, L"5h", true, false}, ScopeChoice{IDC_TIME_7D, L"7d", false, true}}) {
        std::atomic<bool> exclusive{false};
        std::thread chooser([&] {
            Sleep(300);
            HWND h = OwnOptionsWindow();
            if (h) {
                SendMessageW(GetDlgItem(h, choice.control), BM_CLICK, 0, 0);
                int checked = 0;
                for (int id : {IDC_TIME_ALL, IDC_TIME_5H, IDC_TIME_7D})
                    if (SendDlgItemMessageW(h, id, BM_GETCHECK, 0, 0) == BST_CHECKED) ++checked;
                exclusive = checked == 1 && SendDlgItemMessageW(h, choice.control, BM_GETCHECK, 0, 0) == BST_CHECKED;
                PostMessageW(h, WM_COMMAND, IDOK, 0);
            }
        });
        const auto result = p->ShowOptionsDialog(nullptr);
        chooser.join();
        GetPrivateProfileStringW(L"config", L"reset_time_scope", L"", saved_scope, 16, settings_path.c_str());
        if (result != ITMPlugin::OR_OPTION_CHANGED || !exclusive || wcscmp(saved_scope, choice.value) != 0 ||
            plugin.ShowResetTime(true) != choice.five || plugin.ShowResetTime(false) != choice.seven ||
            !plugin.GraphScrolling() || plugin.GraphRemaining()) return 1;
    }
    wprintf(L"time_scope_all_5h_7d_save_and_cancel=1\n");
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
    for (int control : {IDC_SHOW_RESET_TIME, IDC_GRAPH_ENABLE}) {
        std::thread toggler([control] {
            Sleep(300);
            HWND h = OwnOptionsWindow();
            if (h) {
                SendMessageW(GetDlgItem(h, control), BM_CLICK, 0, 0);
                PostMessageW(h, WM_COMMAND, IDOK, 0);
            }
        });
        const auto result = p->ShowOptionsDialog(nullptr);
        toggler.join();
        const bool graph_only = control == IDC_SHOW_RESET_TIME;
        if (result != ITMPlugin::OR_OPTION_CHANGED || plugin.ShowResetTime() ||
            plugin.GraphEnabled() != graph_only || p->GetItem(0)->IsCustomDraw() != graph_only ||
            p->GetItem(0)->IsDrawResourceUsageGraph() != 0) return 1;
    }
    wprintf(L"graph_and_time_switches_independent=1\n");
    std::atomic<bool> selection_retained{false};
    std::thread reenable([&] {
        Sleep(300);
        HWND h = OwnOptionsWindow();
        if (h) {
            selection_retained = !IsWindowEnabled(GetDlgItem(h, IDC_TIME_7D)) &&
                SendDlgItemMessageW(h, IDC_TIME_7D, BM_GETCHECK, 0, 0) == BST_CHECKED;
            SendMessageW(GetDlgItem(h, IDC_SHOW_RESET_TIME), BM_CLICK, 0, 0);
            selection_retained = selection_retained && IsWindowEnabled(GetDlgItem(h, IDC_TIME_7D));
            SaveOwnWindow(h, L"build\\time-scope-options-preview.bmp");
            PostMessageW(h, WM_COMMAND, IDOK, 0);
        }
    });
    const auto reenabled = p->ShowOptionsDialog(nullptr);
    reenable.join();
    if (reenabled != ITMPlugin::OR_OPTION_CHANGED || !selection_retained || plugin.ShowResetTime(true) ||
        !plugin.ShowResetTime(false) || p->GetItem(0)->IsCustomDraw() || !p->GetItem(1)->IsCustomDraw()) return 1;
    wprintf(L"time_scope_retained_when_disabled_and_per_item_custom_draw=1\n");
    DeleteFileW(settings_path.c_str()); RemoveDirectoryW(settings_directory.c_str());
    return 0;
}

#include <windows.h>
#include <commctrl.h>
#include <atomic>
#include <cstdio>
#include <thread>
#pragma warning(push)
#pragma warning(disable: 4100)
#include "PluginInterface.h"
#pragma warning(pop)
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

HWND OwnWindow(const wchar_t* class_name, const wchar_t* title = nullptr) {
    struct Query { const wchar_t* name; const wchar_t* title; HWND result; } query{class_name, title, nullptr};
    EnumWindows([](HWND window, LPARAM data) -> BOOL {
        auto& item = *reinterpret_cast<Query*>(data);
        DWORD process = 0; GetWindowThreadProcessId(window, &process);
        wchar_t name[128]{}, title_text[128]{};
        GetClassNameW(window, name, 128); GetWindowTextW(window, title_text, 128);
        if (process == GetCurrentProcessId() && wcscmp(name, item.name) == 0 &&
            (!item.title || wcscmp(title_text, item.title) == 0)) { item.result = window; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&query));
    return query.result;
}

int wmain() {
    wchar_t root[32768]{}; GetCurrentDirectoryW(32768, root);
    wchar_t path[32768]{}; swprintf_s(path, L"%s\\build\\CodexUsage.dll", root);
    for (int attempt = 0; attempt < 2; ++attempt) {
        HMODULE module = LoadLibraryW(path);
        if (!module) { printf("DLL load failed: %lu\n", GetLastError()); return 1; }
        using GetInstance = ITMPlugin* (*)();
        auto get_instance = reinterpret_cast<GetInstance>(GetProcAddress(module, "TMPluginGetInstance"));
        if (!get_instance) return 2;
        std::atomic<bool> panel_found{false};
        std::thread actor([&] {
            HWND options = nullptr;
            const ULONGLONG deadline = GetTickCount64() + 6000;
            while (!options && GetTickCount64() < deadline) { options = OwnWindow(L"CodexUsageOptionsWnd"); Sleep(20); }
            if (!options) return;
            SendDlgItemMessageW(options, 1001, BM_CLICK, 0, 0);
            std::thread closer([&] {
                const ULONGLONG panel_deadline = GetTickCount64() + 6000;
                while (GetTickCount64() < panel_deadline) {
                    HWND color = OwnWindow(L"#32770", L"颜色");
                    if (color) { panel_found = true; PostMessageW(color, WM_COMMAND, IDCANCEL, 0); return; }
                    Sleep(20);
                }
            });
            SendDlgItemMessageW(options, 1007, BM_CLICK, 0, 0);
            closer.join();
            PostMessageW(options, WM_COMMAND, IDCANCEL, 0);
        });
        const auto result = get_instance()->ShowOptionsDialog(nullptr);
        actor.join();
        WNDCLASSEXW registered{sizeof(registered)};
        const bool class_removed = GetClassInfoExW(module, L"CodexUsageOptionsWnd", &registered) == FALSE;
        FreeLibrary(module);
        if (!panel_found || result != ITMPlugin::OR_OPTION_UNCHANGED || !class_removed || GetModuleHandleW(path)) return 3;
    }
    printf("DLL color-dialog open/cancel, unload and reload checks passed\n");
    return 0;
}

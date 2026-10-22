#include "UsageBackend.h"
#pragma warning(push)
#pragma warning(disable: 4100)
#include "PluginInterface.h"
#pragma warning(pop)
#include <cstdio>
#include <fstream>
#include <string>
#include <algorithm>
#include <vector>
#pragma comment(lib, "gdi32.lib")

using namespace codex_usage;
int failures = 0;
int checks = 0;
void Check(bool condition, const char* message) {
    ++checks;
    if (!condition) { printf("FAIL: %s\n", message); ++failures; }
}
const std::string window = R"({"usedPercent":25,"windowDurationMins":300,"resetsAt":2000000000})";
const std::string snapshot = "{\"primary\":" + window + ",\"secondary\":null}";
const wchar_t* taskbar_labels[] = {L"Codex 5h额度:", L"Codex 7d额度:"};

class Host : public ITrafficMonitor {
public:
    explicit Host(std::wstring directory) : directory_(directory) {}
    int GetAPIVersion() override { return 1; }
    const wchar_t* GetVersion() override { return L"test"; }
    double GetMonitorValue(MonitorItem) override { return 0; }
    const wchar_t* GetMonitorValueString(MonitorItem, int) override { return L""; }
    void ShowNotifyMessage(const wchar_t*) override {}
    unsigned short GetLanguageId() const override { return 0; }
    const wchar_t* GetPluginConfigDir() const override { return directory_.c_str(); }
    int GetDPI(DPIType) const override { return 96; }
    unsigned int GetThemeColor() const override { return 0; }
private:
    std::wstring directory_;
};

bool RenderPreview(ITMPlugin* plugin, const std::wstring& path, const wchar_t* const* expected_labels = taskbar_labels) {
    constexpr int width = 520, height = 72, row_height = height / 2;
    HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    void* pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!dc || !bitmap || !pixels) { if (bitmap) DeleteObject(bitmap); if (dc) DeleteDC(dc); return false; }
    HGDIOBJ old_bitmap = SelectObject(dc, bitmap);
    HFONT font = CreateFontW(-20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HGDIOBJ old_font = SelectObject(dc, font);
    std::fill_n(static_cast<DWORD*>(pixels), width * height, 0x001c1c1c);
    plugin->OnExtenedInfo(ITMPlugin::EI_LABEL_TEXT_COLOR, L"12632256");
    plugin->OnExtenedInfo(ITMPlugin::EI_VALUE_TEXT_COLOR, L"15790320");
    plugin->OnExtenedInfo(ITMPlugin::EI_TASKBAR_WND_VALUE_RIGHT_ALIGN, L"1");
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(192, 192, 192));
    TEXTMETRICW metrics{}; GetTextMetricsW(dc, &metrics);
    SIZE label_sizes[2]{};
    for (int i = 0; i < 2; ++i) {
        GetTextExtentPoint32W(dc, expected_labels[i], static_cast<int>(wcslen(expected_labels[i])), &label_sizes[i]);
        TextOutW(dc, 16, i * row_height + (row_height - metrics.tmHeight) / 2,
            expected_labels[i], static_cast<int>(wcslen(expected_labels[i])));
    }
    GdiFlush();
    const std::vector<DWORD> expected_prefix(static_cast<DWORD*>(pixels), static_cast<DWORD*>(pixels) + width * height);
    std::fill_n(static_cast<DWORD*>(pixels), width * height, 0x001c1c1c);
    plugin->GetItem(0)->GetItemWidthEx(dc);
    plugin->GetItem(1)->GetItemWidthEx(dc);
    plugin->GetItem(0)->DrawItem(dc, 16, 0, width - 32, row_height, true);
    plugin->GetItem(1)->DrawItem(dc, 16, row_height, width - 32, row_height, true);
    GdiFlush();
    bool same_prefix = true;
    for (int i = 0; i < 2; ++i)
        for (int y = i * row_height; y < (i + 1) * row_height; ++y)
            for (int x = 16; x < 16 + label_sizes[i].cx; ++x)
                if (static_cast<DWORD*>(pixels)[y * width + x] != expected_prefix[y * width + x]) same_prefix = false;
    Check(same_prefix, "custom label text, host font, color and position remain unchanged");
    BITMAPFILEHEADER header{};
    header.bfType = 0x4d42;
    header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
    header.bfSize = header.bfOffBits + width * height * 4;
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(&header), sizeof(header));
    output.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(BITMAPINFOHEADER));
    output.write(static_cast<const char*>(pixels), width * height * 4);
    bool success = output.good();
    SelectObject(dc, old_font); SelectObject(dc, old_bitmap);
    DeleteObject(font); DeleteObject(bitmap); DeleteDC(dc);
    return success;
}

int wmain(int argc, wchar_t** argv) {
    UsageData data;
    Check(ParseRpcReply("{ \"id\": 7, \"result\": {} }", 7, nullptr), "spaced initialization reply");
    Check(!ParseRpcReply("{\"id\":70,\"result\":{}}", 7, nullptr), "request ID exact match");
    Check(!ParseRpcReply("{\"id\":7,\"error\":{},\"result\":{}}", 7, nullptr), "error reply rejected");
    Check(ParseRpcReply("{\"id\":7,\"result\":{\"rateLimits\":" + snapshot + "}}", 7, &data), "null secondary accepted");
    Check(data.primary.valid && !data.secondary.valid && data.primary.used_percent == 25, "independent window validity");
    const std::string other = R"({"limitId":"other","primary":{"usedPercent":80,"windowDurationMins":300,"resetsAt":2000000000}})";
    Check(ParseRpcReply("{\"id\":7,\"result\":{\"rateLimits\":" + other + ",\"rateLimitsByLimitId\":{\"codex\":" + snapshot + "}}}", 7, &data) && data.primary.used_percent == 25, "select codex bucket");
    Check(!ParseRpcReply("{\"id\":7,\"result\":{\"rateLimits\":" + other + "}}", 7, &data), "other bucket rejected");
    Check(!ParseRpcReply(R"({"id":7,"result":{"rateLimits":{"primary":null,"secondary":{"usedPercent":20,"windowDurationMins":60,"resetsAt":2000000000}}}})", 7, &data), "unknown duration not mislabeled");
    Check(!ParseRpcReply(R"({"id":7,"result":{"rateLimits":{"primary":{"usedPercent":101,"windowDurationMins":300,"resetsAt":2000000000}}}})", 7, &data), "invalid percent rejected");
    Check(!ParseRpcReply(R"({"id":7,"result":{"rateLimits":{"primary":{},"unrelated":{"usedPercent":25,"windowDurationMins":300,"resetsAt":2000000000}}}})", 7, &data), "no cross-object field matching");
    std::string rollout = R"({"timestamp":"2026-01-02T03:04:05.123Z","type":"event_msg","payload":{"type":"token_count","rate_limits":{"primary":{"used_percent":10,"window_minutes":300,"resets_at":2000000000},"secondary":null}}})";
    Check(ParseRolloutLine(rollout, data) && data.stale && data.updated_at == 1767323045, "snapshot preserves original timestamp");
    Check(!ParseRolloutLine("{\"type\":\"response_item\",\"payload\":{\"rate_limits\":" + snapshot + "}}", data), "conversation content is not quota data");
    Check(!ParseRpcReply(std::string(1024 * 1024 + 1, ' '), 7, &data), "oversized input rejected");
    Check(ResolveExecutable(L".\\codex.exe").empty(), "relative executable rejected");

    wchar_t temporary_root[MAX_PATH]{}; GetTempPathW(MAX_PATH, temporary_root);
    wchar_t temporary_name[MAX_PATH]{}; GetTempFileNameW(temporary_root, L"tmu", 0, temporary_name);
    DeleteFileW(temporary_name); CreateDirectoryW(temporary_name, nullptr);
    const std::wstring directory = temporary_name;
    const std::wstring plugin_directory = JoinPath(directory, L"plugins");
    CreateDirectoryW(plugin_directory.c_str(), nullptr);
    const std::wstring ini = JoinPath(plugin_directory, L"CodexUsage.ini");
    const std::wstring host_ini = JoinPath(directory, L"config.ini");
    const std::string host_preferences = "\xef\xbb\xbf[plugin_display_str_taskbar_window]\r\ncodex_usage_5h = Codex 5h额度:\r\ncodex_usage_7d = Codex 7d额度:\r\n[plugin_display_str_main_window]\r\ncodex_usage_5h =\r\n";
    { std::ofstream file(host_ini, std::ios::binary); file << host_preferences; }
    std::wstring label;
    Check(ReadDisplayPreference(host_ini, L"plugin_display_str_taskbar_window", L"codex_usage_5h", label) && label == taskbar_labels[0], "preserve original custom Codex prefix");
    Check(ReadDisplayPreference(host_ini, L"plugin_display_str_main_window", L"codex_usage_5h", label) && label.empty(), "preserve intentionally empty prefix");
    std::wstring taskbar_defaults[2], main_defaults[2];
    Check(ReadDisplayLabels(host_ini, taskbar_defaults, main_defaults) && main_defaults[0].empty() &&
        main_defaults[1] == L"Codex 7d额度:", "missing saved label uses full Codex default while explicit empty labels stay empty");
    Check(ReadRefreshMilliseconds(ini) == 300000, "default interval 300 seconds");
    const wchar_t* values[] = {L"120", L"1", L"-1", L"999999999999999999999999", L"broken"};
    const DWORD expected[] = {120000, 30000, 30000, 86400000, 300000};
    for (int i = 0; i < 5; ++i) {
        WritePrivateProfileStringW(L"config", L"refresh_seconds", values[i], ini.c_str());
        Check(ReadRefreshMilliseconds(ini) == expected[i], "INI interval parsing and bounds");
    }
    std::wstring rollout_path = JoinPath(directory, L"rollout-test.jsonl");
    { std::ofstream file(rollout_path); file << rollout << "\n{\"incomplete\":"; }
    Check(ReadTailAndParse(rollout_path, data) && data.primary.used_percent == 10, "ignore partial final line");

    wchar_t working_directory[32768]{}; GetCurrentDirectoryW(32768, working_directory);
    const std::wstring executable = JoinPath(working_directory, L"build\\mock_codex.exe");
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE private_handle = CreateEventW(&security, TRUE, FALSE, nullptr);
    wchar_t handle_text[64]{};
    swprintf_s(handle_text, L"%llu", static_cast<unsigned long long>(reinterpret_cast<ULONG_PTR>(private_handle)));
    SetEnvironmentVariableW(L"TM_TEST_HANDLE", handle_text);
    Check(FetchFromAppServer(executable, data) && data.primary.used_percent == 25 && data.from_app_server, "process handshake and handle isolation");
    Check(WaitForSingleObject(private_handle, 0) == WAIT_TIMEOUT, "private host event was not inherited");
    SetEnvironmentVariableW(L"TM_TEST_HANDLE", nullptr);
    CloseHandle(private_handle);

    WritePrivateProfileStringW(L"config", L"refresh_seconds", L"120", ini.c_str());
    WritePrivateProfileStringW(L"config", L"codex_path", executable.c_str(), ini.c_str());
    WritePrivateProfileStringW(L"config", L"show_reset_time", L"1", ini.c_str());
    WritePrivateProfileStringW(L"config", L"time_font_size", L"9", ini.c_str());
    std::wstring dll_path = JoinPath(working_directory, L"build\\CodexUsage.dll");
    HMODULE module = LoadLibraryW(dll_path.c_str());
    Check(module != nullptr, "load plugin DLL");
    if (module) {
        using GetInstance = ITMPlugin* (*)();
        auto get_instance = reinterpret_cast<GetInstance>(GetProcAddress(module, "TMPluginGetInstance"));
        Check(get_instance != nullptr, "plugin export");
        if (get_instance) {
            Host host(plugin_directory);
            auto* plugin = get_instance();
            Check(wcscmp(plugin->GetItem(0)->GetItemLableText(), L"Codex 5h额度:") == 0 &&
                wcscmp(plugin->GetItem(1)->GetItemLableText(), L"Codex 7d额度:") == 0,
                "native display defaults include Codex and quota labels");
            Check(!plugin->GetItem(0)->IsCustomDraw(), "reset-time display is opt-in");
            plugin->OnInitialize(&host);
            Check(plugin->GetItem(0)->IsCustomDraw(), "reset-time INI option enables custom drawing");
            HDC drawing_dc = CreateCompatibleDC(nullptr);
            Check(plugin->GetItem(1)->GetItemWidthEx(drawing_dc) > plugin->GetItem(0)->GetItemWidthEx(drawing_dc), "weekly reset timestamp reserves more width");
            DeleteDC(drawing_dc);
            FreeLibrary(module);
            Check(GetModuleHandleW(dll_path.c_str()) != nullptr, "worker pins DLL during query");
            ULONGLONG deadline = GetTickCount64() + 5000;
            while (GetModuleHandleW(dll_path.c_str()) && GetTickCount64() < deadline) Sleep(20);
            Check(GetModuleHandleW(dll_path.c_str()) == nullptr, "DLL safely unloads after worker exits");
        } else FreeLibrary(module);
    }
    module = LoadLibraryW(dll_path.c_str());
    if (module) {
        using GetInstance = ITMPlugin* (*)();
        auto get_instance = reinterpret_cast<GetInstance>(GetProcAddress(module, "TMPluginGetInstance"));
        Host host(plugin_directory);
        ITMPlugin* plugin = get_instance();
        plugin->OnInitialize(&host);
        ULONGLONG deadline = GetTickCount64() + 5000;
        while (wcscmp(plugin->GetItem(0)->GetItemValueText(), L"--") == 0 && GetTickCount64() < deadline) Sleep(20);
        Check(wcscmp(plugin->GetItem(0)->GetItemValueText(), L"75%") == 0, "quota value remains correct with reset time enabled");
        Check(RenderPreview(plugin, JoinPath(working_directory, L"build\\reset-time-preview.bmp")), "render reset times with a smaller font");
        HDC layout_dc = CreateCompatibleDC(nullptr);
        int original_width = plugin->GetItem(0)->GetItemWidthEx(layout_dc);
        int original_weekly_width = plugin->GetItem(1)->GetItemWidthEx(layout_dc);
        const std::string updated_preferences = "\xef\xbb\xbf[plugin_display_str_taskbar_window]\r\ncodex_usage_5h = 本月 Codex 自定义额度名称:\r\ncodex_usage_7d =\r\n[plugin_display_str_main_window]\r\ncodex_usage_5h = 主窗口五小时:\r\ncodex_usage_7d = 主窗口七天:\r\n";
        { std::ofstream file(host_ini, std::ios::binary); file << updated_preferences; }
        Sleep(300);
        Check(plugin->GetItem(0)->GetItemWidthEx(layout_dc) > original_width, "longer edited label updates layout width without plugin restart");
        Check(plugin->GetItem(1)->GetItemWidthEx(layout_dc) < original_weekly_width, "cleared label removes its reserved width without plugin restart");
        DeleteDC(layout_dc);
        const wchar_t* edited_labels[] = {L"本月 Codex 自定义额度名称:", L""};
        Check(RenderPreview(plugin, JoinPath(working_directory, L"build\\edited-label-preview.bmp"), edited_labels), "render labels edited while reset time is enabled");
        plugin->OnExtenedInfo(ITMPlugin::EI_DRAW_TASKBAR_WND, L"0");
        const wchar_t* main_labels[] = {L"主窗口五小时:", L"主窗口七天:"};
        Check(RenderPreview(plugin, JoinPath(working_directory, L"build\\main-label-preview.bmp"), main_labels), "main-window labels remain independent from taskbar labels");
        plugin->OnExtenedInfo(ITMPlugin::EI_DRAW_TASKBAR_WND, L"1");
        DeleteFileW(host_ini.c_str());
        Sleep(300);
        Check(RenderPreview(plugin, JoinPath(working_directory, L"build\\unavailable-label-preview.bmp"), edited_labels), "temporary configuration unavailability preserves the last labels");
        { std::ofstream file(host_ini, std::ios::binary); file << host_preferences; }
        FreeLibrary(module);
        deadline = GetTickCount64() + 5000;
        while (GetModuleHandleW(dll_path.c_str()) && GetTickCount64() < deadline) Sleep(20);
        Check(GetModuleHandleW(dll_path.c_str()) == nullptr, "rendered plugin unloads before historical-data check");
    }
    WritePrivateProfileStringW(L"config", L"codex_path", JoinPath(directory, L"missing.exe").c_str(), ini.c_str());
    WritePrivateProfileStringW(L"config", L"allow_local_fallback", L"1", ini.c_str());
    WritePrivateProfileStringW(L"config", L"sessions_dir", directory.c_str(), ini.c_str());
    module = LoadLibraryW(dll_path.c_str());
    Check(module != nullptr, "load plugin for historical display");
    if (module) {
        using GetInstance = ITMPlugin* (*)();
        auto get_instance = reinterpret_cast<GetInstance>(GetProcAddress(module, "TMPluginGetInstance"));
        Host host(plugin_directory);
        auto* plugin = get_instance();
        plugin->OnInitialize(&host);
        ULONGLONG deadline = GetTickCount64() + 5000;
        while (wcsstr(plugin->GetTooltipInfo(), L"历史数据") == nullptr && GetTickCount64() < deadline) Sleep(20);
        Check(wcscmp(plugin->GetItem(0)->GetItemValueText(), L"90%") == 0, "historical percentage ends at percent sign without asterisk");
        Check(wcsstr(plugin->GetTooltipInfo(), L"历史数据") != nullptr, "historical status stays visible in tooltip");
        FreeLibrary(module);
        deadline = GetTickCount64() + 5000;
        while (GetModuleHandleW(dll_path.c_str()) && GetTickCount64() < deadline) Sleep(20);
    }
    std::ifstream host_file(host_ini, std::ios::binary);
    const std::string saved_preferences((std::istreambuf_iterator<char>(host_file)), std::istreambuf_iterator<char>());
    Check(saved_preferences == host_preferences, "host display preferences were not modified");
    host_file.close();
    DeleteFileW(rollout_path.c_str()); DeleteFileW(ini.c_str()); DeleteFileW(host_ini.c_str());
    RemoveDirectoryW(plugin_directory.c_str()); RemoveDirectoryW(directory.c_str());
    printf("%s (%d checks, %d failures)\n", failures ? "FAILED" : "All synthetic tests passed", checks, failures);
    if (!failures && argc == 3 && wcscmp(argv[1], L"--live") == 0) {
        UsageData live;
        bool success = FetchFromAppServer(argv[2], live);
        printf("Live read-only query: %s\n", success ? "OK" : "FAILED");
        if (success) printf("5h available=%d used=%.0f%%; 7d available=%d used=%.0f%%\n",
            static_cast<int>(live.primary.valid), live.primary.used_percent,
            static_cast<int>(live.secondary.valid), live.secondary.used_percent);
        else ++failures;
    }
    return failures ? 1 : 0;
}

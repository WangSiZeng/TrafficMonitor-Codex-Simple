#include "UsageBackend.h"
#include "GraphDrawing.h"
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
#pragma comment(lib, "user32.lib")

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
    unsigned int GetThemeColor() const override { return RGB(24, 118, 183); }
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

std::vector<DWORD> RenderGraph(ITMPlugin* plugin, int filled_percent, bool dark, bool host_graph, bool show_time, bool main_window = false,
    const std::wstring& path = {}, bool scrolling = false, COLORREF expected_color = CLR_INVALID) {
    constexpr int width = 432, height = 52, x = 16, y = 8, item_width = 400, item_height = 36;
    const COLORREF background = dark ? RGB(28, 28, 28) : RGB(255, 255, 255);
    const COLORREF foreground = dark ? RGB(240, 240, 240) : RGB(16, 16, 16);
    HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    void* pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!dc || !bitmap || !pixels) {
        Check(false, "create graph rendering surface");
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
        return {};
    }
    HGDIOBJ old_bitmap = SelectObject(dc, bitmap);
    HFONT font = CreateFontW(-20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HGDIOBJ old_font = SelectObject(dc, font);
    RECT surface{0, 0, width, height};
    SetDCBrushColor(dc, background);
    FillRect(dc, &surface, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    // Capture solid foreground pixels of the unchanged label and percentage.
    // Antialiased edge colors can legitimately blend with the new bar fill.
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, foreground);
    TEXTMETRICW metrics{}; GetTextMetricsW(dc, &metrics);
    const int text_y = y + (item_height - metrics.tmHeight) / 2;
    const std::wstring label = L"Quota: ";
    const std::wstring value = plugin->GetItem(0)->GetItemValueText();
    SIZE label_size{}, sample_size{}, value_size{}, space_size{};
    GetTextExtentPoint32W(dc, label.c_str(), static_cast<int>(label.size()), &label_size);
    GetTextExtentPoint32W(dc, L"100%", 4, &sample_size);
    GetTextExtentPoint32W(dc, value.c_str(), static_cast<int>(value.size()), &value_size);
    GetTextExtentPoint32W(dc, L" ", 1, &space_size);
    TextOutW(dc, x, text_y, label.c_str(), static_cast<int>(label.size()));
    TextOutW(dc, x + label_size.cx + (main_window ? 0 : sample_size.cx - value_size.cx),
        text_y, value.c_str(), static_cast<int>(value.size()));
    GdiFlush();
    std::vector<POINT> solid_text;
    for (int row = y; row < y + item_height; ++row)
        for (int column = x; column < x + label_size.cx + sample_size.cx; ++column)
            if (GetPixel(dc, column, row) == foreground) solid_text.push_back(POINT{column, row});
    FillRect(dc, &surface, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    plugin->OnExtenedInfo(ITMPlugin::EI_DRAW_TASKBAR_WND, main_window ? L"0" : L"1");
    plugin->OnExtenedInfo(ITMPlugin::EI_LABEL_TEXT_COLOR, std::to_wstring(foreground).c_str());
    plugin->OnExtenedInfo(ITMPlugin::EI_VALUE_TEXT_COLOR, std::to_wstring(foreground).c_str());
    plugin->OnExtenedInfo(ITMPlugin::EI_TASKBAR_WND_VALUE_RIGHT_ALIGN, L"1");
    IPluginItem* item = plugin->GetItem(0);
    // Mimic the host's independent native graph pass. It must always be
    // opted out, including when the user enables the host's global switch.
    if (host_graph && item->IsDrawResourceUsageGraph()) {
        RECT native_fill{x, y, x + item_width, y + item_height};
        SetDCBrushColor(dc, RGB(255, 0, 0));
        FillRect(dc, &native_fill, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    }
    item->GetItemWidthEx(dc);
    const COLORREF brush_before = GetDCBrushColor(dc);
    item->DrawItem(dc, x, y, item_width, item_height, dark);
    Check(GetCurrentObject(dc, OBJ_FONT) == font && GetDCBrushColor(dc) == brush_before,
        "custom graph restores the host's font and brush state");
    GdiFlush();
    const int expected_width = scrolling ? 0 : item_width * filled_percent / 100;
    const COLORREF fill = GetPixel(dc, x, y);
    bool correct_bar = expected_width == 0 || fill != background;
    for (int column = 0; column < item_width; ++column)
        if (GetPixel(dc, x + column, y) != (column < expected_width ? fill : background)) correct_bar = false;
    Check(correct_bar, "graph pixels match the selected quota fraction, including zero and full");
    if (scrolling) {
        const int expected_height = item_height * filled_percent / 100;
        const COLORREF column_color = GetPixel(dc, x + item_width - 1, y + item_height - 1);
        bool correct_column = expected_height == 0 || column_color != background;
        for (int row = 0; row < item_height; ++row)
            if (GetPixel(dc, x + item_width - 1, y + row) !=
                (row >= item_height - expected_height ? column_color : background)) correct_column = false;
        Check(correct_column, "scrolling graph draws the latest quota as the rightmost history column");
    }
    if (filled_percent > 0 && expected_color != CLR_INVALID)
        Check(GetPixel(dc, scrolling ? x + item_width - 1 : x, y + item_height - 1) == expected_color,
            "graph uses the configured color or host Windows theme color");
    Check(!solid_text.empty() && std::all_of(solid_text.begin(), solid_text.end(), [&](const POINT& point) {
        return GetPixel(dc, point.x, point.y) == foreground;
    }), "independent graph preserves custom label, percentage font and alignment");
    bool extra_text = false;
    for (int column = x + label_size.cx + sample_size.cx + space_size.cx; column < x + item_width; ++column) {
        for (int row = y; row < y + item_height; ++row)
            if (GetPixel(dc, column, row) == foreground) extra_text = true;
    }
    Check(extra_text == show_time, "reset-time text appears only when its own switch is enabled");
    Check(GetPixel(dc, x - 1, y) == background && GetPixel(dc, x + item_width, y) == background &&
        GetPixel(dc, x, y - 1) == background && GetPixel(dc, x, y + item_height) == background,
        "custom graph stays inside the item rectangle");
    const std::vector<DWORD> result(static_cast<DWORD*>(pixels), static_cast<DWORD*>(pixels) + width * height);
    if (!path.empty()) {
        BITMAPFILEHEADER header{};
        header.bfType = 0x4d42; header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
        header.bfSize = header.bfOffBits + width * height * 4;
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(&header), sizeof(header));
        file.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(BITMAPINFOHEADER));
        file.write(static_cast<const char*>(pixels), width * height * 4);
        Check(file.good(), "save independent graph preview");
    }
    SelectObject(dc, old_font); SelectObject(dc, old_bitmap);
    DeleteObject(font); DeleteObject(bitmap); DeleteDC(dc);
    return result;
}

int wmain(int argc, wchar_t** argv) {
    GraphHistory history;
    history.Record(1000, 25); history.Record(1500, 40);
    Check(history.Snapshot().size() == 1 && history.Snapshot()[0].used_percent == 40,
        "multiple paints in one second update a single history sample");
    history.Record(2000, 50); history.Record(4000, 100);
    history.Record(5000, -1); history.Record(5000, 101);
    Check(history.Snapshot().size() == 3, "history rejects invalid percentages");
    HDC history_dc = CreateCompatibleDC(nullptr);
    HBITMAP history_bitmap = CreateBitmap(8, 10, 1, 32, nullptr);
    HGDIOBJ old_history_bitmap = SelectObject(history_dc, history_bitmap);
    RECT history_area{0, 0, 8, 10};
    SetDCBrushColor(history_dc, RGB(0, 0, 0));
    FillRect(history_dc, &history_area, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    SetDCBrushColor(history_dc, RGB(10, 90, 150));
    history.Draw(history_dc, history_area, 4000, false);
    Check(GetPixel(history_dc, 7, 0) == RGB(10, 90, 150) && GetPixel(history_dc, 6, 9) == RGB(0, 0, 0) &&
        GetPixel(history_dc, 5, 4) == RGB(0, 0, 0) && GetPixel(history_dc, 5, 5) == RGB(10, 90, 150),
        "history plots used percentages at the correct time positions and leaves gaps empty");
    SetDCBrushColor(history_dc, RGB(0, 0, 0));
    FillRect(history_dc, &history_area, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    SetDCBrushColor(history_dc, RGB(10, 90, 150));
    history.Draw(history_dc, history_area, 4000, true);
    Check(GetPixel(history_dc, 7, 9) == RGB(0, 0, 0) && GetPixel(history_dc, 4, 4) == RGB(10, 90, 150),
        "changing history data logic reinterprets previous samples as remaining percentages");
    SelectObject(history_dc, old_history_bitmap); DeleteObject(history_bitmap); DeleteDC(history_dc);
    for (ULONGLONG second = 1; second <= 5000; ++second) history.Record(second * 1000, 25);
    Check(history.Snapshot().size() == GraphHistory::capacity && history.Snapshot().front().second == 905,
        "scrolling history retains a bounded number of recent samples");
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
    const std::string quoted_preferences = "\xef\xbb\xbf[plugin_display_str_taskbar_window]\r\ncodex_usage_5h = \"  自定义额度:  \"\r\ncodex_usage_7d = \"\"\r\n[plugin_display_str_main_window]\r\ncodex_usage_5h = $  旧版额度: $\r\ncodex_usage_7d = 金额$内部\r\n";
    { std::ofstream file(host_ini, std::ios::binary); file << quoted_preferences; }
    Check(ReadDisplayLabels(host_ini, taskbar_defaults, main_defaults) && taskbar_defaults[0] == L"  自定义额度:  ",
        "host string quoting preserves surrounding spaces without drawing quote markers");
    Check(taskbar_defaults[1].empty(), "quoted empty host labels remain empty");
    Check(main_defaults[0] == L"  旧版额度: " && main_defaults[1] == L"金额$内部",
        "legacy boundary markers match host decoding without altering internal characters");
    { std::ofstream file(host_ini, std::ios::binary); file << host_preferences; }
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
            WritePrivateProfileStringW(L"config", L"show_reset_time", L"0", ini.c_str());
            plugin->OnExtenedInfo(ITMPlugin::EI_CONFIG_DIR, (plugin_directory + L"\\").c_str());
            Check(plugin->GetItem(0)->IsCustomDraw(), "modern host's second initialization event does not reload options");
            WritePrivateProfileStringW(L"config", L"show_reset_time", L"1", ini.c_str());
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

    // TrafficMonitor V1.85.1 never calls OnInitialize. It supplies the real
    // plugin configuration directory only through EI_CONFIG_DIR (API 2).
    // Keep this fixture outside the DLL's build directory, as in AppData mode.
    const std::string legacy_preferences = "\xef\xbb\xbf[plugin_display_str_taskbar_window]\r\ncodex_usage_5h = 另一台电脑五小时:\r\ncodex_usage_7d = \"\"\r\n[plugin_display_str_main_window]\r\ncodex_usage_5h = \" 主窗口五小时: \"\r\ncodex_usage_7d = 主窗口七天:\r\n";
    { std::ofstream file(host_ini, std::ios::binary); file << legacy_preferences; }
    module = LoadLibraryW(dll_path.c_str());
    Check(module != nullptr, "load plugin with legacy host initialization");
    if (module) {
        using GetInstance = ITMPlugin* (*)();
        auto get_instance = reinterpret_cast<GetInstance>(GetProcAddress(module, "TMPluginGetInstance"));
        ITMPlugin* plugin = get_instance();
        plugin->OnExtenedInfo(ITMPlugin::EI_CONFIG_DIR, (plugin_directory + L"\\").c_str());
        Check(plugin->GetItem(0)->IsCustomDraw(), "API 2 config-directory event loads the reset-time option");
        ULONGLONG deadline = GetTickCount64() + 5000;
        while (wcscmp(plugin->GetItem(0)->GetItemValueText(), L"--") == 0 && GetTickCount64() < deadline) Sleep(20);
        Check(wcscmp(plugin->GetItem(0)->GetItemValueText(), L"75%") == 0, "legacy initialization starts the quota query");
        const wchar_t* legacy_labels[] = {L"另一台电脑五小时:", L""};
        Check(RenderPreview(plugin, JoinPath(working_directory, L"build\\legacy-label-preview.bmp"), legacy_labels), "legacy host uses saved labels from the supplied directory");
        std::string legacy_edited = legacy_preferences;
        legacy_edited.replace(legacy_edited.find("另一台电脑五小时:"), std::string("另一台电脑五小时:").size(), "在旧版中修改后的五小时额度:");
        { std::ofstream file(host_ini, std::ios::binary); file << legacy_edited; }
        Sleep(300);
        const wchar_t* legacy_edited_labels[] = {L"在旧版中修改后的五小时额度:", L""};
        Check(RenderPreview(plugin, JoinPath(working_directory, L"build\\legacy-edited-label-preview.bmp"), legacy_edited_labels), "legacy host edits synchronize while reset time remains enabled");
        plugin->OnExtenedInfo(ITMPlugin::EI_DRAW_TASKBAR_WND, L"0");
        const wchar_t* main_labels[] = {L" 主窗口五小时: ", L"主窗口七天:"};
        Check(RenderPreview(plugin, JoinPath(working_directory, L"build\\legacy-main-label-preview.bmp"), main_labels), "legacy main-window labels stay independent");
        FreeLibrary(module);
        deadline = GetTickCount64() + 5000;
        while (GetModuleHandleW(dll_path.c_str()) && GetTickCount64() < deadline) Sleep(20);
        Check(GetModuleHandleW(dll_path.c_str()) == nullptr, "legacy plugin unloads safely");
    }
    { std::ofstream file(host_ini, std::ios::binary); file << host_preferences; }

    struct GraphCase {
        bool enabled, time, remaining, legacy, valid; int used;
        COLORREF color = RGB(192, 192, 192); bool theme = false, scroll = false;
    };
    const GraphCase graph_cases[] = {
        {false, false, true, false, true, 25}, {false, true, true, true, true, 25},
        {true, false, true, false, true, 25}, {true, true, true, true, true, 25},
        {true, false, false, true, true, 25}, {true, true, false, false, true, 25},
        {true, false, true, false, true, 100}, {true, false, true, true, true, 0},
        {true, false, true, false, false, 25},
        {true, false, true, false, true, 25, RGB(10, 90, 150), false, false},
        {true, false, true, false, true, 25, RGB(10, 90, 150), true, false},
        {true, false, true, false, true, 25, RGB(10, 90, 150), false, true}
    };
    const std::string graph_labels = "\xef\xbb\xbf[plugin_display_str_taskbar_window]\r\ncodex_usage_5h = Quota:\r\ncodex_usage_7d = Quota:\r\n[plugin_display_str_main_window]\r\ncodex_usage_5h = Quota:\r\ncodex_usage_7d = Quota:\r\n";
    { std::ofstream file(host_ini, std::ios::binary); file << graph_labels; }
    for (const auto& scenario : graph_cases) {
        WritePrivateProfileStringW(L"config", L"graph_enabled", scenario.enabled ? L"1" : L"0", ini.c_str());
        WritePrivateProfileStringW(L"config", L"show_reset_time", scenario.time ? L"1" : L"0", ini.c_str());
        WritePrivateProfileStringW(L"config", L"graph_mode", scenario.remaining ? L"remaining" : L"used", ini.c_str());
        WritePrivateProfileStringW(L"config", L"graph_color", std::to_wstring(scenario.color).c_str(), ini.c_str());
        WritePrivateProfileStringW(L"config", L"graph_follow_theme", scenario.theme ? L"1" : L"0", ini.c_str());
        WritePrivateProfileStringW(L"config", L"graph_type", scenario.scroll ? L"scroll" : L"bar", ini.c_str());
        WritePrivateProfileStringW(L"config", L"codex_path", scenario.valid ? executable.c_str() : JoinPath(directory, L"missing.exe").c_str(), ini.c_str());
        SetEnvironmentVariableW(L"TM_TEST_USED_PERCENT", std::to_wstring(scenario.used).c_str());
        module = LoadLibraryW(dll_path.c_str());
        Check(module != nullptr, "load graph test plugin");
        if (!module) continue;
        using GetInstance = ITMPlugin* (*)();
        auto get_instance = reinterpret_cast<GetInstance>(GetProcAddress(module, "TMPluginGetInstance"));
        ITMPlugin* plugin = get_instance();
        Host host(plugin_directory);
        if (scenario.legacy) plugin->OnExtenedInfo(ITMPlugin::EI_CONFIG_DIR, plugin_directory.c_str());
        else {
            plugin->OnInitialize(&host);
            plugin->OnExtenedInfo(ITMPlugin::EI_CONFIG_DIR, plugin_directory.c_str());
        }
        ULONGLONG deadline = GetTickCount64() + 5000;
        while (scenario.valid && wcscmp(plugin->GetItem(0)->GetItemValueText(), L"--") == 0 && GetTickCount64() < deadline) Sleep(20);
        const std::wstring expected_value = scenario.valid ? std::to_wstring(100 - scenario.used) + L"%" : L"--";
        Check(plugin->GetItem(0)->GetItemValueText() == expected_value, "graph mode does not change quota text");
        Check(plugin->GetItem(0)->IsCustomDraw() == (scenario.enabled || scenario.time), "graph and time independently request custom drawing");
        Check(plugin->GetItem(0)->IsDrawResourceUsageGraph() == 0, "native host graph is disabled to prevent double drawing");
        HDC layout_dc = CreateCompatibleDC(nullptr);
        const int daily_width = plugin->GetItem(0)->GetItemWidthEx(layout_dc);
        const int weekly_width = plugin->GetItem(1)->GetItemWidthEx(layout_dc);
        DeleteDC(layout_dc);
        Check(scenario.time ? weekly_width > daily_width : weekly_width == daily_width,
            "hidden reset time reserves no timestamp width");
        const int fraction = !scenario.enabled || !scenario.valid ? 0 : (scenario.remaining ? 100 - scenario.used : scenario.used);
        for (bool dark : {false, true}) {
            const bool preview = scenario.enabled && !scenario.legacy && scenario.valid && scenario.used == 25;
            const std::wstring path = preview ? JoinPath(working_directory, dark
                ? (scenario.time ? L"build\\graph-time-dark.bmp" : L"build\\graph-only-dark.bmp")
                : (scenario.time ? L"build\\graph-time-light.bmp" : L"build\\graph-only-light.bmp")) : L"";
            const COLORREF graph_color = scenario.theme ? host.GetThemeColor() : scenario.color;
            const auto without_host_graph = RenderGraph(plugin, fraction, dark, false, scenario.time, false, path, scenario.scroll, graph_color);
            const auto with_host_graph = RenderGraph(plugin, fraction, dark, true, scenario.time, false, L"", scenario.scroll, graph_color);
            Check(!without_host_graph.empty() && (scenario.scroll || without_host_graph == with_host_graph),
                "host global graph switch has no effect on plugin drawing");
        }
        if (scenario.enabled && scenario.valid) RenderGraph(plugin, 0, true, false, scenario.time, true);
        FreeLibrary(module);
        deadline = GetTickCount64() + 5000;
        while (GetModuleHandleW(dll_path.c_str()) && GetTickCount64() < deadline) Sleep(20);
        Check(GetModuleHandleW(dll_path.c_str()) == nullptr, "graph test plugin unloads safely");
    }
    SetEnvironmentVariableW(L"TM_TEST_USED_PERCENT", nullptr);
    { std::ofstream file(host_ini, std::ios::binary); file << host_preferences; }
    WritePrivateProfileStringW(L"config", L"graph_enabled", L"0", ini.c_str());
    WritePrivateProfileStringW(L"config", L"show_reset_time", L"1", ini.c_str());
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

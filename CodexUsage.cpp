/*********************************************************
* CodexUsage - TrafficMonitor plugin
* Queries Codex app-server; optional local snapshots are opt-in.
**********************************************************/
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <atomic>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <dwmapi.h>
#pragma warning(push)
#pragma warning(disable: 4100)
#include "PluginInterface.h"
#pragma warning(pop)
#include "UsageBackend.h"
#include "ColorPicker.h"
#include "GraphDrawing.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")

using namespace codex_usage;
namespace {
HMODULE g_hModule = nullptr;
constexpr int IDC_GRAPH_ENABLE = 1001;
constexpr int IDC_GRAPH_REMAINING = 1002;
constexpr int IDC_GRAPH_USED = 1003;
constexpr int IDC_SHOW_RESET_TIME = 1004;
constexpr int IDC_TIME_FONT_SIZE = 1005;
constexpr int IDC_TIME_FONT_SPIN = 1006;
constexpr int IDC_GRAPH_COLOR = 1007;
constexpr int IDC_GRAPH_THEME = 1008;
constexpr int IDC_GRAPH_BAR = 1009;
constexpr int IDC_GRAPH_SCROLL = 1010;
constexpr int IDC_GRAPH_LOGIC_LABEL = 1011;
constexpr int IDC_GRAPH_COLOR_LABEL = 1012;
constexpr int IDC_GRAPH_TYPE_LABEL = 1013;
constexpr int kOptionsClientW = 400;
constexpr int kOptionsClientH = 292;

class CriticalLock {
public:
    explicit CriticalLock(CRITICAL_SECTION& section) : section_(section) { EnterCriticalSection(&section_); }
    ~CriticalLock() { LeaveCriticalSection(&section_); }
    CriticalLock(const CriticalLock&) = delete;
    CriticalLock& operator=(const CriticalLock&) = delete;
private:
    CRITICAL_SECTION& section_;
};

class ExclusiveSrwLock {
public:
    explicit ExclusiveSrwLock(SRWLOCK& lock) : lock_(lock) { AcquireSRWLockExclusive(&lock_); }
    ~ExclusiveSrwLock() { ReleaseSRWLockExclusive(&lock_); }
    ExclusiveSrwLock(const ExclusiveSrwLock&) = delete;
    ExclusiveSrwLock& operator=(const ExclusiveSrwLock&) = delete;
private:
    SRWLOCK& lock_;
};

class DrawingFonts {
public:
    DrawingFonts(HDC dc, int time_points, int dpi, const LOGFONTW* normal_description = nullptr) : dc_(dc) {
        original_ = static_cast<HFONT>(GetCurrentObject(dc, OBJ_FONT));
        if (normal_description) owned_normal_ = CreateFontIndirectW(normal_description);
        normal_ = owned_normal_ ? owned_normal_ : (original_ ? original_ : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
        SelectObject(dc_, normal_);
        LOGFONTW description{};
        GetObjectW(normal_, sizeof(description), &description);
        description.lfHeight = time_points > 0 ? -MulDiv(time_points, dpi, 72)
            : -(std::max)(7L, (std::abs(description.lfHeight) * 3) / 4);
        description.lfWidth = 0;
        description.lfWeight = FW_NORMAL;
        small_ = CreateFontIndirectW(&description);
        GetTextMetricsW(dc_, &normal_metrics);
        SelectObject(dc_, small_ ? small_ : normal_);
        GetTextMetricsW(dc_, &small_metrics);
        SelectObject(dc_, normal_);
    }
    ~DrawingFonts() {
        SelectObject(dc_, original_ ? original_ : normal_);
        if (small_) DeleteObject(small_);
        if (owned_normal_) DeleteObject(owned_normal_);
    }
    void Normal() { SelectObject(dc_, normal_); }
    void Small() { SelectObject(dc_, small_ ? small_ : normal_); }
    int Width(const std::wstring& text) const {
        SIZE size{};
        GetTextExtentPoint32W(dc_, text.c_str(), static_cast<int>(text.size()), &size);
        return size.cx;
    }
    TEXTMETRICW normal_metrics{}, small_metrics{};
private:
    HDC dc_;
    HFONT original_ = nullptr, normal_ = nullptr, owned_normal_ = nullptr, small_ = nullptr;
};
std::wstring FormatTime(long long unix_ts, bool with_date)
{
    if (unix_ts <= 0) return L"未知";
    time_t t = (time_t)unix_ts;
    tm lt{};
    localtime_s(&lt, &t);
    wchar_t buf[64];
    if (with_date)
        wcsftime(buf, 64, L"%m-%d %H:%M", &lt);
    else
        wcsftime(buf, 64, L"%H:%M", &lt);
    return buf;
}

} // namespace

// 单个显示项目（5h 或 7d）
class CQuotaItem : public IPluginItem
{
public:
    CQuotaItem(bool is_primary) : m_is_primary(is_primary) {}

    const wchar_t* GetItemName() const override
    {
        return m_is_primary ? L"Codex 5小时额度" : L"Codex 7天额度";
    }

    const wchar_t* GetItemId() const override
    {
        return m_is_primary ? L"codex_usage_5h" : L"codex_usage_7d";
    }

    const wchar_t* GetItemLableText() const override
    {
        return DefaultDisplayLabel(m_is_primary);
    }

    const wchar_t* GetItemValueText() const override;

    const wchar_t* GetItemValueSampleText() const override
    {
        return L"100%";
    }

    int IsDrawResourceUsageGraph() const override;

    float GetResourceUsageGraphValue() const override;

    bool IsCustomDraw() const override;
    int GetItemWidth() const override { return 240; }
    int GetItemWidthEx(void* hDC) const override;
    void DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode) override;

    void InitializeGraphHistory(ULONGLONG origin, DWORD interval) { m_graph_history.Reset(origin, interval); }
    void RecordGraphSample(ULONGLONG query_started, const QuotaWindow& window) {
        if (window.valid) m_graph_history.Record(query_started, window.used_percent);
    }

private:
    bool m_is_primary;
    mutable SRWLOCK m_font_lock = SRWLOCK_INIT;
    mutable LOGFONTW m_normal_description{};
    mutable bool m_has_normal_description = false;
    GraphHistory m_graph_history;
};

// 插件主类
class CCodexUsagePlugin : public ITMPlugin
{
public:
    static CCodexUsagePlugin& Instance()
    {
        static CCodexUsagePlugin instance;
        return instance;
    }

    IPluginItem* GetItem(int index) override
    {
        if (index == 0) return &m_item_5h;
        if (index == 1) return &m_item_7d;
        return nullptr;
    }

    void DataRequired() override
    {
        if (!m_initialized) return;
        ULONGLONG now = GetTickCount64();
        if (now - m_last_scan < m_refresh_ms)
            return;
        m_last_scan = now;
        KickRefresh();
    }

    const wchar_t* GetInfo(PluginInfoIndex index) override
    {
        switch (index)
        {
        case TMI_NAME:
            return L"Codex 额度";
        case TMI_DESCRIPTION:
            return L"显示 Codex 5小时窗口和7天窗口的额度使用情况（app-server 实时查询）。";
        case TMI_AUTHOR:
            return L"CodexUsage contributors";
        case TMI_COPYRIGHT:
            return L"Copyright (C) 2026";
        case TMI_VERSION:
            return L"0.1.4";
        default:
            break;
        }
        return L"";
    }

    const wchar_t* GetTooltipInfo() override
    {
        static thread_local std::wstring snapshot;
        CriticalLock lock(m_cs);
        snapshot = m_tooltip;
        return snapshot.c_str();
    }

    void* GetPluginIcon() override
    {
        // Preserve the original locally installed ChatGPT icon.
        static HICON icon = []() -> HICON {
            wchar_t local[MAX_PATH]{};
            DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
            if (!length || length >= MAX_PATH) return nullptr;
            std::wstring path = JoinPath(local, L"OpenAI\\Logo\\ChatGPT.ico");
            return static_cast<HICON>(LoadImageW(nullptr, path.c_str(), IMAGE_ICON,
                GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_LOADFROMFILE));
        }();
        return icon;
    }

    OptionReturn ShowOptionsDialog(void* hParent) override;

    bool ShowRemaining() const { return m_show_remaining; }
    bool GraphEnabled() const { return m_graph_enabled; }
    bool GraphRemaining() const { return m_graph_remaining; }
    bool GraphScrolling() const { return m_graph_scrolling; }
    bool GraphFollowsTheme() const { return m_graph_follow_theme; }
    COLORREF CustomGraphColor() const { return m_graph_color; }
    COLORREF ThemeColor() const {
        if (m_app) return m_app->GetThemeColor() & 0xffffff;
        DWORD argb = 0; BOOL opaque = FALSE;
        if (SUCCEEDED(DwmGetColorizationColor(&argb, &opaque)))
            return RGB((argb >> 16) & 255, (argb >> 8) & 255, argb & 255);
        return GetSysColor(COLOR_HIGHLIGHT);
    }
    COLORREF GraphColor() const { return m_graph_follow_theme ? ThemeColor() : m_graph_color.load(); }
    bool ShowResetTime() const { return m_show_reset_time; }
    bool DrawingTaskbar() const { return m_drawing_taskbar; }
    int TimeFontSize() const { return m_time_font_size; }
    int DrawingDpi(HDC dc) const {
        int dpi = m_app ? m_app->GetDPI(m_drawing_taskbar ? ITrafficMonitor::DPI_TASKBAR : ITrafficMonitor::DPI_MAIN_WND)
            : GetDeviceCaps(dc, LOGPIXELSY);
        return dpi > 0 ? dpi : 96;
    }
    bool ValueRightAligned() const { return m_drawing_taskbar && m_value_right_align; }
    std::wstring DisplayLabel(bool primary) const {
        ExclusiveSrwLock lock(m_label_lock);
        RefreshDisplayLabelsLocked();
        size_t index = primary ? 0 : 1;
        return m_drawing_taskbar ? m_taskbar_labels[index] : m_main_labels[index];
    }
    COLORREF LabelColor(bool dark) const { return ColorOrDefault(m_label_color, dark); }
    COLORREF ValueColor(bool dark) const { return ColorOrDefault(m_value_color, dark); }

    void OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* text) override
    {
        if (!text) return;
        // V1.85.1 and older hosts have no OnInitialize callback. Both old and
        // new hosts supply the actual settings directory through this API.
        if (index == EI_CONFIG_DIR) InitializeFromConfigDirectory(text);
        else if (index == EI_LABEL_TEXT_COLOR) m_label_color = wcstoul(text, nullptr, 10);
        else if (index == EI_VALUE_TEXT_COLOR) m_value_color = wcstoul(text, nullptr, 10);
        else if (index == EI_DRAW_TASKBAR_WND) m_drawing_taskbar = wcstol(text, nullptr, 10) != 0;
        else if (index == EI_TASKBAR_WND_VALUE_RIGHT_ALIGN) m_value_right_align = wcstol(text, nullptr, 10) != 0;
    }

    void OnInitialize(ITrafficMonitor* pApp) override
    {
        m_app = pApp;
        if (pApp && pApp->GetAPIVersion() >= 1)
            InitializeFromConfigDirectory(pApp->GetPluginConfigDir());
    }

    void GetWindow(bool primary, QuotaWindow& w, bool& data_valid, bool& stale)
    {
        CriticalLock lock(m_cs);
        w = primary ? m_data.primary : m_data.secondary;
        data_valid = m_data.valid;
        stale = m_data.stale;
    }

private:
    void InitializeFromConfigDirectory(const wchar_t* directory)
    {
        // New hosts send both callbacks. Initialize only once so a second
        // notification cannot reset live options or launch another query.
        if (m_initialized || !directory || !*directory) return;
        LoadConfig(JoinPath(directory, L"CodexUsage.ini"));
        {
            ExclusiveSrwLock lock(m_label_lock);
            m_host_config_path = JoinPath(directory, L"..\\config.ini");
            RefreshDisplayLabelsLocked();
        }
        if (m_sessions_dir.empty())
        {
            wchar_t buf[MAX_PATH]{};
            DWORD n = GetEnvironmentVariableW(L"CODEX_HOME", buf, MAX_PATH);
            if (n > 0 && n < MAX_PATH)
                m_sessions_dir = JoinPath(buf, L"sessions");
            else if ((n = GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH)) > 0 && n < MAX_PATH)
                m_sessions_dir = JoinPath(buf, L".codex\\sessions");
        }
        if (m_codex_exe.empty())
        {
            m_auto_detect_exe = true;
            m_codex_exe = FindCodexExe();
        }
        m_last_scan = GetTickCount64();
        m_item_5h.InitializeGraphHistory(m_last_scan, m_refresh_ms);
        m_item_7d.InitializeGraphHistory(m_last_scan, m_refresh_ms);
        m_initialized = true;
        KickRefresh();
    }
    void RefreshDisplayLabelsLocked() const {
        // The host persists labels when settings are applied, but API 7 does
        // not pass their text to custom-drawn items. Refresh this read-only
        // cache independently of the much slower quota polling interval.
        if (m_host_config_path.empty()) return;
        ULONGLONG now = GetTickCount64();
        if (now < m_next_label_check) return;
        m_next_label_check = now + 250;
        WIN32_FILE_ATTRIBUTE_DATA before{}, after{};
        if (!GetFileAttributesExW(m_host_config_path.c_str(), GetFileExInfoStandard, &before)) return;
        auto same_version = [](const WIN32_FILE_ATTRIBUTE_DATA& a, const WIN32_FILE_ATTRIBUTE_DATA& b) {
            return CompareFileTime(&a.ftLastWriteTime, &b.ftLastWriteTime) == 0 &&
                a.nFileSizeHigh == b.nFileSizeHigh && a.nFileSizeLow == b.nFileSizeLow;
        };
        if (m_has_label_version && same_version(before, m_label_version)) return;
        std::wstring taskbar[2], main_window[2];
        if (!ReadDisplayLabels(m_host_config_path, taskbar, main_window) ||
            !GetFileAttributesExW(m_host_config_path.c_str(), GetFileExInfoStandard, &after) ||
            !same_version(before, after)) return;
        for (size_t i = 0; i < 2; ++i) {
            m_taskbar_labels[i].swap(taskbar[i]);
            m_main_labels[i].swap(main_window[i]);
        }
        m_label_version = after;
        m_has_label_version = true;
    }

    static COLORREF ColorOrDefault(DWORD color, bool dark) {
        return color == CLR_INVALID ? (dark ? RGB(255, 255, 255) : RGB(0, 0, 0)) : color;
    }

    CCodexUsagePlugin()
    {
        InitializeCriticalSection(&m_cs);
        m_refresh_ms = 5 * 60 * 1000;
    }
    ~CCodexUsagePlugin()
    {
        DeleteCriticalSection(&m_cs);
    }
    CCodexUsagePlugin(const CCodexUsagePlugin&) = delete;
    CCodexUsagePlugin& operator=(const CCodexUsagePlugin&) = delete;

    void LoadConfig(const std::wstring& path)
    {
        m_config_path = path;
        m_show_remaining = GetPrivateProfileIntW(L"config", L"show_remaining", 1, path.c_str()) != 0;
        m_refresh_ms = ReadRefreshMilliseconds(path);
        m_allow_local_fallback = GetPrivateProfileIntW(L"config", L"allow_local_fallback", 0, path.c_str()) != 0;
        m_graph_enabled = GetPrivateProfileIntW(L"config", L"graph_enabled", 0, path.c_str()) != 0;
        m_graph_color = GetPrivateProfileIntW(L"config", L"graph_color", RGB(192, 192, 192), path.c_str()) & 0xffffff;
        m_graph_follow_theme = GetPrivateProfileIntW(L"config", L"graph_follow_theme", 0, path.c_str()) != 0;
        m_show_reset_time = GetPrivateProfileIntW(L"config", L"show_reset_time", 0, path.c_str()) != 0;
        int time_points = GetPrivateProfileIntW(L"config", L"time_font_size", 10, path.c_str());
        m_time_font_size = time_points > 0 ? time_points : 10;
        wchar_t graph_mode[32]{};
        GetPrivateProfileStringW(L"config", L"graph_mode", L"remaining", graph_mode, 32, path.c_str());
        m_graph_remaining = _wcsicmp(graph_mode, L"used") != 0;
        GetPrivateProfileStringW(L"config", L"graph_type", L"bar", graph_mode, 32, path.c_str());
        m_graph_scrolling = _wcsicmp(graph_mode, L"scroll") == 0;
        wchar_t buf[MAX_PATH]{};
        DWORD n = GetPrivateProfileStringW(L"config", L"sessions_dir", L"", buf, MAX_PATH, path.c_str());
        if (n > 0)
            m_sessions_dir = buf;
        n = GetPrivateProfileStringW(L"config", L"codex_path", L"", buf, MAX_PATH, path.c_str());
        if (n > 0)
            m_codex_exe = buf;
    }

    void KickRefresh()
    {
        if (m_fetching.exchange(true))
            return;
        // A worker owns a module reference, so FreeLibrary by the host cannot
        // unload its code while the query is running. Never wait in DllMain.
        HMODULE worker_module = nullptr;
        if (g_hModule && !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(&CCodexUsagePlugin::WorkerProc), &worker_module))
        {
            m_fetching = false;
            return;
        }
        HANDLE h = CreateThread(nullptr, 0, &CCodexUsagePlugin::WorkerProc, this, 0, nullptr);
        if (h != nullptr)
            CloseHandle(h);
        else
        {
            m_fetching = false;
            if (worker_module) FreeLibrary(worker_module);
        }
    }

    static DWORD WINAPI WorkerProc(LPVOID param)
    {
        auto* self = static_cast<CCodexUsagePlugin*>(param);
        HMODULE worker_module = g_hModule;
        try { self->FetchOnce(); }
        catch (...) { /* Never propagate allocation or parsing errors into the host. */ }
        self->m_fetching = false;
        if (worker_module) FreeLibraryAndExitThread(worker_module, 0);
        return 0;
    }

    static LRESULT CALLBACK OptionsProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void CreateOptionControls(HWND hWnd);
    void EnableGraphControls(HWND hWnd);
    void SaveConfig() const;

    void FetchOnce()
    {
        // Timestamp the request, not its paint or completion time. Network
        // latency must not move a successful query into a later history slot.
        const ULONGLONG query_started = GetTickCount64();
        UsageData data;
        bool ok = FetchFromAppServer(m_codex_exe, data);
        if (!ok && m_auto_detect_exe)
        {
            // Codex 桌面版更新后安装路径会变（含版本号），重新探测一次
            std::wstring fresh = FindCodexExe();
            if (!fresh.empty() && fresh != m_codex_exe)
            {
                m_codex_exe = fresh;
                ok = FetchFromAppServer(m_codex_exe, data);
            }
        }
        if (ok)
        {
            data.from_app_server = true;
        }
        {
            CriticalLock lock(m_cs);
            // Keep the last successful query, including its actual timestamp.
            if (ok) {
                m_item_5h.RecordGraphSample(query_started, data.primary);
                m_item_7d.RecordGraphSample(query_started, data.secondary);
                m_data = data;
            }
            else if (m_data.valid) m_data.stale = true;
        }
        if (!ok && m_allow_local_fallback)
        {
            UsageData local;
            if (FetchFromLocalFiles(m_sessions_dir, local))
            {
                CriticalLock lock(m_cs);
                if (!m_data.valid) m_data = local;
            }
        }
        CriticalLock lock(m_cs);
        BuildTooltipLocked();
    }

    void BuildTooltipLocked()
    {
        if (!m_data.valid)
        {
            m_tooltip = L"Codex 额度\r\n查询失败：请确认已安装 Codex、使用 ChatGPT 账号登录，且网络可用。\r\n可在 INI 中指定 codex_path；默认不读取本地会话。";
            return;
        }
        const wchar_t* source = m_data.from_app_server ? L"Codex app-server" : L"本地会话快照";
        wchar_t buf[512];
        std::wstring plan = m_data.plan_type.empty() ? L"" : L"（" + m_data.plan_type + L"）";
        auto window_text = [](const QuotaWindow& window) {
            if (!window.valid) return std::wstring(L"不可用");
            wchar_t text[160];
            swprintf_s(text, L"已用 %.0f%%，剩余 %.0f%%，重置于 %s", window.used_percent,
                100.0 - window.used_percent, FormatTime(window.resets_at, true).c_str());
            return std::wstring(text);
        };
        swprintf_s(buf, L"Codex 额度%s%s\r\n5小时窗口：%s\r\n7天窗口：%s\r\n数据来源：%s\r\n数据时间：%s",
            plan.c_str(), m_data.stale ? L"（历史数据）" : L"",
            window_text(m_data.primary).c_str(), window_text(m_data.secondary).c_str(),
            source, FormatTime(m_data.updated_at, true).c_str());
        m_tooltip = buf;
    }

    ITrafficMonitor* m_app = nullptr;
    bool m_initialized = false;
    CQuotaItem m_item_5h{ true };
    CQuotaItem m_item_7d{ false };
    UsageData m_data;
    std::wstring m_tooltip{ L"Codex 额度" };
    std::wstring m_sessions_dir;
    std::wstring m_codex_exe;
    std::wstring m_config_path;
    bool m_show_remaining = true;
    bool m_auto_detect_exe = false;
    bool m_allow_local_fallback = false;
    std::atomic<bool> m_graph_enabled{ false };
    std::atomic<bool> m_graph_remaining{ true };
    std::atomic<bool> m_graph_scrolling{ false };
    std::atomic<bool> m_graph_follow_theme{ false };
    std::atomic<COLORREF> m_graph_color{ RGB(192, 192, 192) };
    COLORREF m_option_graph_color = RGB(192, 192, 192);
    std::atomic<bool> m_show_reset_time{ false };
    std::atomic<int> m_time_font_size{ 10 };
    std::atomic<bool> m_drawing_taskbar{ true }, m_value_right_align{ false };
    std::wstring m_host_config_path;
    mutable SRWLOCK m_label_lock = SRWLOCK_INIT;
    mutable ULONGLONG m_next_label_check = 0;
    mutable WIN32_FILE_ATTRIBUTE_DATA m_label_version{};
    mutable bool m_has_label_version = false;
    mutable std::wstring m_taskbar_labels[2]{DefaultDisplayLabel(true), DefaultDisplayLabel(false)};
    mutable std::wstring m_main_labels[2]{DefaultDisplayLabel(true), DefaultDisplayLabel(false)};
    std::atomic<DWORD> m_label_color{ CLR_INVALID }, m_value_color{ CLR_INVALID };
    OptionReturn m_option_result = OR_OPTION_UNCHANGED;
    int m_dlg_dpi = 96;
    DWORD m_refresh_ms = 5 * 60 * 1000;
    ULONGLONG m_last_scan = 0;
    std::atomic<bool> m_fetching{ false };
    CRITICAL_SECTION m_cs;

public:
    bool IsFetching() const { return m_fetching; }
};

void CCodexUsagePlugin::SaveConfig() const
{
    if (m_config_path.empty())
        return;
    WritePrivateProfileStringW(L"config", L"graph_enabled", m_graph_enabled ? L"1" : L"0", m_config_path.c_str());
    WritePrivateProfileStringW(L"config", L"graph_mode", m_graph_remaining ? L"remaining" : L"used", m_config_path.c_str());
    WritePrivateProfileStringW(L"config", L"graph_type", m_graph_scrolling ? L"scroll" : L"bar", m_config_path.c_str());
    WritePrivateProfileStringW(L"config", L"graph_follow_theme", m_graph_follow_theme ? L"1" : L"0", m_config_path.c_str());
    WritePrivateProfileStringW(L"config", L"graph_color", std::to_wstring(m_graph_color.load()).c_str(), m_config_path.c_str());
    WritePrivateProfileStringW(L"config", L"show_reset_time", m_show_reset_time ? L"1" : L"0", m_config_path.c_str());
    WritePrivateProfileStringW(L"config", L"time_font_size", std::to_wstring(m_time_font_size.load()).c_str(), m_config_path.c_str());
}

void CCodexUsagePlugin::CreateOptionControls(HWND hWnd)
{
    auto S = [this](int v) { return v * m_dlg_dpi / 96; };
    HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    auto mk = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id) {
        HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
            S(x), S(y), S(w), S(h), hWnd, (HMENU)(INT_PTR)id, g_hModule, nullptr);
        SendMessageW(c, WM_SETFONT, (WPARAM)font, FALSE);
        return c;
    };

    HWND reset_time = mk(L"BUTTON", L"重置时间显示", BS_AUTOCHECKBOX | WS_GROUP | WS_TABSTOP,
        16, 16, 368, 22, IDC_SHOW_RESET_TIME);
    SendMessageW(reset_time, BM_SETCHECK, m_show_reset_time ? BST_CHECKED : BST_UNCHECKED, 0);
    mk(L"STATIC", L"字体大小:", SS_LEFT | SS_CENTERIMAGE, 32, 48, 72, 22, -1);
    HWND time_size = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"10",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL,
        S(104), S(48), S(56), S(22), hWnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TIME_FONT_SIZE)), g_hModule, nullptr);
    SendMessageW(time_size, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
    HWND time_spin = mk(UPDOWN_CLASSW, L"", UDS_ALIGNRIGHT | UDS_SETBUDDYINT | UDS_ARROWKEYS | UDS_NOTHOUSANDS,
        0, 0, 0, 0, IDC_TIME_FONT_SPIN);
    SendMessageW(time_spin, UDM_SETBUDDY, reinterpret_cast<WPARAM>(time_size), 0);
    SendMessageW(time_spin, UDM_SETRANGE32, 1, MAXLONG);
    SendMessageW(time_spin, UDM_SETPOS32, 0, m_time_font_size);
    EnableWindow(time_size, m_show_reset_time);
    EnableWindow(time_spin, m_show_reset_time);

    HWND chk = mk(L"BUTTON", L"启用资源占用图", BS_AUTOCHECKBOX | WS_GROUP | WS_TABSTOP,
        16, 96, 368, 22, IDC_GRAPH_ENABLE);
    if (m_graph_enabled) SendMessageW(chk, BM_SETCHECK, BST_CHECKED, 0);
    mk(L"STATIC", L"占用图颜色：", SS_LEFT | SS_CENTERIMAGE, 32, 128, 74, 22, IDC_GRAPH_COLOR_LABEL);
    mk(L"BUTTON", L"占用图颜色", BS_OWNERDRAW | WS_TABSTOP, 108, 128, 44, 22, IDC_GRAPH_COLOR);
    HWND theme = mk(L"BUTTON", L"跟随 Windows 主题颜色", BS_AUTOCHECKBOX | WS_GROUP | WS_TABSTOP,
        164, 128, 220, 22, IDC_GRAPH_THEME);
    SendMessageW(theme, BM_SETCHECK, m_graph_follow_theme ? BST_CHECKED : BST_UNCHECKED, 0);
    mk(L"STATIC", L"占用图类型：", SS_LEFT | SS_CENTERIMAGE, 32, 160, 74, 22, IDC_GRAPH_TYPE_LABEL);
    HWND bar = mk(L"BUTTON", L"柱状图", BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP,
        108, 160, 76, 22, IDC_GRAPH_BAR);
    HWND scroll = mk(L"BUTTON", L"横向滚动图", BS_AUTORADIOBUTTON | WS_TABSTOP,
        196, 160, 132, 22, IDC_GRAPH_SCROLL);
    SendMessageW(m_graph_scrolling ? scroll : bar, BM_SETCHECK, BST_CHECKED, 0);
    mk(L"STATIC", L"数据逻辑：", SS_LEFT | SS_CENTERIMAGE, 32, 192, 74, 22, IDC_GRAPH_LOGIC_LABEL);
    HWND r_remaining = mk(L"BUTTON", L"剩余额度", BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP,
        108, 192, 220, 22, IDC_GRAPH_REMAINING);
    HWND r_used = mk(L"BUTTON", L"已用额度", BS_AUTORADIOBUTTON,
        108, 220, 220, 22, IDC_GRAPH_USED);
    SendMessageW(m_graph_remaining ? r_remaining : r_used, BM_SETCHECK, BST_CHECKED, 0);
    EnableGraphControls(hWnd);

    mk(L"BUTTON", L"确定", BS_DEFPUSHBUTTON,
        (kOptionsClientW - 75) / 2, kOptionsClientH - 14 - 25, 75, 25, IDOK);
    SendMessageW(hWnd, DM_SETDEFID, IDOK, 0);
}

void CCodexUsagePlugin::EnableGraphControls(HWND window) {
    const BOOL enabled = SendDlgItemMessageW(window, IDC_GRAPH_ENABLE, BM_GETCHECK, 0, 0) == BST_CHECKED;
    for (int id : {IDC_GRAPH_THEME, IDC_GRAPH_BAR, IDC_GRAPH_SCROLL, IDC_GRAPH_REMAINING, IDC_GRAPH_USED,
        IDC_GRAPH_COLOR_LABEL, IDC_GRAPH_TYPE_LABEL, IDC_GRAPH_LOGIC_LABEL})
        EnableWindow(GetDlgItem(window, id), enabled);
    const bool follow = SendDlgItemMessageW(window, IDC_GRAPH_THEME, BM_GETCHECK, 0, 0) == BST_CHECKED;
    EnableWindow(GetDlgItem(window, IDC_GRAPH_COLOR), enabled && !follow);
    InvalidateRect(GetDlgItem(window, IDC_GRAPH_COLOR), nullptr, TRUE);
}

LRESULT CALLBACK CCodexUsagePlugin::OptionsProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    CCodexUsagePlugin* self = reinterpret_cast<CCodexUsagePlugin*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    switch (msg)
    {
    case WM_CREATE:
    {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<CCodexUsagePlugin*>(cs->lpCreateParams);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->CreateOptionControls(hWnd);
        return 0;
    }
    case WM_COMMAND:
        if (self != nullptr)
        {
            int id = LOWORD(wParam);
            if (id == IDOK)
            {
                BOOL valid_size = FALSE;
                int time_points = static_cast<int>(GetDlgItemInt(hWnd, IDC_TIME_FONT_SIZE, &valid_size, TRUE));
                if (!valid_size || time_points <= 0) time_points = self->m_time_font_size;
                self->m_graph_enabled = SendMessageW(GetDlgItem(hWnd, IDC_GRAPH_ENABLE), BM_GETCHECK, 0, 0) == BST_CHECKED;
                self->m_graph_remaining = SendMessageW(GetDlgItem(hWnd, IDC_GRAPH_REMAINING), BM_GETCHECK, 0, 0) == BST_CHECKED;
                self->m_graph_scrolling = SendDlgItemMessageW(hWnd, IDC_GRAPH_SCROLL, BM_GETCHECK, 0, 0) == BST_CHECKED;
                self->m_graph_follow_theme = SendDlgItemMessageW(hWnd, IDC_GRAPH_THEME, BM_GETCHECK, 0, 0) == BST_CHECKED;
                self->m_graph_color = self->m_option_graph_color;
                self->m_show_reset_time = SendMessageW(GetDlgItem(hWnd, IDC_SHOW_RESET_TIME), BM_GETCHECK, 0, 0) == BST_CHECKED;
                self->m_time_font_size = static_cast<int>(time_points);
                self->SaveConfig();
                self->m_option_result = ITMPlugin::OR_OPTION_CHANGED;
                DestroyWindow(hWnd);
            }
            else if (id == IDCANCEL)
            {
                DestroyWindow(hWnd);
            }
            else if (id == IDC_GRAPH_ENABLE)
            {
                self->EnableGraphControls(hWnd);
            }
            else if (id == IDC_GRAPH_THEME)
            {
                self->EnableGraphControls(hWnd);
            }
            else if (id == IDC_GRAPH_COLOR && HIWORD(wParam) == BN_CLICKED)
            {
                COLORREF selected = self->m_option_graph_color;
                if (ShowGraphColorDialog(hWnd, selected)) self->m_option_graph_color = selected;
                InvalidateRect(GetDlgItem(hWnd, IDC_GRAPH_COLOR), nullptr, TRUE);
            }
            else if (id == IDC_SHOW_RESET_TIME)
            {
                BOOL enabled = SendMessageW(GetDlgItem(hWnd, IDC_SHOW_RESET_TIME), BM_GETCHECK, 0, 0) == BST_CHECKED;
                EnableWindow(GetDlgItem(hWnd, IDC_TIME_FONT_SIZE), enabled);
                EnableWindow(GetDlgItem(hWnd, IDC_TIME_FONT_SPIN), enabled);
            }
        }
        return 0;
    case WM_DRAWITEM:
        if (self) {
            const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
            if (item && item->CtlID == IDC_GRAPH_COLOR) {
                const bool follow = SendDlgItemMessageW(hWnd, IDC_GRAPH_THEME, BM_GETCHECK, 0, 0) == BST_CHECKED;
                const COLORREF color = follow ? self->ThemeColor() : self->m_option_graph_color;
                const int saved = SaveDC(item->hDC);
                SetDCBrushColor(item->hDC, color);
                FillRect(item->hDC, &item->rcItem, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
                RECT edge = item->rcItem;
                DrawEdge(item->hDC, &edge, EDGE_SUNKEN, BF_RECT);
                if (item->itemState & ODS_FOCUS) { InflateRect(&edge, -3, -3); DrawFocusRect(item->hDC, &edge); }
                if (saved) RestoreDC(item->hDC, saved);
                return TRUE;
            }
        }
        break;
    case WM_THEMECHANGED:
    case WM_DWMCOLORIZATIONCOLORCHANGED:
        if (self) InvalidateRect(GetDlgItem(hWnd, IDC_GRAPH_COLOR), nullptr, TRUE);
        break;
    case WM_CLOSE:
        DestroyWindow(hWnd);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

ITMPlugin::OptionReturn CCodexUsagePlugin::ShowOptionsDialog(void* hParent)
{
    HWND parent = static_cast<HWND>(hParent);
    m_option_result = OR_OPTION_UNCHANGED;
    m_option_graph_color = m_graph_color;
    m_dlg_dpi = (m_app != nullptr) ? m_app->GetDPI(ITrafficMonitor::DPI_MAIN_WND) : 96;
    using GetWindowDpi = UINT(WINAPI*)(HWND);
    auto get_window_dpi = reinterpret_cast<GetWindowDpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (parent && get_window_dpi) m_dlg_dpi = static_cast<int>(get_window_dpi(parent));
    if (m_dlg_dpi <= 0)
        m_dlg_dpi = 96;
    INITCOMMONCONTROLSEX common_controls{sizeof(common_controls), ICC_UPDOWN_CLASS};
    InitCommonControlsEx(&common_controls);

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = &CCodexUsagePlugin::OptionsProc;
    wc.hInstance = g_hModule;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)); // IDC_ARROW
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = L"CodexUsageOptionsWnd";
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return OR_OPTION_NOT_PROVIDED;

    auto S = [this](int v) { return v * m_dlg_dpi / 96; };
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    const DWORD ex_style = WS_EX_CONTROLPARENT | WS_EX_DLGMODALFRAME;
    // 客户区尺寸换算为含标题栏的外框尺寸，否则底部按钮会被裁掉
    RECT rc{ 0, 0, S(kOptionsClientW), S(kOptionsClientH) };
    AdjustWindowRectEx(&rc, style, FALSE, ex_style);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    MONITORINFO monitor{sizeof(monitor)};
    HMONITOR display = nullptr;
    if (parent) display = MonitorFromWindow(parent, MONITOR_DEFAULTTONEAREST);
    else {
        POINT cursor{};
        GetCursorPos(&cursor);
        display = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    }
    RECT work{};
    if (GetMonitorInfoW(display, &monitor)) work = monitor.rcWork;
    else SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    // A taskbar parent is very short and near the screen edge. Center on its
    // monitor's usable desktop instead, including monitors with negative coordinates.
    int x = work.left + ((work.right - work.left) - w) / 2;
    int y = work.top + ((work.bottom - work.top) - h) / 2;
    x = (std::max)(work.left, (std::min)(static_cast<LONG>(x), work.right - w));
    y = (std::max)(work.top, (std::min)(static_cast<LONG>(y), work.bottom - h));

    HWND hWnd = CreateWindowExW(ex_style,
        L"CodexUsageOptionsWnd", L"Codex 额度插件设置",
        style, x, y, w, h,
        parent, nullptr, g_hModule, this);
    if (hWnd == nullptr) {
        UnregisterClassW(wc.lpszClassName, g_hModule);
        return OR_OPTION_NOT_PROVIDED;
    }

    if (parent != nullptr)
        EnableWindow(parent, FALSE);
    ShowWindow(hWnd, SW_SHOW);
    SetForegroundWindow(hWnd);

    MSG msg{};
    int message_result = 1;
    while (IsWindow(hWnd) && (message_result = static_cast<int>(GetMessageW(&msg, nullptr, 0, 0))) > 0)
    {
        if (!IsDialogMessageW(hWnd, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (message_result == 0) {
        if (IsWindow(hWnd)) DestroyWindow(hWnd);
        PostQuitMessage(static_cast<int>(msg.wParam));
    }

    if (parent != nullptr)
    {
        EnableWindow(parent, TRUE);
        SetForegroundWindow(parent);
    }
    // A DLL's window classes otherwise survive FreeLibrary with a dangling
    // window-procedure address. Keep this class scoped to the modal window.
    UnregisterClassW(wc.lpszClassName, g_hModule);
    return m_option_result;
}

const wchar_t* CQuotaItem::GetItemValueText() const
{
    static thread_local std::wstring text[2];
    CCodexUsagePlugin& plugin = CCodexUsagePlugin::Instance();
    QuotaWindow w;
    bool data_valid = false, stale = false;
    plugin.GetWindow(m_is_primary, w, data_valid, stale);
    std::wstring& slot = text[m_is_primary ? 0 : 1];
    if (!data_valid || !w.valid)
    {
        slot = L"--";
        return slot.c_str();
    }
    double value = plugin.ShowRemaining() ? 100.0 - w.used_percent : w.used_percent;
    if (value < 0) value = 0;
    if (value > 100) value = 100;
    wchar_t buf[16];
    swprintf_s(buf, L"%.0f%%", value);
    slot = buf;
    return slot.c_str();
}

int CQuotaItem::IsDrawResourceUsageGraph() const
{
    // Opt out of the host's graph pass. DrawItem owns the graph even when
    // TrafficMonitor's global graph switch is enabled, avoiding double fill.
    return 0;
}

bool CQuotaItem::IsCustomDraw() const
{
    const auto& plugin = CCodexUsagePlugin::Instance();
    return plugin.ShowResetTime() || plugin.GraphEnabled();
}

int CQuotaItem::GetItemWidthEx(void* hDC) const
{
    HDC dc = static_cast<HDC>(hDC);
    if (!dc) return 0;
    auto& plugin = CCodexUsagePlugin::Instance();
    LOGFONTW description{};
    if (GetObjectW(GetCurrentObject(dc, OBJ_FONT), sizeof(description), &description)) {
        AcquireSRWLockExclusive(&m_font_lock);
        m_normal_description = description;
        m_has_normal_description = true;
        ReleaseSRWLockExclusive(&m_font_lock);
    }
    DrawingFonts fonts(dc, plugin.TimeFontSize(), plugin.DrawingDpi(dc));
    std::wstring label = plugin.DisplayLabel(m_is_primary);
    if (!label.empty()) label += L" ";
    const bool show_time = plugin.ShowResetTime();
    std::wstring normal = label + GetItemValueSampleText();
    if (show_time) normal += L" ";
    int width = fonts.Width(normal);
    if (show_time) {
        fonts.Small();
        width += fonts.Width(m_is_primary ? L"88:88" : L"88-88 88:88");
    }
    return width + 4;
}

void CQuotaItem::DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode)
{
    HDC dc = static_cast<HDC>(hDC);
    if (!dc || w <= 0 || h <= 0) return;
    auto& plugin = CCodexUsagePlugin::Instance();
    QuotaWindow window;
    bool valid = false, stale = false;
    plugin.GetWindow(m_is_primary, window, valid, stale);
    std::wstring label = plugin.DisplayLabel(m_is_primary);
    if (!label.empty()) label += L" ";
    const std::wstring value = GetItemValueText();
    const std::wstring deadline = valid && window.valid ? FormatTime(window.resets_at, !m_is_primary) : L"--";
    LOGFONTW normal_description{};
    AcquireSRWLockShared(&m_font_lock);
    bool has_description = m_has_normal_description;
    normal_description = m_normal_description;
    ReleaseSRWLockShared(&m_font_lock);
    int saved = SaveDC(dc);
    if (!saved) return;
    IntersectClipRect(dc, x, y, x + w, y + h);
    SetBkMode(dc, TRANSPARENT);
    SetTextAlign(dc, TA_LEFT | TA_TOP | TA_NOUPDATECP);
    if (plugin.GraphEnabled() && plugin.DrawingTaskbar() && valid && window.valid) {
        const ULONGLONG now = GetTickCount64();
        const double percent = plugin.GraphRemaining() ? 100.0 - window.used_percent : window.used_percent;
        const int filled = static_cast<int>(w * (std::max)(0.0, (std::min)(percent, 100.0)) / 100.0);
        SetDCBrushColor(dc, plugin.GraphColor());
        if (plugin.GraphScrolling()) {
            RECT area{x, y, x + w, y + h};
            m_graph_history.Draw(dc, area, now, plugin.GraphRemaining());
        }
        else if (filled > 0) {
            RECT fill{x, y, x + filled, y + h};
            FillRect(dc, &fill, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        }
    }
    {
        // The host measures with its configured font. Reuse that same font
        // even when its GDI/D2D paint surface starts with a different default.
        DrawingFonts fonts(dc, plugin.TimeFontSize(), plugin.DrawingDpi(dc), has_description ? &normal_description : nullptr);
        int baseline = y + (h - fonts.normal_metrics.tmHeight) / 2 + fonts.normal_metrics.tmAscent;
        SetTextColor(dc, plugin.LabelColor(dark_mode));
        TextOutW(dc, x, baseline - fonts.normal_metrics.tmAscent, label.c_str(), static_cast<int>(label.size()));
        x += fonts.Width(label);
        SetTextColor(dc, plugin.ValueColor(dark_mode));
        int original_value_width = fonts.Width(GetItemValueSampleText());
        int value_x = plugin.ValueRightAligned() ? x + (std::max)(0, original_value_width - fonts.Width(value)) : x;
        TextOutW(dc, value_x, baseline - fonts.normal_metrics.tmAscent, value.c_str(), static_cast<int>(value.size()));
        // Append immediately after the actual percentage rather than after
        // a reserved maximum-width column, without changing the prefix text.
        if (plugin.ShowResetTime()) {
            x = value_x + fonts.Width(value) + fonts.Width(L" ");
            fonts.Small();
            int time_y = fonts.small_metrics.tmHeight <= fonts.normal_metrics.tmHeight
                ? baseline - fonts.small_metrics.tmAscent : y + (h - fonts.small_metrics.tmHeight) / 2;
            TextOutW(dc, x, time_y, deadline.c_str(), static_cast<int>(deadline.size()));
        }
    }
    RestoreDC(dc, saved);
}

float CQuotaItem::GetResourceUsageGraphValue() const
{
    CCodexUsagePlugin& plugin = CCodexUsagePlugin::Instance();
    if (!plugin.GraphEnabled())
        return 0.0f;
    QuotaWindow w;
    bool data_valid = false, stale = false;
    plugin.GetWindow(m_is_primary, w, data_valid, stale);
    if (!data_valid || !w.valid)
        return 0.0f;
    double value = plugin.GraphRemaining() ? 100.0 - w.used_percent : w.used_percent;
    if (value < 0) value = 0;
    if (value > 100) value = 100;
    return (float)(value / 100.0);
}


extern "C" __declspec(dllexport) ITMPlugin* TMPluginGetInstance()
{
    return &CCodexUsagePlugin::Instance();
}

void InitializePluginModule(HMODULE module) { g_hModule = module; }

#ifndef CODEX_USE_MFC
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_hModule = hModule;
    }
    return TRUE;
}
#endif

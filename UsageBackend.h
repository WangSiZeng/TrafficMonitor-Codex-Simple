#pragma once
#include <windows.h>
#include <string>

namespace codex_usage {

inline constexpr const wchar_t* DefaultDisplayLabel(bool primary) noexcept {
    return primary ? L"Codex 5h额度:" : L"Codex 7d额度:";
}

struct QuotaWindow {
    bool valid = false;
    double used_percent = 0;
    long long resets_at = 0;
};

struct UsageData {
    bool valid = false;
    bool from_app_server = false;
    bool stale = false;
    QuotaWindow primary;
    QuotaWindow secondary;
    std::wstring plan_type;
    long long updated_at = 0;
};

std::wstring JoinPath(const std::wstring& dir, const std::wstring& name);
DWORD ReadRefreshMilliseconds(const std::wstring& path);
bool ReadDisplayPreference(const std::wstring& path, const wchar_t* section,
    const wchar_t* key, std::wstring& value);
bool ReadDisplayLabels(const std::wstring& path, std::wstring (&taskbar)[2], std::wstring (&main_window)[2]);
std::wstring FindCodexExe();
std::wstring ResolveExecutable(const std::wstring& path);
bool ParseRpcReply(const std::string& text, int id, UsageData* data);
bool ParseRolloutLine(const std::string& text, UsageData& data);
bool ReadTailAndParse(const std::wstring& path, UsageData& data);
bool FetchFromLocalFiles(const std::wstring& directory, UsageData& data);
bool FetchFromAppServer(const std::wstring& executable, UsageData& data);

} // namespace codex_usage

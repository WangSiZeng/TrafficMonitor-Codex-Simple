#include "UsageBackend.h"
#include "third_party/yyjson/yyjson.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <memory>
#include <sstream>
#include <vector>

#pragma comment(lib, "advapi32.lib")

namespace codex_usage {
namespace {
constexpr size_t kMaxLine = 1024 * 1024;
constexpr size_t kTailSize = 256 * 1024;
using JsonDocument = std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)>;

class Handle {
public:
    explicit Handle(HANDLE value = nullptr) : value_(value) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE get() const { return value_; }
    void reset(HANDLE value = nullptr) {
        if (value_ && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_);
        value_ = value;
    }
private:
    HANDLE value_;
};

yyjson_val* Field(yyjson_val* value, const char* camel, const char* snake) {
    yyjson_val* result = yyjson_obj_get(value, camel);
    return result ? result : yyjson_obj_get(value, snake);
}

bool StringEquals(yyjson_val* value, const char* text) {
    return yyjson_is_str(value) && yyjson_equals_str(value, text);
}

std::wstring Environment(const wchar_t* key) {
    DWORD length = GetEnvironmentVariableW(key, nullptr, 0);
    if (!length || length > 32768) return {};
    std::wstring value(length, L'\0');
    DWORD copied = GetEnvironmentVariableW(key, &value[0], length);
    if (!copied || copied >= length) return {};
    value.resize(copied);
    return value;
}

bool AbsolutePath(const std::wstring& path) {
    return (path.size() > 2 && path[1] == L':' &&
        (path[2] == L'\\' || path[2] == L'/')) ||
        (path.size() > 2 && path[0] == L'\\' && path[1] == L'\\');
}

std::wstring ExistingExecutable(const std::wstring& path) {
    if (!AbsolutePath(path) || path.find(L'"') != std::wstring::npos ||
        path.find(L'\n') != std::wstring::npos || path.find(L'\r') != std::wstring::npos ||
        path.size() < 4 || _wcsicmp(path.c_str() + path.size() - 4, L".exe") != 0)
        return {};
    DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || attributes & FILE_ATTRIBUTE_DIRECTORY) return {};
    std::vector<wchar_t> full(32768);
    DWORD count = GetFullPathNameW(path.c_str(), static_cast<DWORD>(full.size()), full.data(), nullptr);
    return count && count < full.size() ? std::wstring(full.data(), count) : std::wstring{};
}

bool ParseWindow(yyjson_val* object, QuotaWindow& window, int& minutes) {
    if (!yyjson_is_obj(object)) return false;
    yyjson_val* used = Field(object, "usedPercent", "used_percent");
    yyjson_val* resets = Field(object, "resetsAt", "resets_at");
    yyjson_val* duration = Field(object, "windowDurationMins", "window_minutes");
    if (!yyjson_is_num(used) || !yyjson_is_int(resets) || !yyjson_is_int(duration)) return false;
    const double percent = yyjson_get_num(used);
    const long long timestamp = yyjson_get_sint(resets);
    const long long window_minutes = yyjson_get_sint(duration);
    if (!std::isfinite(percent) || percent < 0 || percent > 100 ||
        timestamp <= 0 || timestamp > 32535215999LL || window_minutes <= 0 || window_minutes > 525600)
        return false;
    window = {true, percent, timestamp};
    minutes = static_cast<int>(window_minutes);
    return true;
}

bool ParseSnapshot(yyjson_val* object, UsageData& data) {
    if (!yyjson_is_obj(object)) return false;
    yyjson_val* limit_id = Field(object, "limitId", "limit_id");
    if (limit_id && !yyjson_is_null(limit_id) && !StringEquals(limit_id, "codex")) return false;
    UsageData parsed;
    for (const char* name : {"primary", "secondary"}) {
        yyjson_val* value = yyjson_obj_get(object, name);
        if (!value || yyjson_is_null(value)) continue;
        QuotaWindow window;
        int duration = 0;
        if (!ParseWindow(value, window, duration)) return false;
        if (duration == 300) parsed.primary = window;
        else if (duration == 10080) parsed.secondary = window;
    }
    if (!parsed.primary.valid && !parsed.secondary.valid) return false;
    yyjson_val* plan = Field(object, "planType", "plan_type");
    if (yyjson_is_str(plan) && yyjson_get_len(plan) <= 64) {
        const char* text = yyjson_get_str(plan);
        // Plan identifiers are ASCII; do not surface arbitrary control characters.
        for (size_t i = 0; i < yyjson_get_len(plan); ++i) {
            unsigned char ch = static_cast<unsigned char>(text[i]);
            if (ch < 32 || ch > 126) { parsed.plan_type.clear(); break; }
            parsed.plan_type.push_back(ch);
        }
    }
    parsed.valid = true;
    data = parsed;
    return true;
}

long long ParseTimestamp(yyjson_val* value) {
    if (!yyjson_is_str(value)) return 0;
    std::string text(yyjson_get_str(value), yyjson_get_len(value));
    if (text.size() < 20 || text.back() != 'Z') return 0;
    int year, month, day, hour, minute, second;
    if (sscanf_s(text.c_str(), "%d-%d-%dT%d:%d:%d", &year, &month, &day,
        &hour, &minute, &second) != 6 || year < 1970 || year > 2999 || month < 1 ||
        month > 12 || day < 1 || day > 31 || hour < 0 || hour > 23 || minute < 0 ||
        minute > 59 || second < 0 || second > 59) return 0;
    tm utc{};
    utc.tm_year = year - 1900; utc.tm_mon = month - 1; utc.tm_mday = day;
    utc.tm_hour = hour; utc.tm_min = minute; utc.tm_sec = second;
    return _mkgmtime64(&utc);
}

struct FileEntry { std::wstring path; FILETIME write_time; };
void WalkSessions(const std::wstring& directory, int depth, size_t& visited,
    std::vector<FileEntry>& newest) {
    if (depth > 4 || visited >= 20000) return;
    WIN32_FIND_DATAW info{};
    HANDLE search = FindFirstFileW(JoinPath(directory, L"*").c_str(), &info);
    if (search == INVALID_HANDLE_VALUE) return;
    do {
        if (++visited > 20000) break;
        if (info.cFileName[0] == L'.' || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
        std::wstring path = JoinPath(directory, info.cFileName);
        if (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) WalkSessions(path, depth + 1, visited, newest);
        else if (wcsncmp(info.cFileName, L"rollout-", 8) == 0) {
            newest.push_back({path, info.ftLastWriteTime});
            std::sort(newest.begin(), newest.end(), [](const FileEntry& a, const FileEntry& b) {
                return CompareFileTime(&a.write_time, &b.write_time) > 0;
            });
            if (newest.size() > 3) newest.pop_back();
        }
    } while (FindNextFileW(search, &info));
    FindClose(search);
}

class PipeReader {
public:
    explicit PipeReader(HANDLE pipe) : pipe_(pipe) {}
    bool NextLine(std::string& line, ULONGLONG deadline) {
        while (GetTickCount64() < deadline) {
            size_t newline = buffer_.find('\n');
            if (newline != std::string::npos) {
                line = buffer_.substr(0, newline);
                buffer_.erase(0, newline + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                return true;
            }
            DWORD available = 0;
            if (!PeekNamedPipe(pipe_, nullptr, 0, nullptr, &available, nullptr)) return false;
            if (!available) { Sleep(20); continue; }
            char chunk[4096]; DWORD read = 0;
            if (!ReadFile(pipe_, chunk, (std::min)(available, static_cast<DWORD>(sizeof(chunk))), &read, nullptr) || !read)
                return false;
            buffer_.append(chunk, read);
            if (buffer_.size() > kMaxLine) return false;
        }
        return false;
    }
private:
    HANDLE pipe_;
    std::string buffer_;
};

bool WriteMessage(HANDLE pipe, const char* text) {
    DWORD written = 0;
    DWORD size = static_cast<DWORD>(strlen(text));
    return WriteFile(pipe, text, size, &written, nullptr) && written == size;
}

class ProcessCleanup {
public:
    explicit ProcessCleanup(HANDLE process) : process_(process) {}
    ~ProcessCleanup() {
        if (WaitForSingleObject(process_, 2000) == WAIT_TIMEOUT) {
            TerminateProcess(process_, 1);
            WaitForSingleObject(process_, 2000);
        }
    }
private:
    HANDLE process_;
};
} // namespace

std::wstring JoinPath(const std::wstring& directory, const std::wstring& name) {
    return directory.empty() || directory.back() == L'\\' || directory.back() == L'/'
        ? directory + name : directory + L'\\' + name;
}

DWORD ReadRefreshMilliseconds(const std::wstring& path) {
    wchar_t buffer[64]{};
    GetPrivateProfileStringW(L"config", L"refresh_seconds", L"300", buffer, 64, path.c_str());
    wchar_t* end = nullptr;
    long long seconds = _wcstoi64(buffer, &end, 10);
    while (end && (*end == L' ' || *end == L'\t')) ++end;
    if (end == buffer || !end || *end != L'\0') seconds = 300;
    seconds = (std::max)(30LL, (std::min)(seconds, 86400LL));
    return static_cast<DWORD>(seconds * 1000);
}

namespace {
bool ReadPreferenceText(const std::wstring& path, std::wstring& text) {
    // TrafficMonitor writes UTF-8 (including a BOM). The Windows profile APIs
    // interpret that as ANSI and corrupt non-ASCII custom labels.
    Handle file(CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (file.get() == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER length{};
    if (!GetFileSizeEx(file.get(), &length) || length.QuadPart <= 0 || length.QuadPart > 1024 * 1024)
        return false;
    std::string bytes(static_cast<size_t>(length.QuadPart), '\0');
    DWORD read = 0;
    if (!ReadFile(file.get(), &bytes[0], static_cast<DWORD>(bytes.size()), &read, nullptr) || read != bytes.size())
        return false;
    text.clear();
    if (bytes.size() >= 2 &&
        ((static_cast<unsigned char>(bytes[0]) == 0xff && static_cast<unsigned char>(bytes[1]) == 0xfe) ||
         (static_cast<unsigned char>(bytes[0]) == 0xfe && static_cast<unsigned char>(bytes[1]) == 0xff))) {
        bool little_endian = static_cast<unsigned char>(bytes[0]) == 0xff;
        if (bytes.size() % 2) return false;
        for (size_t i = 2; i + 1 < bytes.size(); i += 2) {
            unsigned char a = static_cast<unsigned char>(bytes[i]);
            unsigned char b = static_cast<unsigned char>(bytes[i + 1]);
            text.push_back(static_cast<wchar_t>(little_endian ? a | (b << 8) : (a << 8) | b));
        }
    } else {
        size_t start = bytes.compare(0, 3, "\xef\xbb\xbf") == 0 ? 3 : 0;
        int count = static_cast<int>(bytes.size() - start);
        UINT codepage = CP_UTF8;
        DWORD flags = MB_ERR_INVALID_CHARS;
        int required = MultiByteToWideChar(codepage, flags, bytes.data() + start, count, nullptr, 0);
        if (!required) {
            codepage = CP_ACP; flags = 0;
            required = MultiByteToWideChar(codepage, flags, bytes.data() + start, count, nullptr, 0);
        }
        if (!required) return false;
        text.resize(required);
        if (!MultiByteToWideChar(codepage, flags, bytes.data() + start, count, &text[0], required)) return false;
    }
    return true;
}

bool ParsePreference(const std::wstring& text, const wchar_t* section, const wchar_t* key, std::wstring& value) {
    auto trim = [](const std::wstring& input) {
        size_t first = input.find_first_not_of(L" \t\r\n");
        return first == std::wstring::npos ? std::wstring{} :
            input.substr(first, input.find_last_not_of(L" \t\r\n") - first + 1);
    };
    std::wistringstream input(text);
    std::wstring line;
    bool active_section = false;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line[0] == L';' || line[0] == L'#') continue;
        if (line.front() == L'[' && line.back() == L']') {
            active_section = _wcsicmp(trim(line.substr(1, line.size() - 2)).c_str(), section) == 0;
            continue;
        }
        if (!active_section) continue;
        size_t equals = line.find(L'=');
        if (equals == std::wstring::npos || _wcsicmp(trim(line.substr(0, equals)).c_str(), key) != 0) continue;
        value = trim(line.substr(equals + 1));
        // Match TrafficMonitor's CIniHelper::GetString: quotes protect spaces,
        // and older configurations also use '$' as a boundary marker.
        if (!value.empty() && (value.front() == L'"' || value.front() == L'$')) value.erase(0, 1);
        if (!value.empty() && (value.back() == L'"' || value.back() == L'$')) value.pop_back();
        return true;
    }
    return false;
}
} // namespace

bool ReadDisplayPreference(const std::wstring& path, const wchar_t* section,
    const wchar_t* key, std::wstring& value) {
    std::wstring text;
    return ReadPreferenceText(path, text) && ParsePreference(text, section, key, value);
}

bool ReadDisplayLabels(const std::wstring& path, std::wstring (&taskbar)[2], std::wstring (&main_window)[2]) {
    std::wstring text;
    if (!ReadPreferenceText(path, text)) return false;
    const wchar_t* ids[] = {L"codex_usage_5h", L"codex_usage_7d"};
    for (size_t i = 0; i < 2; ++i) {
        if (!ParsePreference(text, L"plugin_display_str_taskbar_window", ids[i], taskbar[i])) taskbar[i] = DefaultDisplayLabel(i == 0);
        if (!ParsePreference(text, L"plugin_display_str_main_window", ids[i], main_window[i])) main_window[i] = DefaultDisplayLabel(i == 0);
    }
    return true;
}

std::wstring ResolveExecutable(const std::wstring& path) {
    // Explicit executable paths must be absolute. Never search the working directory.
    if (!path.empty()) return ExistingExecutable(path);
    return FindCodexExe();
}

std::wstring FindCodexExe() {
    std::wstring best;
    ULONGLONG newest = 0;
    auto consider = [&](const std::wstring& candidate) {
        std::wstring resolved = ExistingExecutable(candidate);
        WIN32_FILE_ATTRIBUTE_DATA info{};
        if (!resolved.empty() && GetFileAttributesExW(resolved.c_str(), GetFileExInfoStandard, &info)) {
            ULARGE_INTEGER stamp{}; stamp.LowPart = info.ftLastWriteTime.dwLowDateTime;
            stamp.HighPart = info.ftLastWriteTime.dwHighDateTime;
            if (best.empty() || stamp.QuadPart > newest) { best = resolved; newest = stamp.QuadPart; }
        }
    };
    std::wstring local = Environment(L"LOCALAPPDATA");
    if (!local.empty()) {
        std::wstring binary_root = JoinPath(local, L"OpenAI\\Codex\\bin");
        consider(JoinPath(binary_root, L"codex.exe"));
        WIN32_FIND_DATAW info{};
        HANDLE search = FindFirstFileW(JoinPath(binary_root, L"*").c_str(), &info);
        if (search != INVALID_HANDLE_VALUE) {
            do {
                if (info.cFileName[0] != L'.' && (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                    !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                    consider(JoinPath(JoinPath(binary_root, info.cFileName), L"codex.exe"));
            } while (FindNextFileW(search, &info));
            FindClose(search);
        }
    }
    const wchar_t* key = L"Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\CurrentVersion\\AppModel\\Repository\\Packages";
    HKEY packages = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, key, 0, KEY_READ, &packages) == ERROR_SUCCESS) {
        for (DWORD index = 0;; ++index) {
            wchar_t name[256]{}; DWORD count = 256;
            LSTATUS status = RegEnumKeyExW(packages, index, name, &count, nullptr, nullptr, nullptr, nullptr);
            if (status == ERROR_NO_MORE_ITEMS) break;
            if (status != ERROR_SUCCESS || wcsncmp(name, L"OpenAI.Codex_", 13) != 0) continue;
            wchar_t root[32768]{}; DWORD bytes = sizeof(root);
            if (RegGetValueW(packages, name, L"PackageRootFolder", RRF_RT_REG_SZ, nullptr, root, &bytes) == ERROR_SUCCESS)
                consider(JoinPath(root, L"app\\resources\\codex.exe"));
        }
        RegCloseKey(packages);
    }
    if (!best.empty()) return best;
    std::wstring path = Environment(L"PATH");
    size_t position = 0;
    while (position < path.size()) {
        size_t end = path.find(L';', position);
        std::wstring directory = path.substr(position, end == std::wstring::npos ? end : end - position);
        if (directory.size() > 1 && directory.front() == L'"' && directory.back() == L'"')
            directory = directory.substr(1, directory.size() - 2);
        if (AbsolutePath(directory)) {
            std::wstring candidate = ExistingExecutable(JoinPath(directory, L"codex.exe"));
            if (!candidate.empty()) return candidate;
        }
        if (end == std::wstring::npos) break;
        position = end + 1;
    }
    return {};
}

bool ParseRpcReply(const std::string& text, int id, UsageData* data) {
    if (text.size() > kMaxLine) return false;
    JsonDocument document(yyjson_read(text.data(), text.size(), 0), yyjson_doc_free);
    if (!document) return false;
    yyjson_val* root = yyjson_doc_get_root(document.get());
    yyjson_val* response_id = yyjson_obj_get(root, "id");
    yyjson_val* result = yyjson_obj_get(root, "result");
    yyjson_val* error = yyjson_obj_get(root, "error");
    if (!yyjson_is_int(response_id) || yyjson_get_sint(response_id) != id ||
        !yyjson_is_obj(result) || (error && !yyjson_is_null(error))) return false;
    if (!data) return true;
    yyjson_val* buckets = yyjson_obj_get(result, "rateLimitsByLimitId");
    yyjson_val* snapshot = yyjson_obj_get(buckets, "codex");
    if (!snapshot) snapshot = yyjson_obj_get(result, "rateLimits");
    return ParseSnapshot(snapshot, *data);
}

bool ParseRolloutLine(const std::string& text, UsageData& data) {
    if (text.size() > kTailSize) return false;
    JsonDocument document(yyjson_read(text.data(), text.size(), 0), yyjson_doc_free);
    if (!document) return false;
    yyjson_val* root = yyjson_doc_get_root(document.get());
    yyjson_val* payload = yyjson_obj_get(root, "payload");
    if (!StringEquals(yyjson_obj_get(root, "type"), "event_msg") ||
        !StringEquals(yyjson_obj_get(payload, "type"), "token_count")) return false;
    if (!ParseSnapshot(yyjson_obj_get(payload, "rate_limits"), data)) return false;
    data.updated_at = ParseTimestamp(yyjson_obj_get(root, "timestamp"));
    data.stale = true;
    return true;
}

bool ReadTailAndParse(const std::wstring& path, UsageData& data) {
    Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (file.get() == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.get(), &size) || size.QuadPart <= 0) return false;
    LARGE_INTEGER offset{};
    offset.QuadPart = (std::max)(0LL, size.QuadPart - static_cast<LONGLONG>(kTailSize));
    if (!SetFilePointerEx(file.get(), offset, nullptr, FILE_BEGIN)) return false;
    std::string text(static_cast<size_t>(size.QuadPart - offset.QuadPart), '\0');
    DWORD read = 0;
    if (!ReadFile(file.get(), &text[0], static_cast<DWORD>(text.size()), &read, nullptr) || !read) return false;
    text.resize(read);
    size_t end = text.size();
    while (end) {
        if (text[end - 1] == '\n' || text[end - 1] == '\r') { --end; continue; }
        size_t newline = text.rfind('\n', end - 1);
        size_t begin = newline == std::string::npos ? 0 : newline + 1;
        if ((begin || offset.QuadPart == 0) && ParseRolloutLine(text.substr(begin, end - begin), data)) return true;
        end = newline == std::string::npos ? 0 : newline;
    }
    return false;
}

bool FetchFromLocalFiles(const std::wstring& directory, UsageData& data) {
    std::vector<FileEntry> newest; size_t visited = 0;
    if (!AbsolutePath(directory)) return false;
    WalkSessions(directory, 0, visited, newest);
    for (const auto& file : newest) if (ReadTailAndParse(file.path, data)) return true;
    return false;
}

bool FetchFromAppServer(const std::wstring& executable, UsageData& data) {
    std::wstring path = ResolveExecutable(executable);
    if (path.empty()) return false;
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE read_in = nullptr, write_in = nullptr, read_out = nullptr, write_out = nullptr;
    if (!CreatePipe(&read_in, &write_in, &security, 0)) return false;
    Handle child_in(read_in), parent_in(write_in);
    if (!CreatePipe(&read_out, &write_out, &security, 0)) return false;
    Handle parent_out(read_out), child_out(write_out);
    Handle discarded_errors(CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (discarded_errors.get() == INVALID_HANDLE_VALUE ||
        !SetHandleInformation(parent_in.get(), HANDLE_FLAG_INHERIT, 0) ||
        !SetHandleInformation(parent_out.get(), HANDLE_FLAG_INHERIT, 0)) return false;
    SIZE_T attribute_size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_size);
    std::vector<unsigned char> storage(attribute_size);
    auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &attribute_size)) return false;
    HANDLE inherited[] = {child_in.get(), child_out.get(), discarded_errors.get()};
    if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited,
        sizeof(inherited), nullptr, nullptr)) { DeleteProcThreadAttributeList(attributes); return false; }
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = child_in.get();
    startup.StartupInfo.hStdOutput = child_out.get();
    startup.StartupInfo.hStdError = discarded_errors.get();
    startup.lpAttributeList = attributes;
    std::wstring command = L"\"" + path + L"\" app-server";
    std::wstring working_directory = path.substr(0, path.find_last_of(L"\\/"));
    PROCESS_INFORMATION process{};
    BOOL created = CreateProcessW(path.c_str(), &command[0], nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, working_directory.c_str(),
        &startup.StartupInfo, &process);
    DeleteProcThreadAttributeList(attributes);
    child_in.reset(); child_out.reset(); discarded_errors.reset();
    if (!created) return false;
    Handle process_handle(process.hProcess), thread_handle(process.hThread);
    ProcessCleanup cleanup(process_handle.get());
    PipeReader reader(parent_out.get());
    const ULONGLONG deadline = GetTickCount64() + 20000;
    bool initialized = false, success = false;
    if (WriteMessage(parent_in.get(), "{\"id\":1,\"method\":\"initialize\",\"params\":{\"clientInfo\":{\"name\":\"trafficmonitor_codex_usage\",\"version\":\"0.1.2\"}}}\n")) {
        std::string line;
        while (reader.NextLine(line, deadline)) {
            if (ParseRpcReply(line, 1, nullptr)) { initialized = true; break; }
        }
        if (initialized && WriteMessage(parent_in.get(), "{\"method\":\"initialized\",\"params\":{}}\n") &&
            WriteMessage(parent_in.get(), "{\"id\":4207,\"method\":\"account/rateLimits/read\"}\n")) {
            while (reader.NextLine(line, deadline)) {
                if (ParseRpcReply(line, 4207, &data)) { success = true; break; }
            }
        }
    }
    parent_in.reset(); // EOF permits a graceful server exit before cleanup's timeout.
    if (success) {
        data.from_app_server = true;
        data.updated_at = static_cast<long long>(time(nullptr));
    }
    return success;
}
} // namespace codex_usage

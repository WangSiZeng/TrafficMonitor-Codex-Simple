#include <windows.h>
#include <iostream>
#include <string>

int main() {
    wchar_t inherited[64]{};
    if (GetEnvironmentVariableW(L"TM_TEST_HANDLE", inherited, 64)) {
        HANDLE handle = reinterpret_cast<HANDLE>(_wcstoui64(inherited, nullptr, 10));
        // A numeric handle can be reused for an unrelated file in the child.
        // SetEvent only succeeds if the actual event handle was inherited.
        if (SetEvent(handle)) return 10;
    }
    std::string line;
    if (!std::getline(std::cin, line) || line.find("initialize") == std::string::npos) return 1;
    std::cerr << "Synthetic private diagnostic must be discarded" << std::endl;
    Sleep(250);
    std::cout << "{ \"id\": 1, \"result\": {} }" << std::endl;
    if (!std::getline(std::cin, line) || line.find("initialized") == std::string::npos) return 2;
    if (!std::getline(std::cin, line) || line.find("account/rateLimits/read") == std::string::npos) return 3;
    wchar_t percent_text[16]{};
    int primary_used = 25;
    if (GetEnvironmentVariableW(L"TM_TEST_USED_PERCENT", percent_text, 16))
        primary_used = _wtoi(percent_text);
    std::cout << "{\"id\":4207,\"result\":{\"rateLimits\":{\"primary\":{\"usedPercent\":" << primary_used
        << ",\"windowDurationMins\":300,\"resetsAt\":2000000000},\"secondary\":{\"usedPercent\":40,\"windowDurationMins\":10080,\"resetsAt\":2000604800}}}}" << std::endl;
    while (std::getline(std::cin, line)) {}
    return 0;
}

#include "patch_internal.hpp"

namespace AGVoicePatch
{
std::string ToLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string WideToUtf8(const std::wstring& value)
{
    if (value.empty())
    {
        return {};
    }

    const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1)
    {
        return {};
    }

    std::string output(static_cast<size_t>(size - 1), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, output.data(), size, nullptr, nullptr) == 0)
    {
        return {};
    }

    return output;
}

std::wstring Utf8ToWide(const std::string& value)
{
    if (value.empty())
    {
        return {};
    }

    const int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (size <= 1)
    {
        return {};
    }

    std::wstring output(static_cast<size_t>(size - 1), L'\0');
    if (MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, output.data(), size) == 0)
    {
        return {};
    }

    return output;
}

void Log(const char* format, ...)
{
    char message[2048]{};
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    SYSTEMTIME now{};
    GetLocalTime(&now);

    std::lock_guard<std::mutex> lock(g_logMutex);

    if (g_consoleReady)
    {
        std::printf("[%02u:%02u:%02u] %s\n", now.wHour, now.wMinute, now.wSecond, message);
        std::fflush(stdout);
    }

    if (!g_config.log || g_logPath.empty())
    {
        return;
    }

    HANDLE file = CreateFileW(
        g_logPath.c_str(),
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return;
    }

    char line[2300]{};
    const int lineLength = std::snprintf(line, sizeof(line),
        "[%04u-%02u-%02u %02u:%02u:%02u] %s\r\n",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, message);
    if (lineLength > 0)
    {
        DWORD written = 0;
        WriteFile(file, line, static_cast<DWORD>(std::min<int>(lineLength, sizeof(line) - 1)), &written, nullptr);
    }

    CloseHandle(file);
}

void ClearLogFile()
{
    if (!g_config.log || g_logPath.empty() || g_logCleared)
    {
        return;
    }

    g_logCleared = true;
    HANDLE file = CreateFileW(
        g_logPath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file != INVALID_HANDLE_VALUE)
    {
        CloseHandle(file);
    }
}

void OpenConsole()
{
    if (!g_config.console || g_consoleReady)
    {
        return;
    }

    AllocConsole();
    FILE* unused = nullptr;
    freopen_s(&unused, "CONOUT$", "w", stdout);
    freopen_s(&unused, "CONOUT$", "w", stderr);
    SetConsoleTitleW(L"AetherGazer-VoicePatch");
    g_consoleReady = true;
}

std::wstring GetModuleDirectory(HMODULE module)
{
    std::array<wchar_t, MAX_PATH> path{};
    GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    std::wstring result(path.data());
    const auto slash = result.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : result.substr(0, slash);
}

bool WriteAbsoluteJump(void* address, void* destination, uint8_t* savedBytes)
{
    if (!address || !destination)
    {
        return false;
    }

    uint8_t patch[12] = {
        0x48, 0xB8, // mov rax, imm64
        0, 0, 0, 0, 0, 0, 0, 0,
        0xFF, 0xE0  // jmp rax
    };
    std::memcpy(patch + 2, &destination, sizeof(destination));

    DWORD oldProtect = 0;
    if (!VirtualProtect(address, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        return false;
    }

    if (savedBytes)
    {
        std::memcpy(savedBytes, address, sizeof(patch));
    }

    std::memcpy(address, patch, sizeof(patch));
    VirtualProtect(address, sizeof(patch), oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), address, sizeof(patch));
    return true;
}

bool InstallInlineHook(InlineHook& hook, void* target, void* replacement, void** original, const char* name)
{
    if (!target || !replacement || hook.installed)
    {
        return hook.installed;
    }

    auto* targetBytes = static_cast<uint8_t*>(target);
    if (targetBytes[0] == 0x48 && targetBytes[1] == 0xB8 && targetBytes[10] == 0xFF && targetBytes[11] == 0xE0)
    {
        Log("inline hook skipped, already patched: %s", name);
        return false;
    }

    constexpr size_t kPatchSize = 12;
    auto* trampoline = static_cast<uint8_t*>(VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!trampoline)
    {
        Log("inline hook trampoline alloc failed: %s", name);
        return false;
    }

    std::memcpy(trampoline, target, kPatchSize);
    void* resume = targetBytes + kPatchSize;
    WriteAbsoluteJump(trampoline + kPatchSize, resume);

    hook.target = target;
    hook.replacement = replacement;
    hook.trampoline = trampoline;
    hook.patchSize = kPatchSize;

    if (!WriteAbsoluteJump(target, replacement, hook.original))
    {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        hook = {};
        Log("inline hook install failed: %s", name);
        return false;
    }

    hook.installed = true;
    if (original)
    {
        *original = trampoline;
    }

    Log("inline hook installed: %s target=%p trampoline=%p", name, target, trampoline);
    return true;
}

std::string ReadIniString(const wchar_t* section, const wchar_t* key, const wchar_t* fallback)
{
    wchar_t buffer[1024]{};
    GetPrivateProfileStringW(section, key, fallback, buffer, static_cast<DWORD>(std::size(buffer)), g_iniPath.c_str());
    return WideToUtf8(buffer);
}

int ReadIniInt(const wchar_t* section, const wchar_t* key, int fallback)
{
    return static_cast<int>(GetPrivateProfileIntW(section, key, fallback, g_iniPath.c_str()));
}

std::vector<std::string> SplitList(const std::string& value, char delimiter)
{
    std::vector<std::string> result;
    size_t start = 0;
    while (start <= value.size())
    {
        const size_t end = value.find(delimiter, start);
        const size_t length = (end == std::string::npos ? value.size() : end) - start;
        std::string token = value.substr(start, length);
        // trim whitespace
        const auto first = token.find_first_not_of(" \t\r\n");
        const auto last = token.find_last_not_of(" \t\r\n");
        if (first != std::string::npos)
        {
            result.push_back(token.substr(first, last - first + 1));
        }
        if (end == std::string::npos)
        {
            break;
        }
        start = end + 1;
    }
    return result;
}

void LoadConfig()
{
    g_iniPath = g_moduleDir + L"\\AetherGazer-VoicePatch.ini";

    g_config.enabled = ReadIniInt(L"Voice", L"Enabled", 1) != 0;
    g_config.log = ReadIniInt(L"Voice", L"Log", 1) != 0;
    g_config.console = ReadIniInt(L"Voice", L"Console", 0) != 0;
    g_config.logAllDownloads = ReadIniInt(L"Voice", L"LogAllDownloads", 1) != 0;
    g_config.hookUnityWebRequest = ReadIniInt(L"Voice", L"HookUnityWebRequest", 1) != 0;

    const std::string from = ReadIniString(L"Voice", L"FromSegment", L"/pc/resources/");
    const std::string to = ReadIniString(L"Voice", L"ToSegment", L"/android/resources/");
    if (!from.empty())
    {
        g_config.fromSegment = from;
    }
    if (!to.empty())
    {
        g_config.toSegment = to;
    }

    const std::string marker = ReadIniString(L"Voice", L"PathMarker", L"voice");
    g_config.pathMarker = marker; // may be empty to disable localPath detection

    const std::string tokens = ReadIniString(L"Voice", L"VoiceTokens", L"voice_hash;voice_package_list");
    auto parsed = SplitList(tokens, ';');
    if (!parsed.empty())
    {
        g_config.voiceTokens.clear();
        for (auto& token : parsed)
        {
            g_config.voiceTokens.push_back(ToLower(token));
        }
    }
}
}

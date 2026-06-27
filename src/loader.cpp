#include <windows.h>

#include <array>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
std::wstring QuoteArg(const std::wstring& value)
{
    std::wstring result = L"\"";
    for (wchar_t ch : value)
    {
        if (ch == L'"')
        {
            result += L'\\';
        }
        result += ch;
    }
    result += L'"';
    return result;
}

std::wstring DirectoryOf(const std::wstring& path)
{
    const auto slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : path.substr(0, slash);
}

std::wstring ModulePath()
{
    std::array<wchar_t, MAX_PATH> path{};
    GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    return path.data();
}

std::wstring AbsolutePath(const std::wstring& path)
{
    std::array<wchar_t, 32768> buffer{};
    const DWORD length = GetFullPathNameW(path.c_str(), static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
    if (length == 0 || length >= buffer.size())
    {
        return path;
    }
    return buffer.data();
}

bool FileExists(const std::wstring& path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool InjectDll(HANDLE process, const std::wstring& dllPath)
{
    const size_t bytes = (dllPath.size() + 1) * sizeof(wchar_t);
    void* remotePath = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remotePath)
    {
        std::fwprintf(stderr, L"VirtualAllocEx failed: %lu\n", GetLastError());
        return false;
    }

    if (!WriteProcessMemory(process, remotePath, dllPath.c_str(), bytes, nullptr))
    {
        std::fwprintf(stderr, L"WriteProcessMemory failed: %lu\n", GetLastError());
        VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
        return false;
    }

    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    auto* loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(kernel32, "LoadLibraryW"));
    if (!loadLibrary)
    {
        std::fwprintf(stderr, L"LoadLibraryW lookup failed: %lu\n", GetLastError());
        VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
        return false;
    }

    HANDLE thread = CreateRemoteThread(process, nullptr, 0, loadLibrary, remotePath, 0, nullptr);
    if (!thread)
    {
        std::fwprintf(stderr, L"CreateRemoteThread failed: %lu\n", GetLastError());
        VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
        return false;
    }

    WaitForSingleObject(thread, INFINITE);

    DWORD exitCode = 0;
    GetExitCodeThread(thread, &exitCode);
    CloseHandle(thread);
    VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);

    if (exitCode == 0)
    {
        std::fwprintf(stderr, L"Remote LoadLibraryW failed for %ls\n", dllPath.c_str());
        return false;
    }

    return true;
}
}

int wmain(int argc, wchar_t** argv)
{
    const std::wstring loaderDir = DirectoryOf(ModulePath());
    const std::wstring gameExe = argc >= 2 ? AbsolutePath(argv[1]) : loaderDir + L"\\AetherGazer.exe";
    const std::wstring patchDll = AbsolutePath(loaderDir + L"\\AetherGazer-VoicePatch.dll");

    if (!FileExists(gameExe))
    {
        std::fwprintf(stderr, L"Game executable not found: %ls\n", gameExe.c_str());
        return 1;
    }
    if (!FileExists(patchDll))
    {
        std::fwprintf(stderr, L"Patch DLL not found: %ls\n", patchDll.c_str());
        return 1;
    }

    std::wstring commandLine = QuoteArg(gameExe);
    for (int i = 2; i < argc; ++i)
    {
        commandLine += L" ";
        commandLine += QuoteArg(argv[i]);
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');

    const std::wstring workingDir = DirectoryOf(gameExe);
    if (!CreateProcessW(gameExe.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE,
            CREATE_SUSPENDED, nullptr, workingDir.c_str(), &startup, &process))
    {
        std::fwprintf(stderr, L"CreateProcessW failed: %lu\n", GetLastError());
        return 1;
    }

    if (!InjectDll(process.hProcess, patchDll))
    {
        TerminateProcess(process.hProcess, 1);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return 1;
    }

    ResumeThread(process.hThread);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 0;
}

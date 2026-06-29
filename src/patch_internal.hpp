#pragma once

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace AGVoicePatch
{
// ---------------------------------------------------------------------------
// IL2CPP runtime exports (resolved from GameAssembly.dll by name -> version
// resilient, no hard-coded RVAs).
// ---------------------------------------------------------------------------
using Il2CppDomainGetFn = void* (*)();
using Il2CppThreadAttachFn = void* (*)(void*);
using Il2CppClassFromNameFn = void* (*)(void*, const char*, const char*);
using Il2CppClassGetMethodFromNameFn = void* (*)(void*, const char*, int);
using Il2CppStringNewFn = void* (*)(const char*);
using Il2CppDomainAssemblyOpenFn = void* (*)(void*, const char*);
using Il2CppAssemblyGetImageFn = void* (*)(void*);
using Il2CppDomainGetAssembliesFn = void** (*)(void*, size_t*);

// IL2CPP System.String layout (x64): [klass:8][monitor:8][length:int32 @0x10][chars @0x14]
struct Il2CppStringLayout
{
    void* klass;
    void* monitor;
    int32_t length;
    uint16_t chars[1];
};

struct Il2CppStringArrayLayout
{
    void* klass;
    void* monitor;
    void* bounds;
    size_t maxLength;
    void* vector[1];
};

// IL2CPP MethodInfo: the very first field is the compiled native code pointer.
inline void* MethodCodePointer(void* methodInfo)
{
    return methodInfo ? *reinterpret_cast<void**>(methodInfo) : nullptr;
}

struct InlineHook
{
    void* target = nullptr;
    void* replacement = nullptr;
    void* trampoline = nullptr;
    uint8_t original[16]{};
    size_t patchSize = 12;
    bool installed = false;
};

struct Config
{
    bool logAllDownloads = true; // log every resource URL that carries the configured PC branch
    std::string fromSegment = "/pc/resources/";
    std::string toSegment = "/android/resources/";
    // A url is treated as voice when it contains one of these tokens, or when
    // the caller explicitly marks it as voice based on a local /voice/ path.
    std::vector<std::string> voiceTokens = {"voice_hash"};
};

// ---- globals (state.cpp) --------------------------------------------------
extern HMODULE g_module;
extern std::wstring g_moduleDir;
extern std::wstring g_logPath;
extern Config g_config;
extern std::mutex g_logMutex;
extern bool g_consoleReady;
extern bool g_logCleared;
extern bool g_initialized;

extern Il2CppDomainGetFn g_il2cppDomainGet;
extern Il2CppThreadAttachFn g_il2cppThreadAttach;
extern Il2CppClassFromNameFn g_il2cppClassFromName;
extern Il2CppClassGetMethodFromNameFn g_il2cppClassGetMethodFromName;
extern Il2CppStringNewFn g_il2cppStringNew;
extern Il2CppAssemblyGetImageFn g_il2cppAssemblyGetImage;
extern Il2CppDomainGetAssembliesFn g_il2cppDomainGetAssemblies;

// ---- common.cpp -----------------------------------------------------------
std::string ToLower(std::string value);
std::string WideToUtf8(const std::wstring& value);
std::wstring Utf8ToWide(const std::string& value);
void Log(const char* format, ...);
void ClearLogFile();
std::wstring GetModuleDirectory(HMODULE module);
bool WriteAbsoluteJump(void* address, void* destination, uint8_t* savedBytes = nullptr);
bool InstallInlineHook(InlineHook& hook, void* target, void* replacement, void** original, const char* name);

// ---- il2cpp.cpp -----------------------------------------------------------
bool ResolveIl2Cpp();
void* FindIl2CppClass(const char* namespaze, const char* name);
void* FindMethodPointer(const char* namespaze, const char* className, const char* method, int argc);
std::string Il2CppStringToUtf8(void* il2cppString);
void* MakeIl2CppString(const std::string& value);

// ---- voice_redirect.cpp ---------------------------------------------------
// Returns the (possibly) rewritten url string object. If no rewrite is needed,
// returns the input pointer unchanged. When forceVoice is true the url is treated
// as voice regardless of voiceTokens (the caller decided via the cache localPath).
void* RewriteVoiceUrl(void* il2cppUrl, const char* source, bool forceVoice);
bool InstallVoiceRedirectHooks();

// ---- dllmain.cpp ----------------------------------------------------------
void Initialize();
}

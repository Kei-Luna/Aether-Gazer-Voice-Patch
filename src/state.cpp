#include "patch_internal.hpp"

namespace AGVoicePatch
{
HMODULE g_module = nullptr;
std::wstring g_moduleDir;
std::wstring g_logPath;
Config g_config;
std::mutex g_logMutex;
bool g_consoleReady = false;
bool g_logCleared = false;
bool g_initialized = false;

Il2CppDomainGetFn g_il2cppDomainGet = nullptr;
Il2CppThreadAttachFn g_il2cppThreadAttach = nullptr;
Il2CppClassFromNameFn g_il2cppClassFromName = nullptr;
Il2CppClassGetMethodFromNameFn g_il2cppClassGetMethodFromName = nullptr;
Il2CppStringNewFn g_il2cppStringNew = nullptr;
Il2CppAssemblyGetImageFn g_il2cppAssemblyGetImage = nullptr;
Il2CppDomainGetAssembliesFn g_il2cppDomainGetAssemblies = nullptr;
}

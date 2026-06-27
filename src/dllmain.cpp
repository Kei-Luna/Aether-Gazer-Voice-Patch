#include "patch_internal.hpp"

namespace AGVoicePatch
{
void Initialize()
{
    if (g_initialized)
    {
        return;
    }
    g_initialized = true;

    g_moduleDir = GetModuleDirectory(g_module);
    g_logPath = g_moduleDir + L"\\AetherGazer-VoicePatch.log";

    ClearLogFile();

    Log("Aether-Gazer Voice Patch loaded. from=%s to=%s tokens=%zu",
        g_config.fromSegment.c_str(),
        g_config.toSegment.c_str(),
        g_config.voiceTokens.size());
}

static DWORD WINAPI InitThread(void*)
{
    Initialize();

    // GameAssembly.dll / il2cpp metadata are not ready the instant the DLL is
    // injected, and the DownloadManager methods must be hooked before the splash
    // voice phase runs. Retry until the hooks are in place.
    bool installed = false;
    for (int attempt = 0; !installed && attempt < 600; ++attempt)
    {
        if (GetModuleHandleW(L"GameAssembly.dll"))
        {
            try
            {
                installed = InstallVoiceRedirectHooks();
            }
            catch (...)
            {
                Log("exception during hook install (attempt %d)", attempt);
            }
        }

        if (!installed)
        {
            Sleep(500);
        }
    }

    if (installed)
    {
        Log("voice redirect hooks active");
    }
    else
    {
        Log("WARNING: failed to install voice redirect hooks");
    }

    return 0;
}
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        AGVoicePatch::g_module = module;
        DisableThreadLibraryCalls(module);
        HANDLE thread = CreateThread(nullptr, 0, &AGVoicePatch::InitThread, nullptr, 0, nullptr);
        if (thread)
        {
            CloseHandle(thread);
        }
    }

    return TRUE;
}

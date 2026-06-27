#include "patch_internal.hpp"

#include <MinHook.h>

namespace AGVoicePatch
{
// IL2CPP-compiled instance methods carry a trailing hidden `MethodInfo*` arg.
// Every typedef/hook below forwards it so the trampoline ABI stays intact.
using StartDownload3Fn = void* (*)(void* self, void* url, void* userData, int priority, void* method);
using StartDownload6Fn = void* (*)(void* self, void* url, void* postData, void* header, void* callback, void* userData, int priority, void* method);
using StartDownloadCacheFn = void* (*)(void* self, void* url, void* localPath, void* callback, int priority, bool forceDownload, void* method);
using SetUrlFn = void (*)(void* self, void* url, void* method);
// PCYsDownload.PCDownLoadManager.Download(string baseUrl, string fileRootDir, string[] downloadList)
using PcDownloadFn = void (*)(void* self, void* baseUrl, void* fileRootDir, void* downloadList, void* method);

static StartDownload3Fn g_startDownload3Orig = nullptr;
static StartDownload6Fn g_startDownload6Orig = nullptr;
static StartDownloadCacheFn g_startDownloadCacheOrig = nullptr;
static SetUrlFn g_setUrlOrig = nullptr;
static PcDownloadFn g_pcDownloadOrig = nullptr;

static bool ContainsCaseInsensitive(const std::string& haystackLower, const std::string& needleLower)
{
    return !needleLower.empty() && haystackLower.find(needleLower) != std::string::npos;
}

static bool UrlHasFromSegment(const std::string& urlLower)
{
    return urlLower.find(ToLower(g_config.fromSegment)) != std::string::npos;
}

static bool MatchesVoiceToken(const std::string& urlLower)
{
    for (const auto& token : g_config.voiceTokens)
    {
        if (ContainsCaseInsensitive(urlLower, token))
        {
            return true;
        }
    }
    return false;
}

void* RewriteVoiceUrl(void* il2cppUrl, const char* source, bool forceVoice)
{
    if (!g_config.enabled || !il2cppUrl)
    {
        return il2cppUrl;
    }

    const std::string url = Il2CppStringToUtf8(il2cppUrl);
    if (url.empty())
    {
        return il2cppUrl;
    }

    const std::string urlLower = ToLower(url);
    const bool hasFrom = UrlHasFromSegment(urlLower);
    const bool isVoice = forceVoice || (hasFrom && MatchesVoiceToken(urlLower));

    if (!isVoice)
    {
        if (g_config.logAllDownloads && hasFrom)
        {
            // Resource download we are NOT redirecting (asset bundle, etc.).
            Log("[%s] keep  %s", source, url.c_str());
        }
        return il2cppUrl;
    }

    if (!hasFrom)
    {
        // Identified as voice (via localPath) but the url does not contain the
        // configured FromSegment -> the voice branch lives elsewhere. Log it so
        // FromSegment can be adjusted; do not touch the url.
        Log("[%s] voice url without FromSegment(%s): %s", source, g_config.fromSegment.c_str(), url.c_str());
        return il2cppUrl;
    }

    std::string rewritten = url;
    const size_t pos = urlLower.find(ToLower(g_config.fromSegment));
    rewritten.replace(pos, g_config.fromSegment.size(), g_config.toSegment);

    void* newString = MakeIl2CppString(rewritten);
    if (!newString)
    {
        Log("[%s] rewrite failed (string alloc) %s", source, url.c_str());
        return il2cppUrl;
    }

    Log("[%s] REDIRECT %s -> %s", source, url.c_str(), rewritten.c_str());
    return newString;
}

static bool LocalPathIsVoice(void* il2cppLocalPath)
{
    if (g_config.pathMarker.empty() || !il2cppLocalPath)
    {
        return false;
    }
    const std::string path = ToLower(Il2CppStringToUtf8(il2cppLocalPath));
    return ContainsCaseInsensitive(path, ToLower(g_config.pathMarker));
}

// ---- hook bodies ----------------------------------------------------------
static void* Hook_StartDownload3(void* self, void* url, void* userData, int priority, void* method)
{
    void* newUrl = RewriteVoiceUrl(url, "StartDownload", false);
    return g_startDownload3Orig(self, newUrl, userData, priority, method);
}

static void* Hook_StartDownload6(void* self, void* url, void* postData, void* header, void* callback, void* userData, int priority, void* method)
{
    void* newUrl = RewriteVoiceUrl(url, "StartDownload(post)", false);
    return g_startDownload6Orig(self, newUrl, postData, header, callback, userData, priority, method);
}

static void* Hook_StartDownloadCache(void* self, void* url, void* localPath, void* callback, int priority, bool forceDownload, void* method)
{
    // Voice hash manifest and voice files are cached under .../Voice/<lang>/...,
    // whereas asset bundles are cached under .../Windows/... -> the localPath is
    // a precise discriminator.
    const bool voice = LocalPathIsVoice(localPath);
    void* newUrl = RewriteVoiceUrl(url, "StartDownloadWithCache", voice);
    return g_startDownloadCacheOrig(self, newUrl, localPath, callback, priority, forceDownload, method);
}

static void Hook_SetUrl(void* self, void* url, void* method)
{
    void* newUrl = RewriteVoiceUrl(url, "UnityWebRequest.set_url", false);
    g_setUrlOrig(self, newUrl, method);
}

// The bulk .ys files (assets AND voice) are downloaded here, NOT through
// DownloadManager. Asset batches use fileRootDir ".../Windows/...", voice batches
// use ".../Voice/...", so fileRootDir is the discriminator; rewrite the shared
// baseUrl for voice batches only.
static void Hook_PCDownload(void* self, void* baseUrl, void* fileRootDir, void* downloadList, void* method)
{
    const bool voice = LocalPathIsVoice(fileRootDir);
    if (g_config.logAllDownloads)
    {
        Log("[PCDownLoadManager.Download] voice=%d baseUrl=%s fileRootDir=%s",
            voice ? 1 : 0,
            Il2CppStringToUtf8(baseUrl).c_str(),
            Il2CppStringToUtf8(fileRootDir).c_str());
    }
    void* newBase = RewriteVoiceUrl(baseUrl, "PCDownLoadManager.Download", voice);
    g_pcDownloadOrig(self, newBase, fileRootDir, downloadList, method);
}

static bool CreateAndEnable(void* target, void* detour, void** original, const char* name)
{
    if (!target)
    {
        return false;
    }

    MH_STATUS create = MH_CreateHook(target, detour, original);
    if (create != MH_OK)
    {
        Log("MH_CreateHook failed (%d) for %s target=%p", static_cast<int>(create), name, target);
        return false;
    }

    MH_STATUS enable = MH_EnableHook(target);
    if (enable != MH_OK)
    {
        Log("MH_EnableHook failed (%d) for %s target=%p", static_cast<int>(enable), name, target);
        return false;
    }

    Log("hook active: %s target=%p", name, target);
    return true;
}

bool InstallVoiceRedirectHooks()
{
    if (!ResolveIl2Cpp())
    {
        return false;
    }

    static bool minhookReady = false;
    if (!minhookReady)
    {
        MH_STATUS init = MH_Initialize();
        if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED)
        {
            Log("MH_Initialize failed (%d)", static_cast<int>(init));
            return false;
        }
        minhookReady = true;
    }

    bool any = false;

    if (void* code = FindMethodPointer("", "DownloadManager", "StartDownloadWithCache", 5))
    {
        any |= CreateAndEnable(code, reinterpret_cast<void*>(&Hook_StartDownloadCache),
            reinterpret_cast<void**>(&g_startDownloadCacheOrig), "DownloadManager.StartDownloadWithCache/5");
    }

    if (void* code = FindMethodPointer("", "DownloadManager", "StartDownload", 3))
    {
        any |= CreateAndEnable(code, reinterpret_cast<void*>(&Hook_StartDownload3),
            reinterpret_cast<void**>(&g_startDownload3Orig), "DownloadManager.StartDownload/3");
    }

    if (void* code = FindMethodPointer("", "DownloadManager", "StartDownload", 6))
    {
        any |= CreateAndEnable(code, reinterpret_cast<void*>(&Hook_StartDownload6),
            reinterpret_cast<void**>(&g_startDownload6Orig), "DownloadManager.StartDownload/6");
    }

    if (void* code = FindMethodPointer("PCYsDownload", "PCDownLoadManager", "Download", 3))
    {
        any |= CreateAndEnable(code, reinterpret_cast<void*>(&Hook_PCDownload),
            reinterpret_cast<void**>(&g_pcDownloadOrig), "PCDownLoadManager.Download/3");
    }

    if (g_config.hookUnityWebRequest)
    {
        if (void* code = FindMethodPointer("UnityEngine.Networking", "UnityWebRequest", "set_url", 1))
        {
            any |= CreateAndEnable(code, reinterpret_cast<void*>(&Hook_SetUrl),
                reinterpret_cast<void**>(&g_setUrlOrig), "UnityWebRequest.set_url/1");
        }
    }

    return any;
}
}

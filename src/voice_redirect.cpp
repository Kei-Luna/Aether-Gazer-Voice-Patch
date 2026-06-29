#include "patch_internal.hpp"

#include <MinHook.h>

namespace AGVoicePatch
{
// IL2CPP-compiled instance methods carry a trailing hidden `MethodInfo*` arg.
// Every typedef/hook below forwards it so the trampoline ABI stays intact.
using StartDownload3Fn = void* (*)(void* self, void* url, void* userData, int priority, void* method);
using StartDownload6Fn = void* (*)(void* self, void* url, void* postData, void* header, void* callback, void* userData, int priority, void* method);
using StartDownloadCacheFn = void* (*)(void* self, void* url, void* localPath, void* callback, int priority, bool forceDownload, void* method);
using FormatUrlFn = void* (*)(void* self, void* url, void* method);
using AssetStartDownload1Fn = void* (*)(void* url, void* method);
using AssetStartDownload2Fn = void* (*)(void* url, void* userData, void* method);
using AssetStartDownloadCacheFn = void* (*)(void* url, void* localCachePath, void* callback, bool forceDownload, void* method);
using AssetGetWWWFn = void* (*)(void* url, void* method);
using SetUrlFn = void (*)(void* self, void* url, void* method);
using UnityWebRequestGetFn = void* (*)(void* url, void* method);
using UnityWebRequestCtor2Fn = void (*)(void* self, void* url, void* methodName, void* method);
using UnityWebRequestCtor4Fn = void (*)(void* self, void* url, void* methodName, void* downloadHandler, void* uploadHandler, void* method);
// PCYsDownload.PCDownLoadManager.Download(string baseUrl, string fileRootDir, string[] downloadList)
using PcDownloadFn = void (*)(void* self, void* baseUrl, void* fileRootDir, void* downloadList, void* method);

static StartDownload3Fn g_startDownload3Orig = nullptr;
static StartDownload6Fn g_startDownload6Orig = nullptr;
static StartDownloadCacheFn g_startDownloadCacheOrig = nullptr;
static FormatUrlFn g_formatUrlOrig = nullptr;
static AssetStartDownload1Fn g_assetStartDownload1Orig = nullptr;
static AssetStartDownload2Fn g_assetStartDownload2Orig = nullptr;
static AssetStartDownloadCacheFn g_assetStartDownloadCacheOrig = nullptr;
static AssetGetWWWFn g_assetGetWWWOrig = nullptr;
static SetUrlFn g_setUrlOrig = nullptr;
static UnityWebRequestGetFn g_unityWebRequestGetOrig = nullptr;
static UnityWebRequestCtor2Fn g_unityWebRequestCtor2Orig = nullptr;
static UnityWebRequestCtor4Fn g_unityWebRequestCtor4Orig = nullptr;
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
    if (!il2cppUrl)
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
    const bool hasVoiceToken = MatchesVoiceToken(urlLower);
    const bool isVoice = forceVoice || hasVoiceToken;

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
    if (!il2cppLocalPath)
    {
        return false;
    }

    std::string path = ToLower(Il2CppStringToUtf8(il2cppLocalPath));
    std::replace(path.begin(), path.end(), '\\', '/');
    return path.find("/voice/") != std::string::npos ||
        path.ends_with("/voice");
}

static void LogStringArraySample(const char* source, void* il2cppArray, size_t maxItems)
{
    if (!il2cppArray)
    {
        Log("[%s] downloadList=null", source);
        return;
    }

    auto* array = reinterpret_cast<Il2CppStringArrayLayout*>(il2cppArray);
    Log("[%s] downloadList count=%zu", source, array->maxLength);

    const size_t count = std::min(array->maxLength, maxItems);
    for (size_t i = 0; i < count; ++i)
    {
        Log("[%s] downloadList[%zu]=%s", source, i, Il2CppStringToUtf8(array->vector[i]).c_str());
    }
}

static bool DownloadListContainsVoice(void* il2cppArray)
{
    if (!il2cppArray)
    {
        return false;
    }

    auto* array = reinterpret_cast<Il2CppStringArrayLayout*>(il2cppArray);
    for (size_t i = 0; i < array->maxLength; ++i)
    {
        std::string item = ToLower(Il2CppStringToUtf8(array->vector[i]));
        std::replace(item.begin(), item.end(), '\\', '/');
        if (item.find("../voice/") != std::string::npos ||
            item.find("/voice/") != std::string::npos ||
            item.starts_with("voice/"))
        {
            return true;
        }
    }

    return false;
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

static void* Hook_FormatUrl(void* self, void* url, void* method)
{
    void* formatted = g_formatUrlOrig(self, url, method);
    return RewriteVoiceUrl(formatted, "DownloadManager.formatUrl", false);
}

static void* Hook_AssetStartDownload1(void* url, void* method)
{
    void* newUrl = RewriteVoiceUrl(url, "Asset.StartDownload/1", false);
    return g_assetStartDownload1Orig(newUrl, method);
}

static void* Hook_AssetStartDownload2(void* url, void* userData, void* method)
{
    void* newUrl = RewriteVoiceUrl(url, "Asset.StartDownload/2", false);
    return g_assetStartDownload2Orig(newUrl, userData, method);
}

static void* Hook_AssetStartDownloadCache(void* url, void* localCachePath, void* callback, bool forceDownload, void* method)
{
    const bool voice = LocalPathIsVoice(localCachePath);
    void* newUrl = RewriteVoiceUrl(url, "Asset.StartDownloadWitchCache", voice);
    return g_assetStartDownloadCacheOrig(newUrl, localCachePath, callback, forceDownload, method);
}

static void* Hook_AssetGetWWW(void* url, void* method)
{
    void* newUrl = RewriteVoiceUrl(url, "Asset.GetWWW", false);
    return g_assetGetWWWOrig(newUrl, method);
}

static void Hook_SetUrl(void* self, void* url, void* method)
{
    void* newUrl = RewriteVoiceUrl(url, "UnityWebRequest.set_url", false);
    g_setUrlOrig(self, newUrl, method);
}

static void* Hook_UnityWebRequestGet(void* url, void* method)
{
    void* newUrl = RewriteVoiceUrl(url, "UnityWebRequest.Get", false);
    return g_unityWebRequestGetOrig(newUrl, method);
}

static void Hook_UnityWebRequestCtor2(void* self, void* url, void* methodName, void* method)
{
    void* newUrl = RewriteVoiceUrl(url, "UnityWebRequest..ctor/2", false);
    g_unityWebRequestCtor2Orig(self, newUrl, methodName, method);
}

static void Hook_UnityWebRequestCtor4(void* self, void* url, void* methodName, void* downloadHandler, void* uploadHandler, void* method)
{
    void* newUrl = RewriteVoiceUrl(url, "UnityWebRequest..ctor/4", false);
    g_unityWebRequestCtor4Orig(self, newUrl, methodName, downloadHandler, uploadHandler, method);
}

// The bulk .ys files (assets AND voice) are downloaded here, NOT through
// DownloadManager. Most asset batches use fileRootDir ".../Windows/...", while
// voice batches may use either a Voice root or "../Voice/..." entries in the
// download list; rewrite the shared baseUrl for voice batches only.
static void Hook_PCDownload(void* self, void* baseUrl, void* fileRootDir, void* downloadList, void* method)
{
    const bool voice = LocalPathIsVoice(fileRootDir) || DownloadListContainsVoice(downloadList);
    if (g_config.logAllDownloads)
    {
        Log("[PCDownLoadManager.Download] voice=%d baseUrl=%s fileRootDir=%s",
            voice ? 1 : 0,
            Il2CppStringToUtf8(baseUrl).c_str(),
            Il2CppStringToUtf8(fileRootDir).c_str());
        LogStringArraySample("PCDownLoadManager.Download", downloadList, 8);
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

    if (void* code = FindMethodPointer("", "DownloadManager", "formatUrl", 1))
    {
        any |= CreateAndEnable(code, reinterpret_cast<void*>(&Hook_FormatUrl),
            reinterpret_cast<void**>(&g_formatUrlOrig), "DownloadManager.formatUrl/1");
    }

    if (void* code = FindMethodPointer("", "Asset", "StartDownload", 1))
    {
        any |= CreateAndEnable(code, reinterpret_cast<void*>(&Hook_AssetStartDownload1),
            reinterpret_cast<void**>(&g_assetStartDownload1Orig), "Asset.StartDownload/1");
    }

    if (void* code = FindMethodPointer("", "Asset", "StartDownload", 2))
    {
        any |= CreateAndEnable(code, reinterpret_cast<void*>(&Hook_AssetStartDownload2),
            reinterpret_cast<void**>(&g_assetStartDownload2Orig), "Asset.StartDownload/2");
    }

    if (void* code = FindMethodPointer("", "Asset", "StartDownloadWitchCache", 4))
    {
        any |= CreateAndEnable(code, reinterpret_cast<void*>(&Hook_AssetStartDownloadCache),
            reinterpret_cast<void**>(&g_assetStartDownloadCacheOrig), "Asset.StartDownloadWitchCache/4");
    }

    if (void* code = FindMethodPointer("", "Asset", "GetWWW", 1))
    {
        any |= CreateAndEnable(code, reinterpret_cast<void*>(&Hook_AssetGetWWW),
            reinterpret_cast<void**>(&g_assetGetWWWOrig), "Asset.GetWWW/1");
    }

    if (void* code = FindMethodPointer("PCYsDownload", "PCDownLoadManager", "Download", 3))
    {
        any |= CreateAndEnable(code, reinterpret_cast<void*>(&Hook_PCDownload),
            reinterpret_cast<void**>(&g_pcDownloadOrig), "PCDownLoadManager.Download/3");
    }

    {
        if (void* code = FindMethodPointer("UnityEngine.Networking", "UnityWebRequest", "set_url", 1))
        {
            any |= CreateAndEnable(code, reinterpret_cast<void*>(&Hook_SetUrl),
                reinterpret_cast<void**>(&g_setUrlOrig), "UnityWebRequest.set_url/1");
        }

        if (void* code = FindMethodPointer("UnityEngine.Networking", "UnityWebRequest", "Get", 1))
        {
            any |= CreateAndEnable(code, reinterpret_cast<void*>(&Hook_UnityWebRequestGet),
                reinterpret_cast<void**>(&g_unityWebRequestGetOrig), "UnityWebRequest.Get/1");
        }

        if (void* code = FindMethodPointer("UnityEngine.Networking", "UnityWebRequest", ".ctor", 2))
        {
            any |= CreateAndEnable(code, reinterpret_cast<void*>(&Hook_UnityWebRequestCtor2),
                reinterpret_cast<void**>(&g_unityWebRequestCtor2Orig), "UnityWebRequest..ctor/2");
        }

        if (void* code = FindMethodPointer("UnityEngine.Networking", "UnityWebRequest", ".ctor", 4))
        {
            any |= CreateAndEnable(code, reinterpret_cast<void*>(&Hook_UnityWebRequestCtor4),
                reinterpret_cast<void**>(&g_unityWebRequestCtor4Orig), "UnityWebRequest..ctor/4");
        }
    }

    return any;
}
}

#include "patch_internal.hpp"

namespace AGVoicePatch
{
template <typename T>
static T Proc(HMODULE module, const char* name)
{
    return reinterpret_cast<T>(GetProcAddress(module, name));
}

bool ResolveIl2Cpp()
{
    if (g_il2cppClassFromName && g_il2cppStringNew)
    {
        return true;
    }

    HMODULE gameAssembly = GetModuleHandleW(L"GameAssembly.dll");
    if (!gameAssembly)
    {
        return false;
    }

    g_il2cppDomainGet = Proc<Il2CppDomainGetFn>(gameAssembly, "il2cpp_domain_get");
    g_il2cppThreadAttach = Proc<Il2CppThreadAttachFn>(gameAssembly, "il2cpp_thread_attach");
    g_il2cppClassFromName = Proc<Il2CppClassFromNameFn>(gameAssembly, "il2cpp_class_from_name");
    g_il2cppClassGetMethodFromName = Proc<Il2CppClassGetMethodFromNameFn>(gameAssembly, "il2cpp_class_get_method_from_name");
    g_il2cppStringNew = Proc<Il2CppStringNewFn>(gameAssembly, "il2cpp_string_new");
    g_il2cppAssemblyGetImage = Proc<Il2CppAssemblyGetImageFn>(gameAssembly, "il2cpp_assembly_get_image");
    g_il2cppDomainGetAssemblies = Proc<Il2CppDomainGetAssembliesFn>(gameAssembly, "il2cpp_domain_get_assemblies");

    if (!g_il2cppDomainGet || !g_il2cppThreadAttach || !g_il2cppClassFromName ||
        !g_il2cppClassGetMethodFromName || !g_il2cppStringNew ||
        !g_il2cppAssemblyGetImage || !g_il2cppDomainGetAssemblies)
    {
        return false;
    }

    g_il2cppThreadAttach(g_il2cppDomainGet());
    return true;
}

void* FindIl2CppClass(const char* namespaze, const char* name)
{
    if (!g_il2cppDomainGet || !g_il2cppDomainGetAssemblies || !g_il2cppAssemblyGetImage || !g_il2cppClassFromName)
    {
        return nullptr;
    }

    void* domain = g_il2cppDomainGet();
    if (!domain)
    {
        return nullptr;
    }

    size_t count = 0;
    void** assemblies = g_il2cppDomainGetAssemblies(domain, &count);
    if (!assemblies)
    {
        return nullptr;
    }

    for (size_t i = 0; i < count; ++i)
    {
        void* image = g_il2cppAssemblyGetImage(assemblies[i]);
        if (!image)
        {
            continue;
        }

        if (void* klass = g_il2cppClassFromName(image, namespaze, name))
        {
            return klass;
        }
    }

    return nullptr;
}

void* FindMethodPointer(const char* namespaze, const char* className, const char* method, int argc)
{
    void* klass = FindIl2CppClass(namespaze, className);
    if (!klass)
    {
        Log("il2cpp class not found: %s.%s", namespaze && *namespaze ? namespaze : "<global>", className);
        return nullptr;
    }

    void* methodInfo = g_il2cppClassGetMethodFromName(klass, method, argc);
    if (!methodInfo)
    {
        Log("il2cpp method not found: %s.%s::%s/%d", namespaze && *namespaze ? namespaze : "<global>", className, method, argc);
        return nullptr;
    }

    void* code = MethodCodePointer(methodInfo);
    Log("resolved %s::%s/%d methodInfo=%p code=%p", className, method, argc, methodInfo, code);
    return code;
}

std::string Il2CppStringToUtf8(void* il2cppString)
{
    if (!il2cppString)
    {
        return {};
    }

    auto* str = static_cast<Il2CppStringLayout*>(il2cppString);
    if (str->length <= 0 || str->length > (1 << 22))
    {
        return {};
    }

    const std::wstring wide(reinterpret_cast<const wchar_t*>(str->chars), static_cast<size_t>(str->length));
    return WideToUtf8(wide);
}

void* MakeIl2CppString(const std::string& value)
{
    if (!g_il2cppStringNew)
    {
        return nullptr;
    }
    return g_il2cppStringNew(value.c_str());
}
}

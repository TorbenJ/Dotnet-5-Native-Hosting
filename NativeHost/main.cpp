#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>

#include "coreclr_delegates.h"
#include "hostfxr.h"
#include "nethost.h"

#include <climits>

#include <Windows.h>

struct HostFxr {
    void* m_Handle = nullptr;
    hostfxr_initialize_for_runtime_config_fn m_InitRuntimeFn = nullptr;
    hostfxr_get_runtime_delegate_fn m_GetRuntimeDelegateFn = nullptr;
    hostfxr_close_fn m_CloseFn = nullptr;
};

std::optional<HostFxr> loadHostFxr();
load_assembly_and_get_function_pointer_fn getDotnetLoadAssembly(const HostFxr& hostFxr, const std::filesystem::path& configPath);

void SayHello()
{
    std::cout << "Hello from host" << std::endl;
}

int main(int argc, char** argv)
{
    // Resolve absolute path of executable.
    char workingDir[MAX_PATH];
    int rc = GetFullPathNameA(argv[0], MAX_PATH, workingDir, nullptr);
    assert(rc > 0);

    // Get parent directory of executable.
    std::filesystem::path rootPath = workingDir;
    rootPath = rootPath.parent_path();
    assert(!rootPath.empty());

    // Load HostFxr
    std::optional<HostFxr> hostFxr = loadHostFxr();
    if (!hostFxr)
    {
        assert(false && "Failure: loadHostFxr()");
        return EXIT_FAILURE;
    }

    // Initialize and start runtime.
    const std::filesystem::path configPath = rootPath / "DotNetLib.runtimeconfig.json";
    load_assembly_and_get_function_pointer_fn loadAssemblyAndGetFunctionPointerFn = getDotnetLoadAssembly(*hostFxr, configPath);
    assert(loadAssemblyAndGetFunctionPointerFn != nullptr && "Failure: getDotnetLoadAssembly()");

    // Load managed assembly and get entry point.
    const std::filesystem::path assemblyPath = rootPath / "DotNetLib.dll";
    const char_t* dotnetType = L"DotNetLib.Lib, DotNetLib";
    const char_t* dotnetTypeMethod = L"PluginMain";

    struct HostApi {
        typedef void (*helloFn)();

        helloFn Hello = nullptr;
    };

    HostApi hostApi;
    hostApi.Hello = SayHello;

    typedef void(CORECLR_DELEGATE_CALLTYPE * customEntryPointFn)(HostApi);
    customEntryPointFn entryPoint = nullptr;
    rc = loadAssemblyAndGetFunctionPointerFn(
            assemblyPath.c_str(),
            dotnetType,
            dotnetTypeMethod,
            UNMANAGEDCALLERSONLY_METHOD,
            nullptr,
            reinterpret_cast<void**>(&entryPoint));

    assert(rc == 0 && entryPoint != nullptr && "Failure: loadAssemblyAndGetFunctionPointer()");

    // Run managed code.
    entryPoint(hostApi);

    FreeLibrary((HMODULE) hostFxr->m_Handle);

    return EXIT_SUCCESS;
}

template<typename T>
T findHostFxrSymbol(const HostFxr hostFxr, const char* name) {
    return reinterpret_cast<T>(GetProcAddress((HMODULE) hostFxr.m_Handle, name));
}

std::optional<HostFxr> loadHostFxr()
{
    char_t buffer[MAX_PATH];
    size_t bufferSize = sizeof(buffer) / sizeof(char);

    // Get path to hostfxr library.
    int rc = get_hostfxr_path(buffer, &bufferSize, nullptr);
    if (rc != 0)
    {
        std::cerr << "Failed to locate hostfxr path" << std::endl;
        return std::nullopt;
    }

    // Get library handle.
    HostFxr hostFxr;

    hostFxr.m_Handle = LoadLibrary(buffer);
    assert(hostFxr.m_Handle != nullptr);

    // Locate hostfxr symbols.
    hostFxr.m_InitRuntimeFn = findHostFxrSymbol<hostfxr_initialize_for_runtime_config_fn>(hostFxr, "hostfxr_initialize_for_runtime_config");
    hostFxr.m_GetRuntimeDelegateFn = findHostFxrSymbol<hostfxr_get_runtime_delegate_fn>(hostFxr, "hostfxr_get_runtime_delegate");
    hostFxr.m_CloseFn = findHostFxrSymbol<hostfxr_close_fn>(hostFxr, "hostfxr_close");

    if (!hostFxr.m_InitRuntimeFn || !hostFxr.m_GetRuntimeDelegateFn || !hostFxr.m_CloseFn)
    {
        return std::nullopt;
    }

    return hostFxr;
}

load_assembly_and_get_function_pointer_fn getDotnetLoadAssembly(const HostFxr& hostFxr, const std::filesystem::path& configPath)
{
    void* loadAssemblyAndGetFunctionPointer = nullptr;
    hostfxr_handle ctx = nullptr;

    int rc = hostFxr.m_InitRuntimeFn(configPath.c_str(), nullptr, &ctx);
    if (rc != 0 || ctx == nullptr)
    {
        std::cerr << "Failed to init runtime: " << std::hex << std::showbase << rc << std::endl;
        hostFxr.m_CloseFn(ctx);
        return nullptr;
    }

    rc = hostFxr.m_GetRuntimeDelegateFn(ctx, hdt_load_assembly_and_get_function_pointer, &loadAssemblyAndGetFunctionPointer);
    if (rc != 0 || loadAssemblyAndGetFunctionPointer == nullptr)
    {
        std::cerr << "Get delegate failed: " << std::hex << std::showbase << rc << std::endl;
    }

    hostFxr.m_CloseFn(ctx);

    return reinterpret_cast<load_assembly_and_get_function_pointer_fn>(loadAssemblyAndGetFunctionPointer);
}

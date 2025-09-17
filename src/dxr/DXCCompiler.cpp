#include "DXCCompiler.h"
#include "../utils/Logger.h"
#include <stdexcept>
#include <locale>
#include <codecvt>

// Define SAL annotations if not already defined
#ifndef _In_
#define _In_
#endif
#ifndef _In_z_
#define _In_z_
#endif
#ifndef _In_bytecount_
#define _In_bytecount_(size)
#endif
#ifndef _Out_
#define _Out_
#endif
#ifndef _Outptr_
#define _Outptr_
#endif

DXCCompiler::DXCCompiler() = default;

bool DXCCompiler::Initialize() {
    LOGI("Initializing DXC compiler...");

    // Create DXC utils
    HRESULT hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&m_utils));
    if (FAILED(hr)) {
        LOGE("Failed to create DXC utils");
        return false;
    }

    // Create DXC compiler
    hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&m_compiler));
    if (FAILED(hr)) {
        LOGE("Failed to create DXC compiler");
        return false;
    }

    // Create include handler
    hr = m_utils->CreateDefaultIncludeHandler(&m_includeHandler);
    if (FAILED(hr)) {
        LOGE("Failed to create DXC include handler");
        return false;
    }

    LOGI("DXC compiler initialized successfully");
    return true;
}

std::wstring DXCCompiler::StringToWString(const std::string& str) {
    if (str.empty()) return std::wstring();

    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

std::string DXCCompiler::ExtractErrorMessage(IDxcResult* result) {
    ComPtr<IDxcBlobEncoding> errorBuffer;
    HRESULT hr = result->GetErrorBuffer(&errorBuffer);
    if (FAILED(hr) || !errorBuffer) {
        return "Failed to get error buffer";
    }

    if (errorBuffer->GetBufferSize() > 0) {
        return std::string((char*)errorBuffer->GetBufferPointer(), errorBuffer->GetBufferSize());
    }

    return "No error message available";
}

bool DXCCompiler::CompileShader(
    const std::string& source,
    const std::wstring& entryPoint,
    const std::wstring& target,
    const std::vector<std::wstring>& arguments,
    ComPtr<IDxcBlob>& compiledShader,
    std::string& errorMessage) {

    if (!IsAvailable()) {
        errorMessage = "DXC compiler not initialized";
        return false;
    }

    // Create source buffer
    ComPtr<IDxcBlobEncoding> sourceBlob;
    HRESULT hr = m_utils->CreateBlob(source.c_str(), static_cast<UINT32>(source.size()), CP_UTF8, &sourceBlob);
    if (FAILED(hr)) {
        errorMessage = "Failed to create source blob";
        return false;
    }

    // Prepare arguments
    std::vector<LPCWSTR> args;
    for (const auto& arg : arguments) {
        args.push_back(arg.c_str());
    }

    // Compile arguments
    DxcBuffer sourceBuffer;
    sourceBuffer.Ptr = sourceBlob->GetBufferPointer();
    sourceBuffer.Size = sourceBlob->GetBufferSize();
    sourceBuffer.Encoding = DXC_CP_ACP;

    ComPtr<IDxcResult> result;
    hr = m_compiler->Compile(
        &sourceBuffer,
        args.data(),
        static_cast<UINT32>(args.size()),
        m_includeHandler.Get(),
        IID_PPV_ARGS(&result));

    if (FAILED(hr)) {
        errorMessage = "DXC compilation failed";
        return false;
    }

    // Check compilation status
    HRESULT compileStatus;
    hr = result->GetStatus(&compileStatus);
    if (FAILED(hr)) {
        errorMessage = "Failed to get compilation status";
        return false;
    }

    if (FAILED(compileStatus)) {
        errorMessage = ExtractErrorMessage(result.Get());
        return false;
    }

    // Get compiled shader
    hr = result->GetResult(&compiledShader);
    if (FAILED(hr)) {
        errorMessage = "Failed to get compilation result";
        return false;
    }

    return true;
}

bool DXCCompiler::CompileDXRLibrary(
    const std::string& source,
    const std::vector<std::wstring>& additionalArgs,
    ComPtr<IDxcBlob>& compiledLibrary,
    std::string& errorMessage) {

    // Standard arguments for DXR library compilation
    std::vector<std::wstring> args = {
        L"-T", L"lib_6_3",  // Target shader model for DXR
#ifdef _DEBUG
        L"-Zi",             // Debug information
        L"-Od",             // Disable optimizations
#else
        L"-O3",             // Optimize for speed
#endif
        L"-Qembed_debug"    // Embed debug info in shader
    };

    // Add additional arguments
    args.insert(args.end(), additionalArgs.begin(), additionalArgs.end());

    return CompileShader(source, L"", L"lib_6_3", args, compiledLibrary, errorMessage);
}
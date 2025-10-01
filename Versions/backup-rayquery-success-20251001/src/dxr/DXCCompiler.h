#pragma once
#include <windows.h>
#include <sal.h>
#include <dxcapi.h>
#include <wrl.h>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

// DXC (DirectX Shader Compiler) wrapper for DXR shader compilation
class DXCCompiler {
public:
    DXCCompiler();
    ~DXCCompiler() = default;

    // Initialize the DXC compiler
    bool Initialize();

    // Compile HLSL source to DXIL bytecode
    bool CompileShader(
        const std::string& source,
        const std::wstring& entryPoint,
        const std::wstring& target,
        const std::vector<std::wstring>& arguments,
        ComPtr<IDxcBlob>& compiledShader,
        std::string& errorMessage);

    // Compile DXR library (lib_6_3 target)
    bool CompileDXRLibrary(
        const std::string& source,
        const std::vector<std::wstring>& arguments,
        ComPtr<IDxcBlob>& compiledLibrary,
        std::string& errorMessage);

    // Check if DXC is available
    bool IsAvailable() const { return m_compiler && m_utils && m_includeHandler; }

private:
    ComPtr<IDxcCompiler3> m_compiler;
    ComPtr<IDxcUtils> m_utils;
    ComPtr<IDxcIncludeHandler> m_includeHandler;

    // Helper to convert string to wide string
    std::wstring StringToWString(const std::string& str);

    // Helper to extract error messages from compilation result
    std::string ExtractErrorMessage(IDxcResult* result);
};
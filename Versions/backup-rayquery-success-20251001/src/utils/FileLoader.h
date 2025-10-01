#pragma once
#include <vector>
#include <string>
#include <d3d12.h>
#include <wrl.h>

using Microsoft::WRL::ComPtr;

class FileLoader {
public:
    // Load compiled DXIL shader from file
    static bool LoadDXILShader(
        const std::string& filePath,
        ComPtr<ID3DBlob>& shaderBlob,
        std::string& errorMessage);

    // Load binary file into memory
    static bool LoadBinaryFile(
        const std::string& filePath,
        std::vector<uint8_t>& data);
};
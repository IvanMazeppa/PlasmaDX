#include "FileLoader.h"
#include "Logger.h"
#include <d3dcompiler.h>
#include <fstream>

bool FileLoader::LoadBinaryFile(const std::string& filePath, std::vector<uint8_t>& data) {
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return false;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    data.resize(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(data.data()), size)) {
        return false;
    }

    return true;
}

bool FileLoader::LoadDXILShader(
    const std::string& filePath,
    ComPtr<ID3DBlob>& shaderBlob,
    std::string& errorMessage) {

    std::vector<uint8_t> shaderData;
    if (!LoadBinaryFile(filePath, shaderData)) {
        errorMessage = "Failed to load shader file: " + filePath;
        return false;
    }

    if (shaderData.empty()) {
        errorMessage = "Shader file is empty: " + filePath;
        return false;
    }

    // Create D3D blob from loaded data
    HRESULT hr = D3DCreateBlob(shaderData.size(), &shaderBlob);
    if (FAILED(hr)) {
        errorMessage = "Failed to create D3D blob";
        return false;
    }

    // Copy data to blob
    memcpy(shaderBlob->GetBufferPointer(), shaderData.data(), shaderData.size());

    LOGI("Loaded DXIL shader: " + filePath + " (" + std::to_string(shaderData.size()) + " bytes)");
    return true;
}
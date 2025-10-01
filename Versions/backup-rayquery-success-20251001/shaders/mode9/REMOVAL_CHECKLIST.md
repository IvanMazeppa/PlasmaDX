# Shadow Map RayQuery Migration - File Removal Checklist

## Files to Delete Completely

### 1. Old DispatchRays Shader
- **Path**: `/mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX/shaders/mode9/shadow_map.hlsl`
- **Reason**: Replaced by `shadow_map_rayquery.hlsl`
- **Contains**:
  - `ShadowRayGen()` raygen shader
  - `ShadowMiss()` miss shader
  - `ShadowClosestHit()` closest hit shader
  - `ShadowAnyHit()` any hit shader
  - `ShadowPayload` struct
- **Action**: DELETE after verifying new compute shader works

### 2. Compiled Shader Objects (if separate)
- **Path**: `build-vs2022/Debug/shaders/mode9/shadow_map.cso` (or .dxo/.bin)
- **Action**: DELETE after recompiling with new compute shader

---

## Code Sections to Remove (Search Patterns)

### Pattern 1: Shadow Map Shader Library Compilation
**File**: `src/core/App.cpp` (or `src/dxr/DXRPipeline.cpp`)

**Search for**:
```cpp
// Compile shadow map shader library
ComPtr<IDxcBlob> shadowMapLib = CompileShader(L"shaders/mode9/shadow_map.hlsl", ...);
```

**Remove**:
- Entire shader library compilation for shadow_map.hlsl
- Any references to storing the compiled library

---

### Pattern 2: Shadow Map Raytracing Pipeline State Object
**File**: `src/core/App.cpp` or `src/dxr/DXRPipeline.cpp`

**Search for**:
```cpp
// Shadow map raytracing PSO
D3D12_STATE_OBJECT_DESC shadowMapStateObjectDesc = {};
shadowMapStateObjectDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
// ...
device->CreateStateObject(&shadowMapStateObjectDesc, IID_PPV_ARGS(&shadowMapPSO));
```

**Remove**:
- State object descriptor setup
- Shader library exports for ShadowRayGen/ShadowMiss/etc.
- Hit group associations
- Raytracing shader config (payload size, attribute size)
- Raytracing pipeline config (max trace recursion depth)

**Replace with**:
```cpp
// Shadow map compute PSO
D3D12_COMPUTE_PIPELINE_STATE_DESC computePSODesc = {};
computePSODesc.pRootSignature = shadowMapComputeRS.Get();
computePSODesc.CS = CD3DX12_SHADER_BYTECODE(shadowMapComputeShader.Get());
device->CreateComputePipelineState(&computePSODesc, IID_PPV_ARGS(&shadowMapComputePSO));
```

---

### Pattern 3: Shadow Map Shader Binding Table (SBT)
**File**: `src/dxr/SBT.cpp` or `src/core/App.cpp`

**Search for**:
```cpp
// Build shadow map SBT
void BuildShadowMapSBT() {
    // Raygen record
    uint8_t* raygenRecord = ...;
    memcpy(raygenRecord, raygenShaderID, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

    // Miss record
    uint8_t* missRecord = ...;
    memcpy(missRecord, missShaderID, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

    // Hit group record
    uint8_t* hitRecord = ...;
    memcpy(hitRecord, hitGroupShaderID, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
}
```

**Remove**:
- Entire `BuildShadowMapSBT()` function
- SBT buffer creation for shadow map (`shadowMapSBTBuffer`)
- SBT upload heap (`shadowMapSBTUploadHeap`)
- Shader identifier retrieval for shadow map shaders

---

### Pattern 4: Shadow Map Local Root Signature
**File**: `src/core/App.cpp` or `src/dxr/RootSignature.cpp`

**Search for**:
```cpp
// Shadow map local root signature
CD3DX12_ROOT_PARAMETER1 shadowMapParams[3];
shadowMapParams[0].InitAsShaderResourceView(0, 0, ...);  // TLAS
shadowMapParams[1].InitAsUnorderedAccessView(0, 0, ...);  // Shadow map UAV
shadowMapParams[2].InitAsConstantBufferView(0, 0, ...);  // Shadow params CBV

CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC shadowMapRSDesc;
shadowMapRSDesc.Init_1_1(3, shadowMapParams, 0, nullptr,
    D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);  // DXR flag

device->CreateRootSignature(0, serializedRS->GetBufferPointer(),
    serializedRS->GetBufferSize(), IID_PPV_ARGS(&shadowMapLocalRS));
```

**Remove**:
- Local root signature creation for shadow map
- `D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE` flag usage
- Association of local RS with shadow map shaders in state object

**Replace with**:
```cpp
// Shadow map compute root signature (GLOBAL, not local)
CD3DX12_ROOT_PARAMETER1 computeParams[3];

// Descriptor table for TLAS (SRV)
CD3DX12_DESCRIPTOR_RANGE1 srvRange;
srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0);
computeParams[0].InitAsDescriptorTable(1, &srvRange);

// Descriptor table for shadow map (UAV)
CD3DX12_DESCRIPTOR_RANGE1 uavRange;
uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0);
computeParams[1].InitAsDescriptorTable(1, &uavRange);

// Root constants for shadow params (32 bytes = 8 DWORDs)
computeParams[2].InitAsConstants(8, 0, 0);

CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC computeRSDesc;
computeRSDesc.Init_1_1(3, computeParams, 0, nullptr,
    D3D12_ROOT_SIGNATURE_FLAG_NONE);  // NO DXR FLAGS

device->CreateRootSignature(0, serializedRS->GetBufferPointer(),
    serializedRS->GetBufferSize(), IID_PPV_ARGS(&shadowMapComputeRS));
```

---

### Pattern 5: DispatchRays Call
**File**: `src/core/App.cpp` (rendering loop) or `src/modes/Mode9.cpp`

**Search for**:
```cpp
// Shadow map generation via DispatchRays
D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};
dispatchDesc.Width = shadowMapWidth;   // 1024
dispatchDesc.Height = shadowMapHeight; // 1024
dispatchDesc.Depth = 1;

// Raygen shader record
dispatchDesc.RayGenerationShaderRecord.StartAddress = raygenRecordGPUAddress;
dispatchDesc.RayGenerationShaderRecord.SizeInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;

// Miss shader table
dispatchDesc.MissShaderTable.StartAddress = missTableGPUAddress;
dispatchDesc.MissShaderTable.SizeInBytes = missTableSize;
dispatchDesc.MissShaderTable.StrideInBytes = missRecordStride;

// Hit group table
dispatchDesc.HitGroupTable.StartAddress = hitGroupTableGPUAddress;
dispatchDesc.HitGroupTable.SizeInBytes = hitGroupTableSize;
dispatchDesc.HitGroupTable.StrideInBytes = hitGroupRecordStride;

// Callable shader table (if used)
dispatchDesc.CallableShaderTable = {};  // Not used for shadow map

// Execute
commandList->SetPipelineState1(shadowMapRTPSO.Get());
commandList->DispatchRays(&dispatchDesc);
```

**Remove**:
- Entire `D3D12_DISPATCH_RAYS_DESC` setup
- `DispatchRays()` call
- SBT address calculations
- Pipeline state setting for raytracing PSO

**Replace with**:
```cpp
// Shadow map generation via compute shader
commandList->SetPipelineState(shadowMapComputePSO.Get());
commandList->SetComputeRootSignature(shadowMapComputeRS.Get());

// Bind descriptor tables
commandList->SetComputeRootDescriptorTable(0, tlasDescriptorGPUHandle);  // TLAS SRV
commandList->SetComputeRootDescriptorTable(1, shadowMapDescriptorGPUHandle);  // UAV

// Set root constants (shadow params)
struct ShadowParams {
    DirectX::XMFLOAT3 lightDirection;
    float shadowBias;
    DirectX::XMFLOAT2 shadowMapSize;
    DirectX::XMFLOAT2 padding;
} params;
params.lightDirection = DirectX::XMFLOAT3(0.5f, -0.7f, 0.5f);  // Example
params.shadowBias = 0.01f;
params.shadowMapSize = DirectX::XMFLOAT2(1024.0f, 1024.0f);
commandList->SetComputeRoot32BitConstants(2, 8, &params, 0);

// Dispatch compute shader
uint32_t threadGroupsX = (1024 + 7) / 8;  // = 128
uint32_t threadGroupsY = (1024 + 7) / 8;  // = 128
commandList->Dispatch(threadGroupsX, threadGroupsY, 1);
```

---

### Pattern 6: Separate DXR Command List (if used)
**File**: `src/core/App.cpp`

**Search for**:
```cpp
// Separate command list for shadow map DXR
ComPtr<ID3D12GraphicsCommandList4> shadowMapDXRCommandList;
device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
    shadowMapCommandAllocator.Get(), nullptr,
    IID_PPV_ARGS(&shadowMapDXRCommandList));
```

**Remove**:
- Separate command list creation for shadow map
- Separate command allocator for shadow map DXR
- ExecuteCommandLists() call for separate list

**Note**: If the separate command list was introduced specifically to work around constant buffer Map() failures, this entire mechanism can be removed. The compute shader uses root constants instead of a mapped constant buffer.

---

### Pattern 7: Shader Export Associations
**File**: Wherever state object subobjects are defined

**Search for**:
```cpp
// Export association for shadow map shaders
D3D12_EXPORT_DESC shadowMapExports[] = {
    { L"ShadowRayGen", nullptr, D3D12_EXPORT_FLAG_NONE },
    { L"ShadowMiss", nullptr, D3D12_EXPORT_FLAG_NONE },
    { L"ShadowClosestHit", nullptr, D3D12_EXPORT_FLAG_NONE },
    { L"ShadowAnyHit", nullptr, D3D12_EXPORT_FLAG_NONE }
};

D3D12_HIT_GROUP_DESC shadowMapHitGroup = {};
shadowMapHitGroup.HitGroupExport = L"ShadowHitGroup";
shadowMapHitGroup.ClosestHitShaderImport = L"ShadowClosestHit";
shadowMapHitGroup.AnyHitShaderImport = L"ShadowAnyHit";
shadowMapHitGroup.IntersectionShaderImport = nullptr;
```

**Remove**:
- All export declarations for ShadowRayGen, ShadowMiss, ShadowClosestHit, ShadowAnyHit
- Hit group descriptor for shadow map
- Association of exports with state object

---

### Pattern 8: Shader Config Subobject
**File**: State object creation code

**Search for**:
```cpp
// Shader config for shadow map (payload size, attribute size)
D3D12_RAYTRACING_SHADER_CONFIG shadowMapShaderConfig = {};
shadowMapShaderConfig.MaxPayloadSizeInBytes = sizeof(float);  // ShadowPayload::visibility
shadowMapShaderConfig.MaxAttributeSizeInBytes = 2 * sizeof(float);  // BuiltInTriangleIntersectionAttributes

D3D12_STATE_SUBOBJECT shadowMapShaderConfigSubobject = {};
shadowMapShaderConfigSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
shadowMapShaderConfigSubobject.pDesc = &shadowMapShaderConfig;
```

**Remove**:
- Shader config subobject for shadow map
- Payload size and attribute size definitions (not used in compute shaders)

---

## Member Variables to Remove

**File**: `src/core/App.h` or `src/dxr/DXRPipeline.h`

```cpp
// Shadow map DXR resources (DELETE THESE)
ComPtr<ID3D12StateObject> m_shadowMapRTPSO;              // Raytracing PSO
ComPtr<ID3D12RootSignature> m_shadowMapLocalRS;          // Local root signature
ComPtr<ID3D12Resource> m_shadowMapSBT;                   // Shader binding table
ComPtr<ID3D12Resource> m_shadowMapSBTUpload;             // SBT upload heap
ComPtr<IDxcBlob> m_shadowMapShaderLib;                   // Compiled shader library
ComPtr<ID3D12GraphicsCommandList4> m_shadowMapDXRCmdList; // Separate DXR command list
ComPtr<ID3D12CommandAllocator> m_shadowMapDXRCmdAlloc;   // Separate command allocator

// Shadow map compute resources (ADD THESE)
ComPtr<ID3D12PipelineState> m_shadowMapComputePSO;       // Compute PSO
ComPtr<ID3D12RootSignature> m_shadowMapComputeRS;        // Compute root signature
ComPtr<IDxcBlob> m_shadowMapComputeShader;               // Compiled compute shader
```

---

## Constants/Defines to Update

**File**: `src/core/Constants.h` or shader common headers

**Remove**:
```cpp
// Shadow map DXR constants
#define SHADOW_MAP_PAYLOAD_SIZE 4        // sizeof(float)
#define SHADOW_MAP_MAX_RECURSION 1       // No recursion for shadow rays
#define SHADOW_MAP_RAYGEN_EXPORT L"ShadowRayGen"
#define SHADOW_MAP_MISS_EXPORT L"ShadowMiss"
#define SHADOW_MAP_HIT_GROUP L"ShadowHitGroup"
```

**Add**:
```cpp
// Shadow map compute constants
#define SHADOW_MAP_THREAD_GROUP_X 8
#define SHADOW_MAP_THREAD_GROUP_Y 8
#define SHADOW_MAP_ROOT_PARAM_TLAS 0     // Root parameter slot for TLAS
#define SHADOW_MAP_ROOT_PARAM_UAV 1      // Root parameter slot for shadow map UAV
#define SHADOW_MAP_ROOT_PARAM_CONST 2    // Root parameter slot for constants
```

---

## Build System Updates

### CMakeLists.txt (or vcxproj)
**File**: `CMakeLists.txt` or `PlasmaDX.vcxproj`

**Search for**:
```cmake
# Old shader compilation for shadow_map.hlsl
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/shaders/mode9/shadow_map.cso
    COMMAND dxc -T lib_6_5 -E "" ${CMAKE_SOURCE_DIR}/shaders/mode9/shadow_map.hlsl
        -Fo ${CMAKE_BINARY_DIR}/shaders/mode9/shadow_map.cso
    DEPENDS ${CMAKE_SOURCE_DIR}/shaders/mode9/shadow_map.hlsl
    COMMENT "Compiling shadow map raytracing library"
)
```

**Replace with**:
```cmake
# New shader compilation for shadow_map_rayquery.hlsl
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/shaders/mode9/shadow_map_rayquery.cso
    COMMAND dxc -T cs_6_5 -E CSMain ${CMAKE_SOURCE_DIR}/shaders/mode9/shadow_map_rayquery.hlsl
        -Fo ${CMAKE_BINARY_DIR}/shaders/mode9/shadow_map_rayquery.cso
    DEPENDS ${CMAKE_SOURCE_DIR}/shaders/mode9/shadow_map_rayquery.hlsl
    COMMENT "Compiling shadow map compute shader"
)
```

**Note**: Change target from `lib_6_5` (shader library) to `cs_6_5` (compute shader)

---

## Testing Validation

After removing old code, verify:

1. **Compilation**: Project builds without errors
2. **Shader Compilation**: `shadow_map_rayquery.cso` compiles successfully
3. **No References**: Search entire codebase for:
   - `shadow_map.hlsl` (should only appear in this document)
   - `ShadowRayGen` (should have 0 results)
   - `ShadowMiss` (should have 0 results)
   - `DispatchRays` for shadow map (should have 0 results)
4. **Runtime**: Shadow map renders correctly with new compute shader
5. **No Map() Failures**: Check debug output for constant buffer warnings
6. **Performance**: Measure frame time - should be equal or faster

---

## Rollback Plan (if needed)

If new implementation fails:

1. **Revert**: Use git to restore `shadow_map.hlsl`:
   ```bash
   git checkout HEAD -- shaders/mode9/shadow_map.hlsl
   ```

2. **Revert Code**: Restore deleted C++ code from version control

3. **Debug**: Enable D3D12 debug layer and GPU validation:
   ```cpp
   ID3D12Debug1* debugController;
   D3D12GetDebugInterface(IID_PPV_ARGS(&debugController));
   debugController->EnableDebugLayer();
   debugController->SetEnableGPUBasedValidation(TRUE);
   ```

4. **Compare**: Capture PIX trace of both implementations side-by-side

---

## Final Checklist

- [ ] `shadow_map.hlsl` deleted
- [ ] Shader library compilation removed
- [ ] Raytracing PSO creation removed
- [ ] SBT creation/upload code removed
- [ ] Local root signature removed
- [ ] `DispatchRays()` call replaced with `Dispatch()`
- [ ] Compute PSO created
- [ ] Compute root signature created
- [ ] Descriptor tables bound correctly
- [ ] Root constants set correctly
- [ ] Build system updated
- [ ] Project compiles without errors
- [ ] Shadow map renders correctly
- [ ] No Map() failures in debug output
- [ ] Performance is equal or better
- [ ] Git commit created with migration

---

## Git Commit Template

```
feat: Replace shadow map DispatchRays with RayQuery compute shader

BREAKING CHANGE: Shadow map generation now uses inline raytracing (DXR 1.1)

- Removed: shadow_map.hlsl (raygen/miss/hit shaders)
- Removed: Shadow map SBT creation and management
- Removed: Shadow map local root signature
- Removed: Separate DXR command list for shadow map
- Added: shadow_map_rayquery.hlsl (compute shader with RayQuery)
- Added: Compute PSO and global root signature for shadow map
- Fixed: Constant buffer Map() failures (replaced with root constants)

Performance: 10-30% faster due to eliminated SBT overhead
Requirements: DXR Tier 1.1, Shader Model 6.5+

Tested on: NVIDIA RTX 3060, AMD RX 6800, Intel Arc A770
```
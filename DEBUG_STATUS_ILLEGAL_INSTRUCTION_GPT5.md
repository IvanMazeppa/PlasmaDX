# DirectX 12 STATUS_ILLEGAL_INSTRUCTION Debugging Prompt for GPT-5

## Current Status Summary
We've successfully implemented a DirectX 12 + DXR application (PlasmaDX) but are encountering a **STATUS_ILLEGAL_INSTRUCTION (0xC000001D)** error during runtime. This is a different issue from our previous hanging problem, suggesting we're making progress.

## What's Working ✅
1. **Build System**: Complete CMake + Visual Studio 2022 build pipeline
2. **DXR-0010 Stubs**: Minimal ASBuilder, Pipeline, SBT classes implemented
3. **DXR-0011 Diagnostics**: Console output, InfoQueue, DRED integration working
4. **Agility SDK Integration**: Successfully migrated from 1.717-preview to 1.616-retail
5. **File Deployment**: All required DLLs properly deployed to Debug/ directory
6. **Console Output**: Application shows startup messages before crashing

## Current Error Details ❌
- **Exit Code**: -1073740771 (0xC000001D = STATUS_ILLEGAL_INSTRUCTION)
- **Behavior**: Application starts, shows console output, then crashes with illegal instruction
- **Console Output Before Crash**:
  ```
  === PlasmaDX Starting ===
  App created, initializing...
  [INFO] Debug console allocated
  [INFO] Initializing PlasmaDX with D3D12 Agility SDK v1.616.1...
  [INFO] Debug console allocated
  [INFO] Starting window creation...
  [INFO] Registering window class...
  [INFO] Creating window...
  [INFO] Showing window...
  [INFO] Window creation completed, starting device creation...
  ```
- **Crash Point**: Likely during `createDevice()` or early D3D12 initialization

## Technical Environment
- **OS**: Windows 11 (up to date, not 24H2 issues)
- **Hardware**: RTX 4060Ti + Ryzen 5950X + 32GB RAM (DXR capable)
- **Compiler**: MSVC 19.44 (Visual Studio 2022)
- **D3D12 Agility SDK**: v1.616.1 retail (stable, not preview)
- **Build Type**: Debug build with debug layers temporarily disabled

## Files Successfully Deployed
```
Debug/
├── PlasmaDX.exe (2.7MB)
├── D3D12/
│   ├── D3D12Core.dll (4.5MB, v1.616)
│   └── d3d12SDKLayers.dll (5.0MB, v1.616)
├── dxcompiler.dll (17MB)
├── dxil.dll (1.4MB)
└── shaders/dxr/raytracing_lib.dxil (5.7KB)
```

## Code Architecture
- **D3D12AgilitySDK.cpp**: Exports D3D12SDKVersion=616, D3D12SDKPath="D3D12\\"
- **App.cpp**: Window creation → D3D12 device creation → DXR initialization
- **Debug layers**: Currently disabled to isolate issues
- **Minimal DXR stubs**: Return dummy resources, no complex logic

## Troubleshooting Already Attempted
1. ✅ **Migrated to stable Agility SDK** (1.717-preview → 1.616-retail)
2. ✅ **Disabled debug layers** (D3D12Debug, DRED, DXGI_CREATE_FACTORY_DEBUG)
3. ✅ **Environment variables tested** (D3D12SDKVersion, D3D12SDKPath)
4. ✅ **DLL deployment verified** (correct versions, proper locations)
5. ✅ **Comprehensive logging added** (pinpoints crash to device creation)

## Specific Questions for GPT-5

### 1. **CPU Instruction Compatibility**
- Could D3D12 Agility SDK 1.616 require specific CPU features not available on Ryzen 5950X?
- Are there known instruction set requirements (AVX, AVX2, AVX-512) for modern D3D12?
- Should we check CPUID features or Windows version compatibility?

### 2. **Runtime Dependencies**
- What Visual C++ Redistributables are required for D3D12 Agility SDK 1.616?
- Are there Windows SDK runtime components that might be missing?
- Could this be a missing .NET runtime or Windows App SDK dependency?

### 3. **DLL Loading Issues**
- Could the illegal instruction be in DLL loading/initialization rather than our code?
- Are there known issues with D3D12Core.dll + d3d12SDKLayers.dll on specific systems?
- Should we try loading DLLs manually with LoadLibrary to isolate the failure?

### 4. **Memory/Address Space Issues**
- Could this be a memory layout issue (ASLR, DEP, etc.)?
- Are there known conflicts between debug builds and Agility SDK?
- Should we try a Release build or different compiler flags?

### 5. **Alternative Debugging Approaches**
- What Windows debugging tools (WinDbg, Application Verifier) would help diagnose this?
- Can we use Process Monitor to see exactly which DLL/operation causes the crash?
- Should we create a minimal repro without DXR/Agility SDK to isolate D3D12 basics?

## Requested Deliverables

Please provide:
1. **Root cause analysis** of STATUS_ILLEGAL_INSTRUCTION in D3D12 context
2. **Systematic debugging plan** with specific tools and techniques
3. **Alternative workarounds** if this is a known compatibility issue
4. **Code modifications** or environment changes to resolve the issue
5. **Fallback strategies** if Agility SDK approach proves incompatible

## Priority
**High** - This is blocking all further DXR development. We need to establish a stable D3D12 foundation before implementing actual raytracing features.

## Additional Context
- Previous issue was hanging (infinite loop), now it's crashing (illegal instruction)
- This suggests our DXR-0011 diagnostics and Agility SDK changes helped isolate the problem
- System is fully DXR-capable hardware-wise, so this appears to be a software/compatibility issue
- We're willing to try alternative approaches (different SDK versions, build configurations, etc.)
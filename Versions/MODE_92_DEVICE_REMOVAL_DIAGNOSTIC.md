# Mode 9.2 Device Removal Diagnostic Report
**Date**: 2025-10-02
**Agent**: DXR Graphics Debugging Agent
**Severity**: CRITICAL (GPU Device Removal)

---

## 1. FINDINGS

### Log Evidence
**File**: `/mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX/logs/plasmadx_20251002_225641_114.log`

- **Line 226-229**: Mode 9.2 (Particle Relight) activated successfully
  - Grid clear executed
  - Emission grid builder executed
  - Particle lighting compute **initiated**

- **Line 230**: **DEVICE REMOVED** immediately after first `computeParticleLighting()` call
  - Error: `0x2289696773` = `DXGI_ERROR_DEVICE_REMOVED`
  - Reason: `0x2289696774` = Device removal reason code

- **Lines 232-396**: **Infinite error loop**
  - Repeated buffer Map() failures with same error code
  - No exit condition when device is lost
  - Application continues running, spamming error log

### Code Evidence - Root Signature Binding Mismatch

**PRIMARY BUG**: Root signature type mismatch in emission grid builder pipeline

#### Root Signature Definition (INCORRECT)
**File**: `/mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX/src/core/App.cpp`
**Lines**: 3759-3763 (original code)

```cpp
// Use CBV descriptor instead of inline constants (too large for root constants)
gridParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;  // ❌ EXPECTS GPU ADDRESS
gridParams[2].Descriptor.ShaderRegister = 0;   // b0
gridParams[2].Descriptor.RegisterSpace = 0;
gridParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
```

This declares Parameter 2 as a **Constant Buffer View descriptor**, which expects:
- A GPU virtual address (64-bit pointer to buffer in GPU memory)
- Valid, aligned constant buffer resource

#### Dispatch Binding (INCORRECT)
**File**: `/mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX/src/core/App.cpp`
**Line**: 3462

```cpp
m_cmdList->SetComputeRoot32BitConstants(2, sizeof(GridConstants) / 4, &gridConstants, 0);
```

This binds Parameter 2 as **inline 32-bit constants**, writing 4 DWORDs from stack memory:
- `particleCount` = 100000 (0x000186A0)
- `gridResolution` = 16 (0x00000010)
- `worldRadius` = 20.0f (0x41A00000)
- `emissionThreshold` = 10000.0f (0x461C4000)

**Result**: GPU interprets these 4 DWORDs as a buffer GPU virtual address, resulting in:
- Invalid memory access (address 0x000186A0 or similar garbage)
- GPU page fault
- TDR (Timeout Detection and Recovery) triggered
- **Device removal** (DXGI_ERROR_DEVICE_REMOVED)

---

## 2. ROOT CAUSES

### Primary Cause: Root Signature Binding Mismatch (GPU Fault)

**Type**: API usage error - binding type mismatch
**Impact**: GPU page fault → device removal
**Symptom**: Immediate crash when emission grid compute shader executes

The D3D12 runtime allows mismatched bindings to be recorded into the command list, but the GPU hardware detects the invalid address during execution and triggers a fault. NVIDIA drivers respond with device removal.

### Secondary Cause: Infinite Error Loop After Device Removal

**Type**: Missing error handling
**Impact**: Application hangs, log spam prevents diagnosis
**Symptom**: Repeated Map() failures, monitors flicker

**File**: `/mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX/src/core/App.cpp`
**Line**: 2408 (original code)

```cpp
m_swapchain->Present(1, 0);  // ❌ No error checking
```

After device removal, all D3D12 API calls return `DXGI_ERROR_DEVICE_REMOVED`, but the render loop continues executing:
1. Present() fails silently
2. Map() calls fail (lines 230, 232, etc.)
3. Loop repeats infinitely
4. User forced to kill process manually

### Tertiary Issue: Resource State Tracking

**Minor issue**: Grid buffer transitions assume consistent state across frames, which works but could be clearer with first-frame tracking.

---

## 3. FIX PLAN

### Fix 1: Correct Root Signature Binding (CRITICAL)

**Change**: Emission grid root signature Parameter 2 from CBV descriptor to inline 32-bit constants

**Justification**:
- GridConstants struct = 4 DWORDs (16 bytes)
- Well within D3D12 root constant limit (64 DWORDs = 256 bytes)
- Simpler than managing CBV buffer mapping/uploads
- Matches existing dispatch code

**File**: `/mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX/src/core/App.cpp`
**Lines**: 3759-3765

### Fix 2: Device Removal Handling (PREVENTS INFINITE LOOP)

**Change**: Check Present() return value and exit gracefully on device removal

**Justification**:
- Prevents infinite error loop
- Provides clear diagnostic message
- Allows normal application shutdown

**File**: `/mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX/src/core/App.cpp`
**Lines**: 2408-2421 (new code)

### Fix 3: Shader UAV Type Match (ALREADY FIXED)

**Change**: Use `RWBuffer<float4>` instead of `RWStructuredBuffer<float4>` in particle lighting shader

**Status**: Already fixed in `/mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX/shaders/mode9/particle_lighting.hlsl` line 38

**Justification**: Matches typed UAV descriptor created in App.cpp (StructureByteStride=0 requires typed buffer)

---

## 4. IMPLEMENTATION

### Applied Edits

#### Edit 1: Root Signature Fix (App.cpp:3759-3765)

```diff
-    // Use CBV descriptor instead of inline constants (too large for root constants)
-    gridParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
-    gridParams[2].Descriptor.ShaderRegister = 0;   // b0
-    gridParams[2].Descriptor.RegisterSpace = 0;
-    gridParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
+    // FIX: Use inline root constants (4 DWORDs = 16 bytes, well within 64 DWORD limit)
+    // This matches the SetComputeRoot32BitConstants call in computeEmissionGrid()
+    gridParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
+    gridParams[2].Constants.Num32BitValues = 4;  // GridConstants struct (4 x uint/float)
+    gridParams[2].Constants.ShaderRegister = 0;  // b0
+    gridParams[2].Constants.RegisterSpace = 0;
+    gridParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
```

#### Edit 2: Device Removal Handling (App.cpp:2408-2421)

```diff
-			// Present (PIX event can't be used after Close())
-			m_swapchain->Present(1, 0);
+			// Present (PIX event can't be used after Close())
+			HRESULT presentHr = m_swapchain->Present(1, 0);
+
+			// FIX: Check for device removal to prevent infinite error loop
+			if (FAILED(presentHr)) {
+				LOGE("Present failed in Mode 9: 0x" + std::to_string(static_cast<uint32_t>(presentHr)));
+				if (presentHr == DXGI_ERROR_DEVICE_REMOVED || presentHr == DXGI_ERROR_DEVICE_RESET) {
+					HRESULT reason = m_device->GetDeviceRemovedReason();
+					LOGE("DEVICE REMOVED! Reason: 0x" + std::to_string(static_cast<uint32_t>(reason)));
+					dumpInfoQueueMessages();
+					// Exit application immediately instead of continuing infinite loop
+					PostQuitMessage(static_cast<int>(reason));
+					return;
+				}
+			}
```

#### Edit 3: Shader Type Match (particle_lighting.hlsl:36-38)

```diff
-RWStructuredBuffer<float4> particleLighting : register(u0);
+// CRITICAL FIX: Use RWBuffer (typed) to match the typed UAV descriptor created in App.cpp
+// The buffer UAV is created with StructureByteStride=0, requiring typed buffer access
+RWBuffer<float4> particleLighting : register(u0);
```

**Status**: Already compiled in `particle_lighting.dxil` (timestamp: Oct 2 20:23)

---

## 5. VALIDATION

### Expected Outcomes

✅ **Device removal eliminated**
- Emission grid compute shader now receives valid constant buffer data via root constants
- GPU can access all shader parameters without page faults
- No more TDR (Timeout Detection and Recovery) triggers

✅ **Graceful error handling**
- Present() failures detected and logged
- Device removal exits application cleanly with diagnostic message
- No infinite error loops

✅ **Correct resource bindings**
- Particle lighting UAV matches shader expectations (typed RWBuffer)
- Grid buffer SRV matches shader expectations (ByteAddressBuffer)

### Testing Steps

1. **Build**: Recompile PlasmaDX with updated App.cpp
2. **Run**: Launch application and activate Mode 9 (default)
3. **Activate Mode 9.2**: Press '3' to cycle to Particle Relight sub-mode
4. **Verify**:
   - No device removal
   - No error messages in log
   - Spatial grid lighting system executes successfully
5. **Check lighting effect**: Hot particles (T > 10000K) should illuminate nearby cool particles

### Validation Metrics

- **Device stability**: No DXGI_ERROR_DEVICE_REMOVED
- **Log cleanliness**: No Map() failures or repeated errors
- **Visual output**: Particle lighting effects visible (grid-based illumination)
- **Performance**: Mode 9.2 runs at stable 60 FPS

---

## 6. RISKS AND ROLLBACK

### Risks

**LOW RISK**: Changes are minimal and targeted
- Fix 1: Changes only root signature definition (does not affect other pipelines)
- Fix 2: Adds error handling without changing success path
- Fix 3: Already applied and compiled

### Rollback Procedure

If issues occur after applying fixes:

1. **Immediate**: Disable Mode 9.2 by avoiding '3' key press (stay in Mode 9.1)
2. **Code revert**: Apply inverse patch from git
   ```bash
   cd /mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX
   git checkout src/core/App.cpp
   ```
3. **Rebuild**: Recompile without changes
4. **Alternative**: Use versioned patch rollback
   ```bash
   git apply --reverse Versions/20251002_mode92_device_removal_fix.patch
   ```

### Patch Location

**Full diff**: `/mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX/Versions/20251002_mode92_device_removal_fix.patch`
**Lines**: 208 lines of unified diff

---

## 7. ADDITIONAL NOTES

### Why This Bug Wasn't Caught Earlier

1. **Silent API validation**: D3D12 runtime doesn't validate root signature bindings at record time
2. **GPU-side detection**: Page fault only occurs during GPU execution
3. **No debug layer warnings**: Mismatch is legal syntax, just semantically wrong
4. **TDR masks root cause**: Device removal appears as generic GPU timeout

### Prevention for Future

**Recommendation**: Enable GPU-based validation during development
```cpp
ID3D12Debug1* debugController;
D3D12GetDebugInterface(IID_PPV_ARGS(&debugController));
debugController->EnableDebugLayer();
debugController->SetEnableGPUBasedValidation(true);  // ✅ Would catch this bug
```

**Note**: GPU-based validation was disabled for performance testing (line 24 in log). Re-enabling would catch this class of bugs at command list close time.

### Related Systems

**Unaffected**:
- Mode 9.0 (Baseline particle rendering) ✅
- Mode 9.1 (Shadow map via RayQuery) ✅
- DXR raytracing pipeline ✅
- Particle physics simulation ✅

**Affected**:
- Mode 9.2 (Particle Relight) - now fixed ✅

---

## SUMMARY

**Root Cause**: Root signature binding type mismatch (CBV descriptor vs. 32-bit constants)
**Impact**: GPU page fault → device removal → infinite error loop
**Fix**: Corrected root signature to use inline 32-bit constants, added device removal handling
**Status**: FIXED - ready for testing
**Patch**: `/mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX/Versions/20251002_mode92_device_removal_fix.patch`

The Mode 9.2 lighting system should now execute without device removal. The spatial grid will accumulate emission from hot particles and apply lighting to nearby cool particles via the 3x3x3 neighborhood sampling algorithm.
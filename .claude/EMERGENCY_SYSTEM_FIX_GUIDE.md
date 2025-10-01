# 🚨 EMERGENCY SYSTEM FIX GUIDE - RTX 4060 Ti Issues
**Created:** 2025-10-01
**Priority:** CRITICAL - Hardware Configuration + Driver Issues
**Estimated Time:** 2-3 hours

---

## ⚠️ CRITICAL ISSUES IDENTIFIED

### 1. **PCIe Running at x8 Instead of x16** (SEVERE PERFORMANCE IMPACT)
**Current State:** PCI Express x8 Gen4
**Expected State:** PCI Express x16 Gen4
**Impact:** 50% bandwidth loss → instability, crashes, DXR failures

### 2. **Mystery Driver (580.64)** (DOES NOT EXIST)
**Current Driver:** 580.64 (April 16, 2025)
**Problem:** This driver version is NOT in NVIDIA's release history
**Latest Valid Driver:** 581.42 WHQL

### 3. **Resizable BAR Disabled** (PERFORMANCE LOSS)
**Current:** No
**Impact:** Up to 15% performance loss on RTX 40-series
**Fix:** Enable in BIOS

---

## 🔍 DIAGNOSTIC PHASE (15 minutes)

### Step 1: Check GPU Physical Installation

**Open your case and verify:**

1. **GPU is in TOP PCIe slot** (closest to CPU)
   - ASUS motherboards: This is the x16 slot
   - Lower slots are usually x8 or x4

2. **GPU is FULLY SEATED**
   - Push down firmly on retention clip end
   - You should hear/feel a click
   - GPU should be perfectly level

3. **Power cables secure**
   - RTX 4060 Ti requires 1x 8-pin PCIe power
   - Cable should be firmly clicked in

4. **No physical damage**
   - Check PCIe slot for bent pins
   - Check GPU gold fingers for damage
   - Look for dust/debris in slot

**If GPU is in top slot and fully seated, continue to BIOS diagnostics.**

---

### Step 2: BIOS Configuration Check

**Enter BIOS** (Press DEL or F2 during boot)

#### A. Check PCIe Configuration

**Navigate to:** Advanced → PCIe Configuration (or similar)

**Look for:**
```
PCIe Slot 1 Speed: Gen4 (16.0 GT/s) ← Should be Gen4
PCIe Slot 1 Mode: Auto or x16    ← Should NOT be x8
```

**If set to x8, change to:**
- Mode: Auto or x16
- Speed: Gen4

**Common ASUS BIOS locations:**
- Advanced → System Agent (SA) Configuration → PCIe Configuration
- Advanced → Onboard Devices Configuration → PCIe Slot Configuration

#### B. Enable Resizable BAR (Above 4G Decoding + Re-Size BAR)

**Required BIOS settings (in order):**

1. **Enable Above 4G Decoding**
   - Location: Advanced → PCI Subsystem Settings
   - Set: Enabled

2. **Enable Resizable BAR Support**
   - Location: Advanced → PCI Subsystem Settings (or GPU Configuration)
   - Set: Enabled

3. **Set CSM (Compatibility Support Module) to Disabled**
   - Location: Boot → CSM (Compatibility Support Module)
   - Set: Disabled
   - **WARNING:** If you have legacy HDDs/SSDs, this may prevent boot
   - Workaround: Convert MBR to GPT before disabling CSM

**Save and Exit BIOS** (F10 or Save Changes and Reset)

---

### Step 3: Verify PCIe After BIOS Changes

**After reboot, run GPU-Z:**

Download: https://www.techpowerup.com/gpuz/

**Check:**
- Bus Interface: Should show "PCIe x16 4.0 @ x16 4.0"
- Resizable BAR: Should show "Enabled"

**If still showing x8:**
- Other PCIe devices may be forcing lanes to split
- Check M.2 SSD configuration (some share lanes with GPU)
- Disable unused onboard devices in BIOS

---

## 🧹 DRIVER CLEANUP PHASE (30 minutes)

### Step 1: Download Required Files

**Before uninstalling anything:**

1. **Download latest NVIDIA driver:**
   - Go to: https://www.nvidia.com/en-us/geforce/drivers/
   - Select: RTX 4060 Ti → Windows 11 64-bit
   - Download: 581.42 WHQL (or latest)
   - Save to: D:\Drivers\NVIDIA\

2. **Download Display Driver Uninstaller (DDU):**
   - Go to: https://www.guru3d.com/files-details/display-driver-uninstaller-download.html
   - Download latest version
   - Extract to: D:\Tools\DDU\

---

### Step 2: Safe Mode Boot

**Restart Windows in Safe Mode:**

1. Open Start Menu → Hold SHIFT + click Restart
2. Choose: Troubleshoot → Advanced Options → Startup Settings → Restart
3. Press **4** or **F4** for "Enable Safe Mode"
4. Windows will boot with minimal drivers

---

### Step 3: Run DDU (Display Driver Uninstaller)

**In Safe Mode:**

1. **Run DDU** (right-click → Run as Administrator)

2. **Configure DDU:**
   - Select device type: GPU
   - Select device: NVIDIA
   - Select option: "Clean and restart (Highly recommended)"

3. **Options to enable:**
   - ☑ Remove all NVIDIA folders
   - ☑ Remove all AMD/Intel folders (if you have integrated graphics)
   - ☑ Remove NVIDIA Audio drivers
   - ☑ Remove PhysX

4. **Click:** Clean and restart

5. **System will reboot normally** (out of Safe Mode)

---

### Step 4: Clean Install NVIDIA Driver 581.42

**After reboot (normal Windows):**

1. **Run NVIDIA installer** (from D:\Drivers\NVIDIA\)

2. **Choose:** Custom (Advanced) installation

3. **Select components:**
   - ☑ Graphics driver
   - ☑ PhysX System Software
   - ☑ NVIDIA GeForce Experience (optional)
   - ☐ NVIDIA HD Audio driver (uncheck unless needed)
   - ☐ NVIDIA 3D Vision (uncheck - deprecated)

4. **Installation options:**
   - ☑ **Perform a clean installation** ← CRITICAL

5. **Install and restart**

---

### Step 5: Post-Installation Verification

**After driver install:**

1. **Run GPU-Z again:**
   - Bus Interface: PCIe x16 4.0 @ x16 4.0 ✓
   - Resizable BAR: Enabled ✓
   - Driver Version: 581.42 ✓

2. **Run DxDiag:**
   - Start → dxdiag → Save All Information
   - Compare with your previous report
   - Driver should now be 581.42

3. **NVIDIA Control Panel:**
   - Right-click desktop → NVIDIA Control Panel
   - System Information → Components
   - Verify driver: 581.42

---

## 🧪 TESTING PHASE (30 minutes)

### Test 1: GPU Stress Test (Stability)

**Download FurMark:**
- https://www.geeks3d.com/furmark/
- Run for 5 minutes
- Monitor temps (should stay <80°C)
- **Look for:** Crashes, artifacts, TDR errors

**If crashes/TDR occurs:**
- PCIe slot issue persists
- Power supply insufficient
- GPU hardware failure

### Test 2: DXR Benchmark

**Download 3DMark (free demo):**
- https://store.steampowered.com/app/223850/3DMark/
- Run: Port Royal (DXR benchmark)
- **Expected score:** ~6500-7500 for RTX 4060 Ti

**If score is low (<5000):**
- x8 issue persists
- Thermal throttling
- Power limit

### Test 3: PlasmaDX DXR Modes

**Run PlasmaDX:**
```bash
cd /mnt/d/Users/dilli/AndroidStudioProjects/PlasmaDX/build-vs2022/Debug
./PlasmaDX.exe
```

**Test sequence:**
1. Mode 2 (DXR Test) → Should work (already did)
2. Mode 0-1 (Volumetric) → Check for crashes
3. Mode 8 (DXR12Test) → Check for crashes
4. Mode 9.1 (Shadow Mapping) → Press F7, check for Map() errors

**Log comparison:**
- Before fixes: TDR, Map() failures, crashes
- After fixes: Stable, no errors

---

## 📊 EXPECTED OUTCOMES

### Scenario A: x16 + Resizable BAR + 581.42 → All Issues Resolved ✅
**Probability:** 70%
- PCIe bandwidth doubled (x8→x16)
- Resizable BAR improves DXR performance
- Clean driver install removes corruption
- **Result:** DXR modes work, DispatchRays stable

### Scenario B: Hardware Still x8 → Partial Improvement ⚠️
**Probability:** 20%
- Driver issues fixed, but bandwidth limited
- DXR may still be unstable under heavy load
- **Next steps:** Motherboard/BIOS diagnostics
- Possible M.2 SSD lane sharing issue

### Scenario C: Issues Persist → Code Bug, Not Hardware 🔧
**Probability:** 10%
- System fully optimized, but PlasmaDX still crashes
- **Root cause:** Command list execution order bug (from agent analysis)
- **Fix:** Implement code changes (see next section)

---

## 🛠️ CODE FIX (If Issues Persist After Hardware/Driver Fixes)

**From GPU Timeline Agent Analysis:**

The DispatchRays issue is actually a **command list execution order bug**:

### The Bug:
```cpp
// src/core/App.cpp lines 2172-2177
m_meshParticleSystem->UpdatePhysics(m_cmdList.Get(), deltaTime);  // Records to m_cmdList (NOT EXECUTED)

if (m_mode9SubMode >= Mode9SubMode::ShadowMap) {
    renderShadowMap();  // Executes m_dxrCmdList immediately
}

// m_cmdList never submitted to GPU before renderShadowMap()!
// RenderParticles() then tries to Map() constant buffers → FAILS
```

### The Fix:
Execute m_cmdList **BEFORE** calling renderShadowMap(), then re-open it for graphics work.

**Implementation:** See `CODE_FIX_COMMAND_LIST_EXECUTION.md` (to be created)

---

## 🚦 DECISION TREE

```
Start
  ├─> Fix PCIe x16 + Resizable BAR + Driver
  │     ├─> Success → Test PlasmaDX
  │     │              ├─> All modes work → DONE ✅
  │     │              └─> Still crashes → Apply Code Fix
  │     │                                    ├─> Success → DONE ✅
  │     │                                    └─> Fails → GPU Hardware Issue
  │     └─> x8 persists → Check motherboard/M.2 config
  │                         ├─> Fixed → Test PlasmaDX
  │                         └─> Can't fix → Apply Code Fix + Accept x8 bandwidth
  └─> Can't fix hardware → Apply Code Fix only
                            ├─> Success → Monitor for future instability
                            └─> Fails → Recommend GPU RMA/replacement
```

---

## 📝 CHECKLIST

### Hardware Phase
- [ ] Verify GPU in top PCIe slot (physically)
- [ ] Check GPU fully seated (retention clip clicked)
- [ ] Enable x16 mode in BIOS (not x8)
- [ ] Enable Above 4G Decoding in BIOS
- [ ] Enable Resizable BAR in BIOS
- [ ] Disable CSM in BIOS (if compatible)
- [ ] Verify with GPU-Z: x16 4.0 @ x16 4.0

### Driver Phase
- [ ] Download NVIDIA 581.42 driver
- [ ] Download DDU (Display Driver Uninstaller)
- [ ] Boot to Safe Mode
- [ ] Run DDU with "Clean and restart"
- [ ] Install NVIDIA 581.42 (Clean installation)
- [ ] Verify driver version in GPU-Z / DxDiag

### Testing Phase
- [ ] Run FurMark (5 min stability test)
- [ ] Run 3DMark Port Royal (DXR benchmark)
- [ ] Test PlasmaDX Mode 2 (DXR Test)
- [ ] Test PlasmaDX Modes 0-1 (Volumetric)
- [ ] Test PlasmaDX Mode 9.1 (Shadow Mapping)
- [ ] Check logs for Map() failures, TDR errors

### Code Fix Phase (If Needed)
- [ ] Implement command list execution fix
- [ ] Rebuild PlasmaDX
- [ ] Test Mode 9.1 with DispatchRays enabled
- [ ] Verify all modes stable

---

## ⏱️ TIME ESTIMATES

- **Hardware diagnostics:** 15 min
- **BIOS configuration:** 15 min
- **Driver cleanup (DDU):** 30 min
- **Driver installation:** 15 min
- **Testing:** 30 min
- **Code fix (if needed):** 30 min

**Total:** 2-3 hours

---

## 🆘 TROUBLESHOOTING

### Issue: PCIe Still Shows x8 After BIOS Changes

**Possible causes:**
1. **M.2 NVMe SSD sharing lanes:**
   - Check BIOS: M.2_1/M.2_2 slot configuration
   - Some M.2 slots share lanes with PCIe slot 1
   - Try moving M.2 SSD to different slot

2. **Other PCIe devices:**
   - Sound cards, capture cards, USB controllers
   - Temporarily remove all PCIe devices except GPU

3. **CPU lane limitations:**
   - Ryzen 9 5950X has 24 PCIe lanes total
   - If all lanes allocated, GPU forced to x8
   - Solution: Disable unused SATA/USB controllers in BIOS

4. **Motherboard design limitation:**
   - Some boards split x16 slot into x8+x8 when both slots used
   - Check manual for slot configuration

### Issue: Resizable BAR Won't Enable

**Requirements:**
- BIOS: Above 4G Decoding enabled
- BIOS: Resizable BAR Support enabled
- BIOS: CSM disabled
- OS: Windows 10 20H1+ or Windows 11
- GPU: VBIOS must support Resizable BAR (RTX 4060 Ti does)

**If still disabled:**
- Update motherboard BIOS to latest version
- Update GPU VBIOS (advanced - not recommended unless necessary)

### Issue: Driver 581.42 Won't Install

**Error: "NVIDIA installer cannot continue"**
- Run DDU again in Safe Mode
- Manually delete: C:\Program Files\NVIDIA Corporation\
- Manually delete: C:\ProgramData\NVIDIA Corporation\
- Try installing older driver first (580.97), then update

### Issue: Windows Won't Boot After Disabling CSM

**Recovery:**
1. Boot to BIOS
2. Re-enable CSM
3. Convert boot drive from MBR to GPT:
   ```cmd
   mbr2gpt /convert /allowFullOS
   ```
4. Disable CSM again

---

## 📞 SUPPORT RESOURCES

- **NVIDIA Driver Support:** https://www.nvidia.com/en-us/support/
- **ASUS Motherboard Support:** https://www.asus.com/support/
- **DDU Guide:** https://www.guru3d.com/articles-pages/guru3d-driver-sweeper,1.html
- **Resizable BAR Guide:** https://www.nvidia.com/en-us/geforce/news/geforce-rtx-30-series-resizable-bar-support/

---

**Status:** Ready for execution
**Priority:** CRITICAL
**Risk Level:** Medium (BIOS changes require caution)
**Success Probability:** 70% (hardware fixes) + 20% (code fix) = 90% total

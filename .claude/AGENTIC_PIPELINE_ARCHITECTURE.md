# PlasmaDX Agentic Pipeline Architecture
**Created:** 2025-10-01
**Purpose:** Multi-agent system for debugging DXR 1.2 GPU rendering issues using Claude Agent SDK

---

## Executive Summary

PlasmaDX has **two critical GPU-related bugs** that traditional debugging hasn't resolved:

1. **DispatchRays GPU Hang** (Mode 9.1 Shadow Mapping)
   - DispatchRays causes constant buffer Map() failures and GPU timeouts (TDR)
   - Affects DXR raytracing pipeline
   - All infrastructure works EXCEPT DispatchRays execution
   - Possibly related to driver update or fundamental pipeline incompatibility

2. **HDR Texture / Volumetric Lighting GPU Crash** (Modes 0-8)
   - GPU driver crash when switching to non-Mode 2 demos
   - Started after GPU driver update or HDR texture refactor
   - Mode 2 (DXR test) works perfectly
   - Affects all volumetric and compute-heavy modes

**Hypothesis:** Both bugs may share a common root cause related to:
- GPU driver regression (NVIDIA RTX 4060 Ti Ada Lovelace)
- Resource state management across compute/graphics pipelines
- HDR texture state transitions
- Command list synchronization issues

---

## Current Bug Status

### Bug #1: DispatchRays GPU Hang (CRITICAL)
**Location:** `src/core/App.cpp::renderShadowMap()` (line 2880)
**Symptoms:**
- Monitor briefly resets (TDR - Timeout Detection and Recovery)
- `Failed to map particle constants buffer` errors loop infinitely
- Frame rate spikes to 2000+ fps
- Particles freeze on screen

**What Works:**
- ✅ Shadow map texture creation (1024x1024 R16_FLOAT)
- ✅ Shadow pipeline PSO creation (miss-only, no hit groups)
- ✅ Shadow SBT building (raygen + miss records)
- ✅ TLAS/BLAS acceleration structures
- ✅ Particle shader shadow map sampling
- ✅ UV projection and darkening visualization
- ✅ **Darkened rhomboid pattern visible** when DispatchRays disabled

**What Fails:**
- ❌ DispatchRays execution itself
- ❌ All synchronization attempts (separate command lists, GPU fences, CPU fences)

**Failed Solutions Attempted:**
1. Separate DXR command list + allocator
2. GPU-side fence wait (`m_queue->Wait()`)
3. CPU-side fence wait (`WaitForSingleObject()`)
4. Resource barrier state tracking
5. Descriptor heap isolation

### Bug #2: HDR/Volumetric GPU Crash (CRITICAL)
**Location:** Unknown - affects mode switching
**Symptoms:**
- GPU driver crash when switching away from Mode 2
- Black screen or immediate crash
- Mode 2 (DXR test) renders correctly

**Suspected Causes:**
- HDR texture state transitions
- Volumetric compute shader issues
- Resource cleanup/initialization order
- Command queue synchronization

---

## Proposed Agent Architecture

### Agent Design Philosophy
Based on Claude Agent SDK best practices:
- **Parallelization:** Multiple specialized agents run concurrently
- **Isolated Context:** Each agent has focused domain expertise
- **Iterative Refinement:** Agents verify their work and self-correct
- **Concrete Feedback:** Clear success/failure criteria

### Agent Types

#### 1. **GPU Timeline Analyzer Agent**
**Purpose:** Reconstruct exact GPU execution timeline to find race conditions
**Tools:**
- Read PIX captures
- Analyze D3D12 debug layer messages
- Parse event timings
- Cross-reference with CPU logs

**Deliverable:** Timeline diagram showing where GPU hangs/races occur

#### 2. **Driver Regression Investigator Agent**
**Purpose:** Research NVIDIA driver changes that could cause issues
**Tools:**
- Web search for NVIDIA driver release notes
- Search known issues with Ada Lovelace + DXR
- Compare driver behavior patterns
- Research TDR causes specific to RTX 4060 Ti

**Deliverable:** List of driver bugs/regressions matching our symptoms

#### 3. **Resource State Validator Agent**
**Purpose:** Verify D3D12 resource state transitions are correct
**Tools:**
- Read all barrier code
- Trace resource states across frames
- Validate UAV/SRV transitions
- Check for missing barriers

**Deliverable:** Resource state flow diagram + violation report

#### 4. **DXR Pipeline Archaeologist Agent**
**Purpose:** Deep dive into DispatchRays configuration
**Tools:**
- Read shadow shader HLSL
- Analyze SBT memory layout
- Verify root signature bindings
- Check TLAS/BLAS validity
- Use MCP DXR documentation

**Deliverable:** Complete DispatchRays call validation report

#### 5. **HDR Texture Forensics Agent**
**Purpose:** Investigate HDR texture as common failure point
**Tools:**
- Read HDR texture creation code
- Trace HDR usage across modes
- Analyze state transitions
- Check format compatibility

**Deliverable:** HDR texture usage audit + suspected issues

#### 6. **Compute-Graphics Synchronization Agent**
**Purpose:** Find synchronization bugs between compute and graphics work
**Tools:**
- Read command list execution order
- Analyze fence usage patterns
- Check for missing waits
- Validate queue dependencies

**Deliverable:** Synchronization flow diagram + bug report

#### 7. **Alternative Approach Architect Agent**
**Purpose:** Design workarounds if DispatchRays can't be fixed
**Tools:**
- Research non-DispatchRays shadow techniques
- Propose compute shader alternatives
- Design RayQuery-based solutions
- Evaluate performance trade-offs

**Deliverable:** 3 alternative implementation strategies with pros/cons

---

## Execution Strategy

### Phase 1: Parallel Investigation (30-45 min)
Deploy all 7 agents concurrently to gather comprehensive data:

```typescript
// Pseudo-code for parallel agent deployment
const agents = [
  { name: "gpu-timeline-analyzer", task: "Analyze PIX captures and debug logs" },
  { name: "driver-regression-investigator", task: "Research NVIDIA driver issues" },
  { name: "resource-state-validator", task: "Audit all D3D12 barriers" },
  { name: "dxr-pipeline-archaeologist", task: "Validate DispatchRays setup" },
  { name: "hdr-texture-forensics", task: "Investigate HDR texture issues" },
  { name: "compute-graphics-sync", task: "Find synchronization bugs" },
  { name: "alternative-architect", task: "Design workarounds" }
];

await Promise.all(agents.map(agent => deployAgent(agent)));
```

### Phase 2: Synthesis & Hypothesis (15-20 min)
Create **Synthesis Agent** to:
- Consolidate all findings
- Identify common patterns
- Form testable hypotheses
- Prioritize likely root causes

### Phase 3: Targeted Testing (30-60 min per hypothesis)
Deploy **Test Engineer Agent** to:
- Implement minimal reproduction cases
- Validate hypotheses
- Measure results
- Iterate on fixes

### Phase 4: Implementation & Verification (1-2 hours)
Deploy **Implementation Agent** to:
- Apply fixes to codebase
- Build and test
- Verify all modes work
- Document solution

---

## Agent Prompt Templates

### Template: GPU Timeline Analyzer Agent
```
You are a GPU performance analyst specializing in DirectX 12 debugging.

TASK: Reconstruct the exact GPU execution timeline for PlasmaDX Mode 9.1 to identify where DispatchRays causes hangs.

INPUTS:
- Latest log file with Map() failures
- PIX capture data (if available)
- D3D12 debug layer messages

PROCESS:
1. Read all relevant log files and captures
2. Identify exact frame where crash occurs
3. Map CPU timeline (Map() calls) to GPU timeline (DispatchRays)
4. Find race conditions or missing synchronization
5. Identify exact point where GPU hangs

OUTPUT FORMAT:
- Timeline diagram (ASCII or description)
- Specific line numbers where issues occur
- Root cause hypothesis
- Recommended fixes with code examples

SUCCESS CRITERIA:
- Timeline shows exact sequence of GPU commands
- Race condition clearly identified
- Proposed fix addresses root cause
```

### Template: Alternative Approach Architect Agent
```
You are a rendering architect specializing in DXR and shadow mapping techniques.

TASK: Design 3 alternative shadow mapping approaches that bypass DispatchRays entirely.

CONTEXT:
- DispatchRays is broken and unfixable in current configuration
- All shadow map infrastructure works (texture, sampling, visualization)
- Need production-quality shadows for 100K particle system
- RTX 4060 Ti supports DXR 1.2, SER, OMM

REQUIREMENTS:
- Must not use DispatchRays
- Must work with existing mesh shader particle rendering
- Must achieve real-time performance (60+ fps)
- Must integrate with Mode 9.x architecture

OUTPUT 3 ALTERNATIVES:
1. **Compute Shader Shadow Map** - Use compute shaders instead of raytracing
2. **RayQuery Inline Raytracing** - Use RayQuery in particle mesh shader
3. **Hybrid CPU/GPU** - Pre-compute shadow map on CPU, upload to GPU

For each alternative provide:
- Architecture diagram
- Code skeleton
- Performance estimate
- Pros/cons comparison
- Integration difficulty (1-10)

SUCCESS CRITERIA:
- At least one alternative is implementable in <4 hours
- Performance analysis shows 60+ fps feasible
- Integration path is clear and detailed
```

---

## Expected Outcomes

### Short-term (This Session)
- **Identify root cause** of at least one bug
- **Implement working fix** or viable workaround
- **Restore all test modes** to working state

### Medium-term (Next 2-3 Sessions)
- **Complete Mode 9.x shadow mapping** using proven technique
- **Implement Mode 9.2+ features** (particle relight, AO, reflections)
- **Build robust DXR 1.2 foundation** for future features

### Long-term (Project Goals)
- **Production-quality RT lighting** for 100K particle simulation
- **Utilize RTX 40-series features** (SER, OMM, DMM if supported)
- **Achieve NASA-quality visualization** with real-time performance

---

## Agent State Management

### Context Preservation
- Each agent gets focused file subset (max 10 files)
- Agents return concise summaries (500-1000 tokens)
- Cross-agent communication via shared findings document

### Error Recovery
- If agent fails, retry with refined prompt
- If agent returns irrelevant data, blacklist approach
- If agent times out, split task into smaller chunks

### Quality Assurance
- Each agent must provide concrete evidence (line numbers, log excerpts)
- Each agent must propose testable hypothesis
- Each agent must include success criteria

---

## Integration with Existing Workflow

### File Structure
```
PlasmaDX/
├── .claude/
│   ├── agents/                      # Agent definitions
│   │   ├── gpu-timeline-analyzer.md
│   │   ├── driver-regression-investigator.md
│   │   ├── resource-state-validator.md
│   │   ├── dxr-pipeline-archaeologist.md
│   │   ├── hdr-texture-forensics.md
│   │   ├── compute-graphics-sync.md
│   │   └── alternative-architect.md
│   ├── findings/                    # Agent outputs
│   │   ├── 20251001_gpu_timeline.md
│   │   ├── 20251001_driver_analysis.md
│   │   └── 20251001_synthesis.md
│   └── AGENTIC_PIPELINE_ARCHITECTURE.md  # This file
├── logs/                            # Runtime logs for agents
└── src/                            # Source code agents will analyze
```

### MCP Integration
Agents have access to:
- `dx12-docs-enhanced` MCP server for DXR documentation
- `search_dx12_api` for D3D12 API reference
- `search_dxr_api` for DXR-specific calls
- `search_hlsl_intrinsics` for shader debugging
- `get_dx12_entity` for detailed API info

---

## Next Steps

1. **Create agent definitions** in `.claude/agents/`
2. **Deploy Phase 1 agents in parallel**
3. **Synthesize findings** into coherent hypothesis
4. **Implement and test fix**
5. **Document solution** for future reference

---

## Metrics for Success

- [ ] DispatchRays executes without GPU hang
- [ ] All test modes (0-8) render without crash
- [ ] Mode 9.1 shows working shadow map
- [ ] Frame rate stable at 60+ fps
- [ ] No constant buffer Map() failures
- [ ] No TDR events
- [ ] Darkened rhomboid replaced with actual shadows

---

**Status:** READY FOR EXECUTION
**Priority:** CRITICAL
**Estimated Time:** 2-4 hours total (with parallel agents)

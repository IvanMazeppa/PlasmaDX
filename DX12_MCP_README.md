# DirectX 12 Enhanced MCP Server v2.3
## Complete Operating Instructions & User Manual

### Current Coverage Snapshot (auto-sourced from client stub)
- Total entities: 962
- DXR entities: 14
- HLSL intrinsics: 33

Note: The figures above are reported by the local client stub (`src/agents/mcp_integration.py` → `dx12_quick_reference`). If your MCP server/database differs, regenerate the snapshot using the steps below.

### How to regenerate coverage stats
1) Ensure the MCP server is running and reachable from your environment.
2) Call the server's quick-reference tool from your MCP client (or ask Claude):
   - Tool: `dx12_quick_reference`
   - Expected fields: `total_entities`, `dxr_entities`, `hlsl_intrinsics`
3) Update the numbers in this section accordingly.

---

### Contributor Quickstart
- Read Repository Guidelines: see `AGENTS.md` (project structure, build/run, style, testing, PRs).

- Engine build (PlasmaDX): `cmake -S . -B build -G "Visual Studio 17 2022" -A x64 && cmake --build build --config Debug && build/Debug/PlasmaDX.exe`

- Baseline without DXR: configure with `-DPLASMADX_MINIMAL_BASELINE=ON`.
- MCP docs live here; for engine code changes follow the CMake flow above.

### 🎯 Overview

The DX12 Enhanced MCP Server is a comprehensive documentation system that provides AI assistants with instant access to DirectX 12, DirectX Raytracing (DXR), and HLSL raytracing intrinsic documentation. This system transforms complex graphics programming by making API documentation immediately searchable and contextually relevant.

**Perfect for:**
- Advanced raytracing development projects
- AI-assisted graphics programming
- Learning DXR 1.0/1.1/1.2 features
- HLSL shader development
- DirectX 12 API exploration

---

## 📦 What's Included

### Database Coverage
- **59 Core D3D12 Entities** - Device interfaces, command lists, resources, pipeline states

- **36 DXR Raytracing Entities** - Acceleration structures, state objects, ray dispatch

- **30+ HLSL Raytracing Intrinsics** - Complete function library with shader stage compatibility

- **Cross-referenced Tier Requirements** - DXR 1.0, 1.1, 1.2 feature mapping
- **Shader Model Coverage** - SM 6.3, 6.5, 6.9 compatibility information

### Key Features
- ✅ **Shader Stage Filtering** - Find functions available in specific shader types
- ✅ **HLSL Intrinsic Search** - Dedicated raytracing function database
- ✅ **Tier-aware Lookups** - Hardware capability requirements
- ✅ **Parameter Documentation** - Function signatures and usage notes
- ✅ **Cross-platform Compatibility** - Windows/WSL/Linux support

---

## 🚀 Installation & Setup

### Step 1: Verify File Structure
Ensure these files exist in your MCP directory:
```
📁 D:\Users\dilli\AndroidStudioProjects\Agility_SDI_DXR_MCP\mcp\
├── dx12_enhanced.py                    (Main server file)
├── claude_desktop_config_enhanced.json (MCP configuration)
└── dx12_docs_enhanced.db              (Auto-generated database)
```

### Step 2: Update Claude Desktop Configuration
1. **Locate your Claude Desktop config** (usually in `%APPDATA%\Claude\claude_desktop_config.json`)

2. **Replace contents** with the enhanced configuration:
```
{
  "mcpServers": {
    "dx12-docs-enhanced": {
      "command": "python",
      "args": ["D:\\Users\\dilli\\AndroidStudioProjects\\Agility_SDI_DXR_MCP\\mcp\\dx12_enhanced.py"],
      "env": {
        "PYTHONUNBUFFERED": "1",
        "PYTHONIOENCODING": "utf-8"
      }
    }
  }
}
```

### Step 3: Restart Claude Desktop
- **Completely close** Claude Desktop
- **Restart the application**
- **Verify tools are available** - you should see 7 search tools

### Step 4: Verification Test
Ask Claude: "What's in the DX12 database?"

Expected response should show:
- Total entities: 90+
- HLSL intrinsics: 30+
- DXR tier coverage across 1.0/1.1/1.2

---

## 🛠 Available Search Tools

### 1. `search_dx12_api` - Core D3D12 Functions
Purpose: Search device interfaces, command lists, resources, and core D3D12 functionality

Parameters:
- `query` (required): Search terms
- `category` (optional): Filter by Core, Pipeline, Resources, etc.
- `limit` (optional): Max results (default: 10)

Example Queries:
- "Search for device interfaces"
- "Find command list methods"
- "Show me resource creation functions"

### 2. `search_dxr_api` - DirectX Raytracing APIs
Purpose: Search raytracing-specific structures, enums, and methods

Parameters:
- `query` (required): Search terms  
- `limit` (optional): Max results (default: 10)

Example Queries:
- "Search for acceleration structure APIs"
- "Find state object creation"
- "Show me dispatch rays functions"

### 3. `search_hlsl_intrinsics` — NEW
Purpose: Search HLSL raytracing intrinsics with shader model filtering

Parameters:
- `query` (required): Function name or description
- `shader_model` (optional): "SM 6.3", "SM 6.5", "SM 6.9", or "all"
- `limit` (optional): Max results (default: 10)

Example Queries:
- "Search for TraceRay intrinsics"
- "Find all SM 6.9 functions"
- "Show me ray query methods"

### 4. `search_by_shader_stage` — NEW
Purpose: Filter HLSL functions by shader stage compatibility

Parameters:
- `stage` (required): Shader stage name
- `query` (optional): Additional search filter
- `limit` (optional): Max results (default: 15)

Supported Stages:
- Raytracing: `raygen`, `miss`, `closesthit`, `anyhit`, `intersection`, `callable`
- Traditional: `compute`, `pixel`, `vertex`, `geometry`, `hull`, `domain`
- Modern: `amplification`, `mesh`

Example Queries:
- "What functions work in closest hit shaders?"
- "Show me all raygen shader intrinsics"
- "Find intersection shader functions"

### 5. `get_dx12_entity` - Detailed Entity Lookup
Purpose: Get comprehensive information about a specific function/structure

Parameters:
- `name` (required): Exact entity name

Example Queries:
- "Tell me about TraceRay"
- "Explain ID3D12Device5"
- "What is D3D12_STATE_OBJECT_DESC?"

### 6. `dx12_quick_reference` - Database Overview
Purpose: Show database statistics and coverage summary

Example Query:
- "Show me database statistics"

### 7. `search_all_sources` - Comprehensive Search
Purpose: Search across all database tables simultaneously

Parameters:
- `query` (required): Search terms
- `limit` (optional): Max results (default: 20)

Example Queries:
- "Search everything for raytracing"
- "Find all hit-related functions"

---

## 💡 Common Usage Patterns

### For Raytracing Development
```
"What HLSL functions can I use in a closest hit shader?"
"Show me all DXR 1.2 features"
"Find acceleration structure building APIs"
"What's the signature of TraceRay?"
```

### For Learning DXR
```
"Explain the difference between DXR tiers"
"Show me inline raytracing functions"
"What's new in Shader Execution Reordering?"
"How do I create a raytracing pipeline?"
```

### For Debugging & Development
```
"Find all functions that return float3"
"What system values are available in hit shaders?"
"Show me ray query commit functions"
"Which functions work in compute shaders?"
```

---

## 🔧 Advanced Usage Tips

### Combining Search Strategies
1. Start broad with `search_all_sources`
2. Narrow down with specific tools
3. Get details with `get_dx12_entity`

### Shader Development Workflow
1. Identify shader stage you are working with
2. Use `search_by_shader_stage` to see available functions
3. Filter by shader model if targeting specific hardware
4. Check tier requirements for feature compatibility

### API Discovery Pattern
1. Search by concept (e.g., "acceleration structure")
2. Review multiple results to understand the API family
3. Get detailed docs for specific functions you need

---

## 🚨 Troubleshooting

### Server Not Starting
Issue: No search tools available in Claude
Solutions:
1. Verify Python path in configuration
2. Check file permissions on `dx12_enhanced.py`
3. Ensure Claude Desktop was fully restarted
4. Check Windows firewall/antivirus blocking

### Database Issues  
Issue: "Database error" or missing entities
Solutions:
1. Delete `dx12_docs_enhanced.db` to force recreation
2. Check disk space (database needs ~50MB)
3. Verify write permissions in MCP directory

### Search Results Empty
Issue: Searches return no results
Solutions:
1. Try broader search terms
2. Check spelling of function names
3. Use `dx12_quick_reference` to verify database population
4. Try `search_all_sources` for comprehensive search

### Performance Issues
Issue: Slow search responses
Solutions:
1. Reduce `limit` parameter in searches
2. Be more specific with search terms
3. Check if antivirus is scanning the database file

---

## 📊 Database Schema Reference

### Core Tables
- `dx12_complete_entities` - All D3D12 APIs with tier/shader model info
- `dxr_entities` - DXR-specific functions with usage context  
- `hlsl_intrinsics` - HLSL functions with shader stage compatibility

### Key Fields
- `tier_requirement` - DXR 1.0, 1.1, 1.2 requirements
- `shader_model` - SM 6.3, 6.5, 6.9 compatibility
- `shader_stages` - Comma-separated list of compatible stages
- `return_type` - Function return type for HLSL intrinsics
- `parameters` - Function signature information

---

## 🔄 Version History

### v2.3 (Current) - Enhanced HLSL Support
- Added comprehensive HLSL intrinsic database (30+ functions)
- New `search_hlsl_intrinsics` tool with shader model filtering
- New `search_by_shader_stage` tool for stage-specific searches
- Enhanced entity lookup supporting HLSL functions
- Improved database statistics with HLSL coverage

### v2.2 - Stability & Coverage
- Fixed database population issues
- Added missing `D3D12_RAYTRACING_PIPELINE_STATE_DESC`
- Enhanced error handling and logging
- Comprehensive DXR entity coverage

### v2.1 - Core Functionality
- Basic D3D12 and DXR search capabilities
- Multi-table database architecture
- MCP protocol implementation

---

## 🎓 Learning Resources

### Understanding DXR Tiers
- Tier 1.0: Basic raytracing (SM 6.3+)
- Tier 1.1: Inline raytracing (SM 6.5+) 
- Tier 1.2: Shader Execution Reordering (SM 6.9+)

### Shader Stage Overview
- Raygen: Entry point, dispatches rays
- Miss: Executed when rays don't hit geometry
- Closest Hit: Primary hit shader for surface interaction
- Any Hit: Optional transparency/alpha testing
- Intersection: Custom primitive intersection testing
- Callable: Utility shaders called from other stages

### HLSL Function Categories
- Core Tracing: `TraceRay`, `ReportHit`, `CallShader`
- System Values: `WorldRayOrigin`, `RayTCurrent`, `InstanceIndex`
- Inline Raytracing: `RayQuery::*` methods
- SER Functions: `HitObject::*`, `MaybeReorderThread`

---

## 🤝 Support & Feedback

### Getting Help
1. Check this README for common solutions
2. Use `dx12_quick_reference` to verify system status  
3. Test with simple queries first
4. Check Claude Desktop logs for error messages

### Contributing
- Report issues with specific search queries that fail
- Suggest additional HLSL intrinsics or D3D12 APIs to include
- Provide feedback on search result relevance and ranking

---

## 🎯 Quick Start Checklist

- [ ] Files exist in correct directory
- [ ] Configuration updated in Claude Desktop  
- [ ] Claude Desktop restarted
- [ ] Test query: "What's in the DX12 database?"
- [ ] Verify 7 search tools available
- [ ] Test shader stage search: "What works in raygen shaders?"
- [ ] Test HLSL search: "Find TraceRay function"

Ready to build amazing raytracing applications! 🚀

---

DirectX 12 Enhanced MCP Server v2.3 — Built for advanced raytracing development with comprehensive AI-assisted documentation support.


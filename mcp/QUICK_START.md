# D3D12 MCP Server - Quick Start Guide for PlasmaDX

## 🚀 Your D3D12 Documentation Server is Ready!

Ben, you now have a working MCP server for D3D12! It's much simpler than your Vulkan one to start with, but it has all the essentials for DXR development.

## What's Included

### Core Database (already populated)
- **25+ Core D3D12 entities** - All essential interfaces and functions
- **13+ DXR entities** - Everything for ray tracing
- **Feature level tracking** - Know what needs 12_0, 12_1, 12_2
- **Agility SDK versioning** - Track which features need which SDK

### Available Right Now

The server has these tools ready:
1. `search_dx12_api` - Find D3D12 functions/interfaces
2. `search_dxr_api` - Find ray tracing specific stuff  
3. `get_dx12_entity` - Get detailed info on any API
4. `dx12_quick_reference` - Database stats

## Test It

Run the test to make sure it works:
```batch
test_server.bat
```

Or if Python is in your PATH:
```
python test_mcp.py
```

## Using with Claude

Add to your Claude Desktop config:
```json
{
  "dx12-docs": {
    "command": "python",
    "args": ["D:/Users/dilli/AndroidStudioProjects/PlasmaDX/mcp/dx12_mcp_server.py"]
  }
}
```

## For Your Ray Tracing Project

Key DXR APIs already in the database:
- `BuildRaytracingAccelerationStructure` - Build BLAS/TLAS
- `DispatchRays` - Launch rays
- `D3D12_RAYTRACING_ACCELERATION_STRUCTURE_DESC` - AS setup
- `D3D12_DISPATCH_RAYS_DESC` - Shader binding table

## Next Steps (when you're ready)

1. **Extract Agility SDK headers** (script provided):
   ```
   python parse_agility_sdk.py
   ```
   This will extract from your .nupkg files

2. **Add DirectX-Specs from GitHub**:
   - Clone https://github.com/microsoft/DirectX-Specs
   - Parse the Markdown files (much easier than PDF!)

3. **Enhance PDF parsing**:
   - Install PyPDF2: `pip install PyPDF2`
   - The server will automatically use it

## Example Queries for Claude

Once connected, ask Claude:
- "Using the D3D12 MCP, search for device creation"
- "Find all raytracing acceleration structure APIs"
- "Get details on ID3D12Device5 for DXR support"
- "What's the difference between BLAS and TLAS?"

## Why This Approach Works

- **Start simple** - Core features first, expand later
- **DXR focused** - Has everything you need for ray tracing
- **Expandable** - Easy to add more content as needed
- **Familiar pattern** - Based on your successful Vulkan server

## Troubleshooting

If Python isn't found:
1. Install Python 3.8+ from python.org
2. Or use `py` launcher if you have it
3. Or specify full path to python.exe

The server creates `dx12_docs.db` automatically on first run.

---

You've got this, Ben! This gives you a solid foundation without the complexity. Once it's working, you can expand it as needed. The fact that you built that sophisticated Vulkan server means this will be easy for you! 🎮🚀

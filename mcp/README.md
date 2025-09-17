# D3D12 MCP Documentation Server

## Quick Start

This MCP server provides instant access to D3D12, DXR (DirectX Raytracing), and Agility SDK documentation directly in Claude!

## Features

- **D3D12 Core API** - All essential interfaces, functions, and structures
- **DirectX Raytracing (DXR)** - Complete DXR API for ray tracing
- **Feature Level Tracking** - Know which features need which D3D12 version
- **Agility SDK Support** - Track which features need specific SDK versions

## Available Tools

1. **search_dx12_api** - Search D3D12 functions, interfaces, structures
   - Example: "search for command"
   - Categories: Core, Pipeline, Resources, Synchronization, etc.

2. **search_dxr_api** - Search ray tracing specific APIs
   - Example: "search for acceleration structure"
   - Focuses on DXR-specific functionality

3. **get_dx12_entity** - Get detailed info on specific API
   - Example: "get details on ID3D12Device"
   - Shows feature level requirements

4. **dx12_quick_reference** - Database statistics and overview

## Setup

1. **Install PyPDF2** (optional, for enhanced PDF parsing):
   ```
   pip install PyPDF2
   ```

2. **Run the server**:
   ```
   python dx12_mcp_server.py
   ```

3. **Or use with Claude Desktop** - add to your Claude config

## Database

The server automatically creates `dx12_docs.db` with:
- Core D3D12 API entities
- DXR raytracing APIs
- Agility SDK feature tracking
- PDF content (if PyPDF2 installed)

## Extending the Database

To add more content:
1. Extract Agility SDK headers from the NuGet packages
2. Parse additional documentation
3. Add to the database tables

## For Your Ray Tracing Goals

Key DXR entities already included:
- Acceleration structures (BLAS/TLAS)
- Ray dispatch pipeline
- Shader binding tables
- Ray generation, hit, and miss shaders

## Next Steps

1. **Parse Agility SDK headers** - Extract from those .nupkg files
2. **Add DirectX-Specs content** - Clone and parse the GitHub markdown
3. **Enhance PDF parsing** - Extract more detailed API info

## Examples for Claude

Ask Claude:
- "Using the D3D12 MCP, search for raytracing APIs"
- "Get details on BuildRaytracingAccelerationStructure"
- "Find all command queue related functions"
- "What's needed for mesh shaders?"

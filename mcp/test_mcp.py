#!/usr/bin/env python3
"""
Quick test for D3D12 MCP Server
Run this to verify the server is working correctly
"""

import sys
import os

# Add the mcp directory to path
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from dx12_mcp_server import DX12MCPServer

def test_server():
    print("Testing D3D12 MCP Server...")
    print("-" * 50)
    
    # Initialize server
    server = DX12MCPServer()
    
    # Test 1: Quick reference
    print("\n1. Quick Reference:")
    stats = server.dx12_quick_reference()
    for key, value in stats.items():
        print(f"  {key}: {value}")
    
    # Test 2: Search D3D12 API
    print("\n2. Search for 'device':")
    results = server.search_dx12_api("device", limit=5)
    for r in results:
        print(f"  - {r['name']} ({r['type']}): {r['description'][:60]}...")
    
    # Test 3: Search DXR API
    print("\n3. Search for 'ray':")
    results = server.search_dxr_api("ray", limit=5)
    for r in results:
        print(f"  - {r['name']} ({r['type']}): {r['description'][:60]}...")
    
    # Test 4: Get specific entity
    print("\n4. Get details on 'ID3D12Device':")
    entity = server.get_dx12_entity("ID3D12Device")
    if entity:
        print(f"  Name: {entity['name']}")
        print(f"  Type: {entity['type']}")
        print(f"  Description: {entity['description']}")
        print(f"  Category: {entity.get('category', 'N/A')}")
    
    # Test 5: DXR entity
    print("\n5. Get details on 'DispatchRays':")
    entity = server.get_dx12_entity("DispatchRays")
    if entity:
        print(f"  Name: {entity['name']}")
        print(f"  Type: {entity['type']}")
        print(f"  Description: {entity['description']}")
    
    print("\n" + "-" * 50)
    print("✅ All tests completed successfully!")

if __name__ == "__main__":
    test_server()

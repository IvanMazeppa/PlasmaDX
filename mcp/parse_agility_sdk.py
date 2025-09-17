#!/usr/bin/env python3
"""
Extract and parse Agility SDK headers from NuGet packages
This will enhance the MCP database with latest D3D12 features
"""

import zipfile
import os
import re
from pathlib import Path

def extract_headers_from_nupkg(nupkg_path, output_dir):
    """Extract D3D12 headers from a NuGet package"""
    print(f"Extracting from {nupkg_path}...")
    
    # NuGet packages are just ZIP files
    with zipfile.ZipFile(nupkg_path, 'r') as zip_ref:
        # Look for header files
        headers = [f for f in zip_ref.namelist() if f.endswith('.h')]
        
        print(f"Found {len(headers)} header files")
        
        # Extract headers to output directory
        for header in headers:
            if 'd3d12' in header.lower() or 'dxgi' in header.lower():
                print(f"  Extracting: {os.path.basename(header)}")
                zip_ref.extract(header, output_dir)
    
    return headers

def parse_d3d12_header(header_path):
    """Parse a D3D12 header for interfaces and functions"""
    entities = []
    
    with open(header_path, 'r', encoding='utf-8', errors='ignore') as f:
        content = f.read()
    
    # Find interfaces (ID3D12*)
    interface_pattern = r'MIDL_INTERFACE.*?\n\s*(?:interface\s+)?(\w+)\s*:\s*public\s+(\w+)'
    interfaces = re.findall(interface_pattern, content, re.MULTILINE | re.DOTALL)
    
    for interface_name, parent in interfaces:
        if interface_name.startswith('ID3D12'):
            entities.append({
                'name': interface_name,
                'type': 'interface',
                'parent': parent,
                'category': 'Core'
            })
    
    # Find method declarations
    method_pattern = r'STDMETHOD\((\w+)\)\s*\((.*?)\)'
    methods = re.findall(method_pattern, content)
    
    for method_name, params in methods:
        entities.append({
            'name': method_name,
            'type': 'method',
            'params': params.strip(),
            'category': 'Methods'
        })
    
    # Find structures
    struct_pattern = r'typedef\s+struct\s+(\w+)'
    structs = re.findall(struct_pattern, content)
    
    for struct_name in structs:
        if struct_name.startswith('D3D12_'):
            entities.append({
                'name': struct_name,
                'type': 'structure',
                'category': 'Structures'
            })
    
    # Find enums
    enum_pattern = r'typedef\s+enum\s+(\w+)'
    enums = re.findall(enum_pattern, content)
    
    for enum_name in enums:
        if enum_name.startswith('D3D12_'):
            entities.append({
                'name': enum_name,
                'type': 'enum',
                'category': 'Enums'
            })
    
    return entities

def main():
    project_dir = Path(__file__).parent.parent  # PlasmaDX directory
    mcp_dir = Path(__file__).parent
    
    # Look for NuGet packages
    nupkg_files = list(project_dir.glob("*.nupkg"))
    
    if not nupkg_files:
        print("No NuGet packages found in project directory")
        return
    
    print(f"Found {len(nupkg_files)} NuGet packages:")
    for pkg in nupkg_files:
        print(f"  - {pkg.name}")
    
    # Create extraction directory
    extract_dir = mcp_dir / "agility_headers"
    extract_dir.mkdir(exist_ok=True)
    
    # Extract headers from each package
    all_entities = []
    
    for nupkg in nupkg_files:
        if 'd3d12' in nupkg.name.lower():
            headers = extract_headers_from_nupkg(nupkg, extract_dir)
            
            # Parse extracted headers
            for header_file in extract_dir.rglob("*.h"):
                if 'd3d12' in header_file.name.lower():
                    print(f"Parsing {header_file.name}...")
                    entities = parse_d3d12_header(header_file)
                    all_entities.extend(entities)
    
    print(f"\nTotal entities found: {len(all_entities)}")
    
    # Summary
    interfaces = [e for e in all_entities if e['type'] == 'interface']
    methods = [e for e in all_entities if e['type'] == 'method']
    structs = [e for e in all_entities if e['type'] == 'structure']
    enums = [e for e in all_entities if e['type'] == 'enum']
    
    print(f"  Interfaces: {len(interfaces)}")
    print(f"  Methods: {len(methods)}")
    print(f"  Structures: {len(structs)}")
    print(f"  Enums: {len(enums)}")
    
    # Save parsed data for database import
    import json
    output_file = mcp_dir / "parsed_agility_entities.json"
    with open(output_file, 'w') as f:
        json.dump(all_entities, f, indent=2)
    
    print(f"\nSaved parsed entities to {output_file}")
    print("You can now import these into the MCP database!")

if __name__ == "__main__":
    main()

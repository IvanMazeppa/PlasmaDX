#!/usr/bin/env python3
"""
DirectX 12 MCP Server v1.0
Quick documentation server for D3D12, DXR, and Agility SDK
Built for PlasmaDX project - September 2025
"""

import json
import sys
import sqlite3
import re
from pathlib import Path
from typing import Dict, List, Optional, Any

# PDF parsing imports
try:
    import PyPDF2
    HAS_PYPDF = True
except ImportError:
    HAS_PYPDF = False
    print("Warning: PyPDF2 not installed - PDF parsing will be limited", file=sys.stderr)

class DX12MCPServer:
    def __init__(self):
        # Set up paths
        self.script_dir = Path(__file__).parent
        self.db_path = self.script_dir / "dx12_docs.db"
        self.pdf_path = self.script_dir / "windows-win32-api-d3d12.pdf"
        
        # Check if we need to create/update the database
        self.ensure_database()
    
    def ensure_database(self):
        """Create or verify the D3D12 documentation database"""
        if not self.db_path.exists():
            print(f"Creating D3D12 database...", file=sys.stderr)
            self.create_database()
        else:
            # Verify database integrity
            try:
                conn = sqlite3.connect(self.db_path)
                cursor = conn.cursor()
                cursor.execute("SELECT COUNT(*) FROM dx12_entities")
                count = cursor.fetchone()[0]
                conn.close()
                print(f"Database OK with {count} entities", file=sys.stderr)
            except:
                print("Database corrupted, recreating...", file=sys.stderr)
                self.create_database()
    
    def create_database(self):
        """Create and populate the D3D12 database"""
        conn = sqlite3.connect(self.db_path)
        cursor = conn.cursor()
        
        # Create main entities table
        cursor.execute('''
            CREATE TABLE IF NOT EXISTS dx12_entities (
                id INTEGER PRIMARY KEY,
                name TEXT UNIQUE NOT NULL,
                type TEXT NOT NULL,
                description TEXT,
                category TEXT,
                keywords TEXT,
                agility_version TEXT,
                feature_level TEXT
            )
        ''')
        
        # Create DXR-specific table
        cursor.execute('''
            CREATE TABLE IF NOT EXISTS dxr_entities (
                id INTEGER PRIMARY KEY,
                name TEXT UNIQUE NOT NULL,
                type TEXT NOT NULL,
                description TEXT,
                keywords TEXT
            )
        ''')
        
        # Populate with essential D3D12 data
        self._populate_core_dx12(cursor)
        self._populate_dxr_basics(cursor)
        
        # Parse PDF if available
        if HAS_PYPDF and self.pdf_path.exists():
            self._parse_pdf_content(cursor)
        
        conn.commit()
        conn.close()
        print("Database creation completed!", file=sys.stderr)
    
    def _populate_core_dx12(self, cursor):
        """Populate core D3D12 entities"""
        core_entities = [
            # Essential Device & Queue Functions
            ('D3D12CreateDevice', 'function', 'Creates a D3D12 device interface for GPU access', 'device gpu creation initialization', 'Core', None, '12_0'),
            ('ID3D12Device', 'interface', 'Main D3D12 device interface representing a virtual GPU adapter', 'device interface gpu adapter', 'Core', None, '12_0'),
            ('ID3D12CommandQueue', 'interface', 'Command queue for GPU work submission', 'queue command submission gpu execution', 'Core', None, '12_0'),
            ('ID3D12CommandAllocator', 'interface', 'Memory allocator for command lists', 'command allocator memory list', 'Core', None, '12_0'),
            ('ID3D12GraphicsCommandList', 'interface', 'Records GPU commands for execution', 'command list recording gpu draw dispatch', 'Core', None, '12_0'),
            
            # Resources & Memory
            ('ID3D12Resource', 'interface', 'GPU resource (buffer/texture) interface', 'resource buffer texture memory gpu', 'Resources', None, '12_0'),
            ('D3D12_HEAP_TYPE', 'enum', 'Memory heap types (DEFAULT, UPLOAD, READBACK, CUSTOM)', 'heap memory type gpu cpu', 'Memory', None, '12_0'),
            ('CreateCommittedResource', 'method', 'Creates a resource with committed memory allocation', 'resource create committed memory allocation', 'Resources', None, '12_0'),
            ('CreatePlacedResource', 'method', 'Creates a resource in an existing heap', 'resource create placed heap memory', 'Resources', None, '12_0'),
            
            # Pipeline State
            ('ID3D12PipelineState', 'interface', 'Compiled GPU pipeline state object', 'pipeline state pso shader render compute', 'Pipeline', None, '12_0'),
            ('ID3D12RootSignature', 'interface', 'Defines shader resource bindings', 'root signature binding shader resources descriptors', 'Pipeline', None, '12_0'),
            ('D3D12_GRAPHICS_PIPELINE_STATE_DESC', 'structure', 'Graphics pipeline configuration', 'graphics pipeline state description pso', 'Pipeline', None, '12_0'),
            ('D3D12_COMPUTE_PIPELINE_STATE_DESC', 'structure', 'Compute pipeline configuration', 'compute pipeline state description pso dispatch', 'Pipeline', None, '12_0'),
            
            # Synchronization
            ('ID3D12Fence', 'interface', 'GPU/CPU synchronization primitive', 'fence synchronization sync gpu cpu wait', 'Synchronization', None, '12_0'),
            ('Signal', 'method', 'Signals a fence from GPU', 'signal fence synchronization gpu', 'Synchronization', None, '12_0'),
            ('Wait', 'method', 'GPU waits for fence value', 'wait fence synchronization gpu stall', 'Synchronization', None, '12_0'),
            
            # Descriptors
            ('D3D12_DESCRIPTOR_HEAP_TYPE', 'enum', 'Types of descriptor heaps (CBV_SRV_UAV, SAMPLER, RTV, DSV)', 'descriptor heap type cbv srv uav rtv dsv', 'Descriptors', None, '12_0'),
            ('CreateConstantBufferView', 'method', 'Creates a constant buffer view', 'cbv constant buffer view descriptor', 'Descriptors', None, '12_0'),
            ('CreateShaderResourceView', 'method', 'Creates a shader resource view', 'srv shader resource view texture buffer', 'Descriptors', None, '12_0'),
            ('CreateUnorderedAccessView', 'method', 'Creates an unordered access view', 'uav unordered access view compute storage', 'Descriptors', None, '12_0'),
            
            # Modern Features (12.1+)
            ('ID3D12Device5', 'interface', 'Device interface with raytracing support', 'device raytracing dxr rt', 'Raytracing', '1.4', '12_1'),
            ('ID3D12Device10', 'interface', 'Latest device interface with mesh shaders and more', 'device mesh shader sampler feedback', 'Modern', '1.6', '12_2'),
            ('CreateStateObject', 'method', 'Creates raytracing pipeline state', 'raytracing state object rtpso dxr pipeline', 'Raytracing', '1.4', '12_1'),
        ]
        
        for entity in core_entities:
            cursor.execute('''
                INSERT OR REPLACE INTO dx12_entities 
                (name, type, description, keywords, category, agility_version, feature_level)
                VALUES (?, ?, ?, ?, ?, ?, ?)
            ''', entity)
    
    def _populate_dxr_basics(self, cursor):
        """Populate DXR (DirectX Raytracing) specific entities"""
        dxr_entities = [
            # DXR Core
            ('D3D12_RAYTRACING_TIER', 'enum', 'Raytracing hardware capability tiers', 'raytracing tier capability dxr hardware'),
            ('D3D12_RAYTRACING_ACCELERATION_STRUCTURE_DESC', 'structure', 'Describes a BLAS or TLAS structure', 'acceleration structure blas tlas raytracing'),
            ('BuildRaytracingAccelerationStructure', 'method', 'Builds acceleration structures for raytracing', 'build acceleration structure blas tlas dxr'),
            ('DispatchRays', 'method', 'Launches raytracing work on GPU', 'dispatch rays raytracing dxr gpu launch'),
            
            # DXR Pipeline
            ('D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE', 'enum', 'Raytracing pipeline state object type', 'raytracing pipeline state object rtpso'),
            ('D3D12_RAYTRACING_SHADER_CONFIG', 'structure', 'Ray payload and attribute size configuration', 'raytracing shader config payload attribute'),
            ('D3D12_RAYTRACING_PIPELINE_CONFIG', 'structure', 'Maximum ray recursion depth setting', 'raytracing pipeline config recursion depth'),
            
            # Shader Table
            ('D3D12_DISPATCH_RAYS_DESC', 'structure', 'Shader binding table configuration for ray dispatch', 'dispatch rays shader table sbt raygen miss hit'),
            ('RayGenerationShaderRecord', 'concept', 'Entry point for ray generation shader', 'raygen shader record entry sbt'),
            ('HitGroupRecord', 'concept', 'Shader records for ray-geometry intersections', 'hit group record shader intersection closest any'),
            ('MissShaderRecord', 'concept', 'Shader executed when rays miss all geometry', 'miss shader record ray sbt'),
            
            # BLAS/TLAS
            ('D3D12_RAYTRACING_GEOMETRY_DESC', 'structure', 'Describes geometry for bottom-level acceleration structure', 'geometry blas vertex index triangle aabb'),
            ('D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS', 'structure', 'Input data for AS builds', 'acceleration structure inputs blas tlas instances'),
            ('D3D12_RAYTRACING_INSTANCE_DESC', 'structure', 'Instance data for top-level acceleration structure', 'instance tlas transform blas reference'),
        ]
        
        for name, type_, desc, keywords in dxr_entities:
            cursor.execute('''
                INSERT OR REPLACE INTO dxr_entities (name, type, description, keywords)
                VALUES (?, ?, ?, ?)
            ''', (name, type_, desc, keywords))
    
    def _parse_pdf_content(self, cursor):
        """Parse the D3D12 PDF for additional content"""
        # This would parse the PDF similar to your Vulkan implementation
        # For now, we'll skip the heavy PDF parsing to get you started quickly
        pass
    
    def search_dx12_api(self, query: str, category: str = "all", limit: int = 10) -> List[Dict]:
        """Search D3D12 API entities"""
        conn = sqlite3.connect(self.db_path)
        conn.row_factory = sqlite3.Row
        cursor = conn.cursor()
        
        # Build query
        where_clause = "WHERE (name LIKE ? OR description LIKE ? OR keywords LIKE ?)"
        params = [f"%{query}%", f"%{query}%", f"%{query}%"]
        
        if category != "all":
            where_clause += " AND category = ?"
            params.append(category)
        
        # Search main D3D12 entities
        sql = f"""
            SELECT name, type, description, category, feature_level
            FROM dx12_entities
            {where_clause}
            ORDER BY 
                CASE 
                    WHEN name LIKE ? THEN 1
                    WHEN name LIKE ? THEN 2
                    ELSE 3
                END
            LIMIT ?
        """
        params.extend([f"{query}%", f"%{query}%", limit])
        
        cursor.execute(sql, params)
        results = [dict(row) for row in cursor.fetchall()]
        
        # Also search PDF entities if table exists
        try:
            cursor.execute("SELECT name FROM sqlite_master WHERE type='table' AND name='dx12_pdf_entities'")
            if cursor.fetchone():
                # Reset params for PDF search
                params = [f"%{query}%", f"%{query}%", f"%{query}%"]
                if category != "all":
                    params.append(category)
                
                pdf_sql = f"""
                    SELECT name, type, description, category, NULL as feature_level
                    FROM dx12_pdf_entities
                    WHERE (name LIKE ? OR description LIKE ? OR keywords LIKE ?)
                    {' AND category = ?' if category != 'all' else ''}
                    LIMIT ?
                """
                params.append(max(0, limit - len(results)))
                
                cursor.execute(pdf_sql, params)
                pdf_results = [dict(row) for row in cursor.fetchall()]
                
                # Merge results, avoiding duplicates
                existing_names = {r['name'] for r in results}
                for pdf_result in pdf_results:
                    if pdf_result['name'] not in existing_names:
                        results.append(pdf_result)
        except:
            pass  # PDF table doesn't exist yet
        
        conn.close()
        return results[:limit]
    
    def search_dxr_api(self, query: str, limit: int = 10) -> List[Dict]:
        """Search DXR-specific entities"""
        conn = sqlite3.connect(self.db_path)
        conn.row_factory = sqlite3.Row
        cursor = conn.cursor()
        
        cursor.execute("""
            SELECT name, type, description
            FROM dxr_entities
            WHERE name LIKE ? OR description LIKE ? OR keywords LIKE ?
            ORDER BY name
            LIMIT ?
        """, (f"%{query}%", f"%{query}%", f"%{query}%", limit))
        
        results = [dict(row) for row in cursor.fetchall()]
        conn.close()
        return results
    
    def get_dx12_entity(self, name: str) -> Optional[Dict]:
        """Get detailed info about a specific D3D12 entity"""
        conn = sqlite3.connect(self.db_path)
        conn.row_factory = sqlite3.Row
        cursor = conn.cursor()
        
        # Try main entities first
        cursor.execute("""
            SELECT * FROM dx12_entities WHERE name = ? COLLATE NOCASE
        """, (name,))
        
        row = cursor.fetchone()
        if row:
            conn.close()
            return dict(row)
        
        # Try DXR entities
        cursor.execute("""
            SELECT * FROM dxr_entities WHERE name = ? COLLATE NOCASE
        """, (name,))
        
        row = cursor.fetchone()
        conn.close()
        return dict(row) if row else None
    
    def dx12_quick_reference(self) -> Dict[str, Any]:
        """Get D3D12 database statistics"""
        conn = sqlite3.connect(self.db_path)
        cursor = conn.cursor()
        
        cursor.execute("SELECT COUNT(*) FROM dx12_entities")
        dx12_count = cursor.fetchone()[0]
        
        cursor.execute("SELECT COUNT(*) FROM dxr_entities")
        dxr_count = cursor.fetchone()[0]
        
        cursor.execute("SELECT DISTINCT category FROM dx12_entities WHERE category IS NOT NULL")
        categories = [row[0] for row in cursor.fetchall()]
        
        conn.close()
        
        return {
            'total_dx12_entities': dx12_count,
            'total_dxr_entities': dxr_count,
            'categories': categories,
            'database_path': str(self.db_path),
            'pdf_available': self.pdf_path.exists()
        }


def main():
    """Main MCP server loop"""
    try:
        server = DX12MCPServer()
    except Exception as e:
        error_response = {
            'jsonrpc': '2.0',
            'id': 0,
            'error': {'code': -32603, 'message': f'Server init error: {str(e)}'}
        }
        print(json.dumps(error_response), flush=True)
        return
    
    # MCP Protocol handler
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        
        try:
            request = json.loads(line)
            method = request.get('method')
            params = request.get('params', {})
            request_id = request.get('id')
            
            if method == 'initialize':
                response = {
                    'jsonrpc': '2.0',
                    'id': request_id,
                    'result': {
                        'protocolVersion': '2024-11-05',
                        'capabilities': {'tools': {}},
                        'serverInfo': {
                            'name': 'dx12-mcp-server',
                            'version': '1.0.0'
                        }
                    }
                }
            
            elif method == 'tools/list':
                response = {
                    'jsonrpc': '2.0',
                    'id': request_id,
                    'result': {
                        'tools': [
                            {
                                'name': 'search_dx12_api',
                                'description': 'Search D3D12 API functions, interfaces, and structures',
                                'inputSchema': {
                                    'type': 'object',
                                    'properties': {
                                        'query': {'type': 'string', 'description': 'Search query'},
                                        'category': {'type': 'string', 'description': 'Category filter (all, Core, Pipeline, etc.)', 'default': 'all'},
                                        'limit': {'type': 'integer', 'description': 'Max results', 'default': 10}
                                    },
                                    'required': ['query']
                                }
                            },
                            {
                                'name': 'search_dxr_api',
                                'description': 'Search DirectX Raytracing (DXR) specific APIs',
                                'inputSchema': {
                                    'type': 'object',
                                    'properties': {
                                        'query': {'type': 'string', 'description': 'Search query'},
                                        'limit': {'type': 'integer', 'description': 'Max results', 'default': 10}
                                    },
                                    'required': ['query']
                                }
                            },
                            {
                                'name': 'get_dx12_entity',
                                'description': 'Get detailed information about a specific D3D12/DXR entity',
                                'inputSchema': {
                                    'type': 'object',
                                    'properties': {
                                        'name': {'type': 'string', 'description': 'Entity name (e.g., ID3D12Device, DispatchRays)'}
                                    },
                                    'required': ['name']
                                }
                            },
                            {
                                'name': 'dx12_quick_reference',
                                'description': 'Get D3D12 database statistics and overview',
                                'inputSchema': {
                                    'type': 'object',
                                    'properties': {}
                                }
                            }
                        ]
                    }
                }
            
            elif method == 'tools/call':
                tool_name = params.get('name')
                arguments = params.get('arguments', {})
                
                if tool_name == 'search_dx12_api':
                    results = server.search_dx12_api(
                        query=arguments.get('query', ''),
                        category=arguments.get('category', 'all'),
                        limit=arguments.get('limit', 10)
                    )
                    
                    if results:
                        formatted = []
                        for r in results:
                            text = f"**{r['name']}** ({r['type']}): {r['description']}"
                            if r.get('feature_level'):
                                text += f" [Feature Level: {r['feature_level']}]"
                            formatted.append(text)
                        response_text = f"Found {len(results)} D3D12 entities:\n\n" + "\n\n".join(formatted)
                    else:
                        response_text = f"No results found for '{arguments.get('query', '')}'"
                    
                    response = {
                        'jsonrpc': '2.0',
                        'id': request_id,
                        'result': {'content': [{'type': 'text', 'text': response_text}]}
                    }
                
                elif tool_name == 'search_dxr_api':
                    results = server.search_dxr_api(
                        query=arguments.get('query', ''),
                        limit=arguments.get('limit', 10)
                    )
                    
                    if results:
                        formatted = []
                        for r in results:
                            formatted.append(f"**{r['name']}** ({r['type']}): {r['description']}")
                        response_text = f"Found {len(results)} DXR entities:\n\n" + "\n\n".join(formatted)
                    else:
                        response_text = f"No DXR results found for '{arguments.get('query', '')}'"
                    
                    response = {
                        'jsonrpc': '2.0',
                        'id': request_id,
                        'result': {'content': [{'type': 'text', 'text': response_text}]}
                    }
                
                elif tool_name == 'get_dx12_entity':
                    entity = server.get_dx12_entity(arguments.get('name', ''))
                    if entity:
                        response_text = f"**{entity['name']}** ({entity['type']})\n\n{entity['description']}"
                        if entity.get('category'):
                            response_text += f"\n\n**Category:** {entity['category']}"
                        if entity.get('feature_level'):
                            response_text += f"\n**Feature Level:** {entity['feature_level']}"
                        if entity.get('agility_version'):
                            response_text += f"\n**Agility SDK:** {entity['agility_version']}"
                        response_text += f"\n\n**Keywords:** {entity.get('keywords', 'N/A')}"
                    else:
                        response_text = f"Entity '{arguments.get('name', '')}' not found"
                    
                    response = {
                        'jsonrpc': '2.0',
                        'id': request_id,
                        'result': {'content': [{'type': 'text', 'text': response_text}]}
                    }
                
                elif tool_name == 'dx12_quick_reference':
                    stats = server.dx12_quick_reference()
                    response_text = "**D3D12 MCP Database Statistics**\n\n"
                    response_text += f"**D3D12 Entities:** {stats['total_dx12_entities']}\n"
                    response_text += f"**DXR Entities:** {stats['total_dxr_entities']}\n"
                    response_text += f"**Categories:** {', '.join(stats['categories'])}\n"
                    response_text += f"**PDF Available:** {'Yes' if stats['pdf_available'] else 'No'}\n"
                    response_text += f"**Database:** {stats['database_path']}"
                    
                    response = {
                        'jsonrpc': '2.0',
                        'id': request_id,
                        'result': {'content': [{'type': 'text', 'text': response_text}]}
                    }
                
                else:
                    response = {
                        'jsonrpc': '2.0',
                        'id': request_id,
                        'error': {'code': -32601, 'message': f'Unknown tool: {tool_name}'}
                    }
            
            else:
                response = {
                    'jsonrpc': '2.0',
                    'id': request_id,
                    'error': {'code': -32601, 'message': f'Unknown method: {method}'}
                }
            
            print(json.dumps(response), flush=True)
            
        except json.JSONDecodeError as e:
            error_response = {
                'jsonrpc': '2.0',
                'id': 0,
                'error': {'code': -32700, 'message': f'Parse error: {str(e)}'}
            }
            print(json.dumps(error_response), flush=True)
        except Exception as e:
            error_response = {
                'jsonrpc': '2.0',
                'id': request_id if 'request_id' in locals() else 0,
                'error': {'code': -32603, 'message': f'Internal error: {str(e)}'}
            }
            print(json.dumps(error_response), flush=True)


if __name__ == '__main__':
    main()

# DX12 Technique Discovery Agent - User Guide

## Overview

The DX12 Technique Discovery Agent is an intelligent background service that continuously explores your MCP server for DX12/DXR techniques relevant to the PlasmaDX project. It maintains a knowledge base of discovered techniques and provides query interfaces for easy access.

## Architecture

### Components

1. **DX12TechniqueAgent** (`src/agents/dx12_technique_agent.py`)
   - Main agent logic for technique discovery
   - Relevance scoring based on project context
   - Knowledge base management

2. **MCPIntegration** (`src/agents/mcp_integration.py`)
   - Interface to your MCP server
   - Search orchestration across different API categories
   - Result processing and filtering

3. **KnowledgeBaseManager** (`src/agents/knowledge_base_manager.py`)
   - Persistent storage using SQLite
   - Search and caching functionality
   - Statistics and reporting

4. **TechniqueAgentRunner** (`src/agents/technique_agent_runner.py`)
   - Command-line interface
   - Background service management
   - Query interface

## Project Context

The agent understands your PlasmaDX project context:

### Current Features (High Relevance)
- Volumetric rendering
- Particle simulation
- Density grid generation
- Compute ray marching
- HDR pipeline
- Temporal accumulation

### Upcoming Features (Medium Relevance)
- DXR integration
- Acceleration structures
- Ray-traced shadows
- Empty space skipping
- Advanced scattering

### Technical Priorities
- Performance optimization
- Memory management
- Shader efficiency
- Barrier optimization

## Usage

### Quick Start

1. **Single Discovery Run**
   ```bash
   run_technique_agent.bat
   ```

2. **Background Service**
   ```bash
   run_technique_agent_background.bat
   ```

3. **Query Techniques**
   ```bash
   query_techniques.bat "volumetric rendering"
   query_techniques.bat "acceleration structure"
   query_techniques.bat "barrier synchronization"
   ```

### Command Line Interface

```bash
# Single discovery cycle
python src/agents/technique_agent_runner.py --mode single --verbose

# Background mode (24-hour intervals)
python src/agents/technique_agent_runner.py --mode background --interval 24

# Query knowledge base
python src/agents/technique_agent_runner.py --mode query --query "volumetric" --limit 10

# Generate report
python src/agents/technique_agent_runner.py --mode report

# Show statistics
python src/agents/technique_agent_runner.py --mode stats
```

## Knowledge Base Structure

### Technique Entry Fields

- **name**: API function/struct name
- **category**: DXR, Core, HLSL, Pipeline, etc.
- **description**: What the technique does
- **relevance_score**: 0.0-1.0 based on project relevance
- **project_applications**: How it applies to PlasmaDX
- **api_references**: Related API functions
- **shader_stages**: Applicable shader stages
- **tags**: Searchable keywords
- **examples**: Usage patterns
- **discovery_date**: When it was found
- **usage_count**: How often it's been accessed

### Storage

- **Primary**: SQLite database (`findings/technique_knowledge_base.db`)
- **Export**: JSON format (`findings/technique_knowledge_base.json`)
- **Reports**: Markdown format (`findings/technique_discovery_report.md`)

## Search and Discovery

### Search Patterns

The agent searches for techniques using these patterns:

1. **Volumetric Rendering**
   - "volumetric rendering"
   - "density sampling"
   - "3D texture"
   - "volume ray marching"

2. **DXR Techniques**
   - "acceleration structure"
   - "ray tracing pipeline"
   - "shader binding table"
   - "dispatch rays"

3. **Performance Optimization**
   - "resource barrier"
   - "descriptor heap"
   - "memory management"
   - "synchronization"

### Relevance Scoring

Techniques are scored based on:
- Keyword matching (0.9-1.0 weight)
- Project context alignment
- Category relevance
- Multiple match bonus

## Integration with PlasmaDX

### Current Workflow Integration

The agent is designed to complement your existing workflow:

1. **During Development**
   - Run background discovery to find new techniques
   - Query knowledge base when implementing features
   - Check for optimization opportunities

2. **Feature Implementation**
   - Search for relevant APIs before coding
   - Find usage examples and patterns
   - Discover related techniques

3. **Performance Optimization**
   - Query for barrier optimization techniques
   - Find memory management patterns
   - Discover synchronization best practices

### MCP Server Integration

The agent integrates with your MCP server through:

```python
# Example MCP queries the agent performs
mcp_client.search_all_sources("volumetric rendering", limit=20)
mcp_client.search_dxr_api("acceleration structure", limit=15)
mcp_client.search_hlsl_intrinsics("ray marching", limit=10)
mcp_client.get_dx12_entity("D3D12_RESOURCE_BARRIER")
```

## Output Files

### Knowledge Base Files

1. **`findings/technique_knowledge_base.db`**
   - SQLite database with all discovered techniques
   - Search cache and usage statistics
   - Discovery history

2. **`findings/technique_knowledge_base.json`**
   - JSON export of all techniques
   - Project context metadata
   - Discovery history (last 100 entries)

3. **`findings/technique_discovery_report.md`**
   - Human-readable technique report
   - Grouped by category
   - Sorted by relevance

### Log Files

1. **`logs/technique_agent.log`**
   - Agent execution logs
   - Discovery progress
   - Error messages

## Customization

### Adding New Search Patterns

Edit `dx12_technique_agent.py`:

```python
search_patterns = [
    "your new pattern",
    "another search term",
    # ... existing patterns
]
```

### Modifying Relevance Scoring

Update `_build_relevance_keywords()`:

```python
relevance_keywords = {
    "your_keyword": 0.95,  # High relevance
    "another_term": 0.7,   # Medium relevance
    # ... existing keywords
}
```

### Custom Project Context

Update `ProjectContext` in `dx12_technique_agent.py`:

```python
self.project_context = ProjectContext(
    current_features=["your", "new", "features"],
    upcoming_features=["planned", "features"],
    technical_priorities=["your", "priorities"],
    known_issues=["current", "issues"]
)
```

## Monitoring and Maintenance

### Background Service

The background service runs continuously and:
- Discovers new techniques every 24 hours (configurable)
- Updates the knowledge base automatically
- Logs all activities
- Handles errors gracefully

### Cache Management

- Search results are cached for 30 minutes
- Database includes automatic cache cleanup
- Memory cache is cleared on updates

### Statistics

Monitor agent performance:
```bash
python src/agents/technique_agent_runner.py --mode stats
```

Shows:
- Total techniques discovered
- Techniques by category
- High relevance techniques
- Most frequently used techniques
- Recent discoveries

## Troubleshooting

### Common Issues

1. **Agent fails to start**
   - Check Python dependencies
   - Verify MCP server connectivity
   - Check log files for errors

2. **No techniques discovered**
   - Verify MCP server is running
   - Check search patterns
   - Review relevance scoring

3. **Database errors**
   - Check file permissions
   - Verify SQLite installation
   - Clear cache if corrupted

### Debug Mode

Run with verbose logging:
```bash
python src/agents/technique_agent_runner.py --mode single --verbose
```

### Log Analysis

Check `logs/technique_agent.log` for:
- Discovery progress
- MCP server responses
- Error details
- Performance metrics

## Future Enhancements

### Planned Features

1. **Machine Learning Integration**
   - Learn from usage patterns
   - Improve relevance scoring
   - Suggest related techniques

2. **Code Generation**
   - Generate usage examples
   - Create boilerplate code
   - Integration with IDE

3. **Performance Monitoring**
   - Track technique usage in code
   - Performance impact analysis
   - Optimization suggestions

4. **Community Integration**
   - Share discoveries with team
   - Collaborative knowledge base
   - Technique ratings and reviews

---

*This agent is designed to grow with your project and become an essential part of your DX12 development workflow.*




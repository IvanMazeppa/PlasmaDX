#!/usr/bin/env python3
"""
DX12 Technique Discovery Agent for PlasmaDX

This background agent explores the MCP server to discover useful DX12/DXR techniques
and maintains a knowledge base of relevant APIs and patterns for the project.

Author: AI Assistant
Created: 2025-01-19
"""

import json
import time
import logging
from datetime import datetime
from typing import Dict, List, Set, Optional, Any
from pathlib import Path
import asyncio
from dataclasses import dataclass, asdict
import hashlib

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
    handlers=[
        logging.FileHandler('logs/technique_agent.log'),
        logging.StreamHandler()
    ]
)
logger = logging.getLogger('DX12TechniqueAgent')

@dataclass
class TechniqueEntry:
    """Represents a discovered DX12/DXR technique or API"""
    name: str
    category: str  # e.g., 'DXR', 'Core', 'Pipeline', 'HLSL'
    description: str
    relevance_score: float  # 0.0 to 1.0 based on project relevance
    project_applications: List[str]  # How this applies to PlasmaDX
    api_references: List[str]  # Related API functions/structs
    shader_stages: List[str]  # If applicable
    discovery_date: str
    last_updated: str
    tags: List[str]
    examples: List[str]  # Usage examples or patterns

@dataclass
class ProjectContext:
    """Current project context for relevance scoring"""
    current_features: List[str]
    upcoming_features: List[str]
    technical_priorities: List[str]
    known_issues: List[str]

class DX12TechniqueAgent:
    """Background agent for discovering and cataloging DX12/DXR techniques"""
    
    def __init__(self, knowledge_base_path: str = "findings/technique_knowledge_base.json"):
        self.knowledge_base_path = Path(knowledge_base_path)
        self.knowledge_base_path.parent.mkdir(exist_ok=True)
        
        # Project context for relevance scoring
        self.project_context = ProjectContext(
            current_features=[
                "volumetric_rendering",
                "particle_simulation", 
                "density_grid_generation",
                "compute_ray_marching",
                "hdr_pipeline",
                "temporal_accumulation"
            ],
            upcoming_features=[
                "dxr_integration",
                "acceleration_structures",
                "ray_traced_shadows",
                "empty_space_skipping",
                "advanced_scattering"
            ],
            technical_priorities=[
                "performance_optimization",
                "memory_management",
                "shader_efficiency",
                "barrier_optimization"
            ],
            known_issues=[
                "dxr_pipeline_creation",
                "resource_barriers",
                "descriptor_management"
            ]
        )
        
        # Knowledge base
        self.techniques: Dict[str, TechniqueEntry] = {}
        self.discovery_history: List[str] = []
        self.relevance_keywords = self._build_relevance_keywords()
        
        # Load existing knowledge base
        self.load_knowledge_base()
        
        logger.info("DX12 Technique Agent initialized")

    def _build_relevance_keywords(self) -> Dict[str, float]:
        """Build keyword relevance scores for PlasmaDX project"""
        return {
            # High relevance (0.9-1.0)
            "volumetric": 1.0,
            "raytracing": 1.0,
            "acceleration": 1.0,
            "density": 1.0,
            "particle": 1.0,
            "compute": 0.95,
            "hdr": 0.95,
            "temporal": 0.95,
            "barrier": 0.9,
            "descriptor": 0.9,
            "uav": 0.9,
            "srv": 0.9,
            
            # Medium relevance (0.6-0.8)
            "pipeline": 0.8,
            "shader": 0.8,
            "texture": 0.8,
            "buffer": 0.8,
            "memory": 0.7,
            "synchronization": 0.7,
            "state": 0.7,
            "resource": 0.7,
            
            # Lower relevance (0.3-0.5)
            "geometry": 0.5,
            "mesh": 0.5,
            "triangle": 0.4,
            "vertex": 0.4,
            "rasterization": 0.3
        }

    def calculate_relevance_score(self, name: str, description: str, category: str) -> float:
        """Calculate relevance score based on project context"""
        text = f"{name} {description} {category}".lower()
        score = 0.0
        matches = 0
        
        for keyword, weight in self.relevance_keywords.items():
            if keyword in text:
                score += weight
                matches += 1
        
        if matches == 0:
            return 0.1  # Minimum score for unknown relevance
        
        # Normalize and boost for multiple matches
        normalized_score = min(score / matches, 1.0)
        boost_factor = 1.0 + (matches - 1) * 0.1  # 10% boost per additional match
        
        return min(normalized_score * boost_factor, 1.0)

    def discover_techniques_from_mcp(self, mcp_client) -> List[TechniqueEntry]:
        """Discover techniques by exploring the MCP server"""
        logger.info("Starting technique discovery from MCP server")
        discoveries = []
        
        # Search patterns relevant to PlasmaDX
        search_patterns = [
            "volumetric rendering",
            "ray marching", 
            "acceleration structure",
            "compute shader",
            "temporal accumulation",
            "density sampling",
            "particle simulation",
            "hdr rendering",
            "barrier synchronization",
            "descriptor management",
            "empty space skipping",
            "scattering",
            "absorption"
        ]
        
        for pattern in search_patterns:
            try:
                # Search all sources
                results = mcp_client.search_all_sources(pattern, limit=20)
                discoveries.extend(self._process_search_results(results, pattern))
                
                # Search DXR specific
                if any(word in pattern for word in ["ray", "acceleration", "volumetric"]):
                    dxr_results = mcp_client.search_dxr_api(pattern, limit=15)
                    discoveries.extend(self._process_search_results(dxr_results, pattern, category="DXR"))
                
                # Search HLSL intrinsics
                if any(word in pattern for word in ["shader", "compute", "sampling"]):
                    hlsl_results = mcp_client.search_hlsl_intrinsics(pattern, limit=10)
                    discoveries.extend(self._process_search_results(hlsl_results, pattern, category="HLSL"))
                    
            except Exception as e:
                logger.error(f"Error searching for pattern '{pattern}': {e}")
                continue
        
        # Remove duplicates and merge similar entries
        unique_discoveries = self._deduplicate_techniques(discoveries)
        
        logger.info(f"Discovered {len(unique_discoveries)} unique techniques")
        return unique_discoveries

    def _process_search_results(self, results: Any, search_pattern: str, category: str = "Core") -> List[TechniqueEntry]:
        """Process search results into technique entries"""
        techniques = []
        
        if not results or not hasattr(results, '__iter__'):
            return techniques
            
        for result in results:
            try:
                # Extract information from result
                name = getattr(result, 'name', str(result))
                description = getattr(result, 'description', '')
                
                # Calculate relevance
                relevance = self.calculate_relevance_score(name, description, category)
                
                # Only include relevant techniques
                if relevance >= 0.3:
                    technique = TechniqueEntry(
                        name=name,
                        category=category,
                        description=description,
                        relevance_score=relevance,
                        project_applications=self._suggest_applications(name, description, search_pattern),
                        api_references=[name],
                        shader_stages=self._infer_shader_stages(name, description),
                        discovery_date=datetime.now().isoformat(),
                        last_updated=datetime.now().isoformat(),
                        tags=self._generate_tags(name, description, search_pattern),
                        examples=self._generate_examples(name, description)
                    )
                    techniques.append(technique)
                    
            except Exception as e:
                logger.error(f"Error processing search result: {e}")
                continue
                
        return techniques

    def _suggest_applications(self, name: str, description: str, search_pattern: str) -> List[str]:
        """Suggest how this technique applies to PlasmaDX"""
        applications = []
        text = f"{name} {description}".lower()
        
        if "volumetric" in text or "density" in text:
            applications.append("3D density grid generation and sampling")
        if "ray" in text or "march" in text:
            applications.append("Ray marching through volumetric data")
        if "acceleration" in text:
            applications.append("Empty space skipping optimization")
        if "compute" in text:
            applications.append("GPU particle simulation")
        if "temporal" in text:
            applications.append("Temporal anti-aliasing and accumulation")
        if "barrier" in text:
            applications.append("Resource synchronization")
        if "descriptor" in text:
            applications.append("Shader resource binding")
        if "hdr" in text:
            applications.append("HDR pipeline and tone mapping")
        if "scatter" in text:
            applications.append("Physically-based scattering simulation")
        
        return applications if applications else ["General DX12 optimization"]

    def _infer_shader_stages(self, name: str, description: str) -> List[str]:
        """Infer applicable shader stages"""
        stages = []
        text = f"{name} {description}".lower()
        
        if "compute" in text:
            stages.append("compute")
        if "raygen" in text or "ray" in text:
            stages.append("raygen")
        if "miss" in text:
            stages.append("miss")
        if "closesthit" in text:
            stages.append("closesthit")
        if "anyhit" in text:
            stages.append("anyhit")
        if "intersection" in text:
            stages.append("intersection")
            
        return stages if stages else []

    def _generate_tags(self, name: str, description: str, search_pattern: str) -> List[str]:
        """Generate relevant tags"""
        tags = [search_pattern]
        text = f"{name} {description}".lower()
        
        if "volumetric" in text:
            tags.append("volumetric")
        if "performance" in text or "optimization" in text:
            tags.append("performance")
        if "memory" in text:
            tags.append("memory")
        if "synchronization" in text or "barrier" in text:
            tags.append("synchronization")
        if "rendering" in text:
            tags.append("rendering")
            
        return tags

    def _generate_examples(self, name: str, description: str) -> List[str]:
        """Generate usage examples"""
        examples = []
        text = f"{name} {description}".lower()
        
        if "barrier" in text:
            examples.append("UAV barriers before compute dispatch")
            examples.append("SRV transitions for texture sampling")
        if "descriptor" in text:
            examples.append("Dynamic descriptor heap allocation")
        if "compute" in text:
            examples.append("Particle physics simulation")
        if "volumetric" in text:
            examples.append("3D texture sampling for density")
            
        return examples

    def _deduplicate_techniques(self, techniques: List[TechniqueEntry]) -> List[TechniqueEntry]:
        """Remove duplicate techniques and merge similar ones"""
        unique_techniques = {}
        
        for technique in techniques:
            # Create a unique key based on name and category
            key = f"{technique.category}:{technique.name}"
            
            if key in unique_techniques:
                # Merge with existing technique
                existing = unique_techniques[key]
                existing.project_applications.extend(technique.project_applications)
                existing.api_references.extend(technique.api_references)
                existing.shader_stages.extend(technique.shader_stages)
                existing.tags.extend(technique.tags)
                existing.examples.extend(technique.examples)
                
                # Remove duplicates
                existing.project_applications = list(set(existing.project_applications))
                existing.api_references = list(set(existing.api_references))
                existing.shader_stages = list(set(existing.shader_stages))
                existing.tags = list(set(existing.tags))
                existing.examples = list(set(existing.examples))
                
                # Update relevance score (take maximum)
                existing.relevance_score = max(existing.relevance_score, technique.relevance_score)
                existing.last_updated = datetime.now().isoformat()
            else:
                unique_techniques[key] = technique
        
        return list(unique_techniques.values())

    def update_knowledge_base(self, new_techniques: List[TechniqueEntry]):
        """Update the knowledge base with new discoveries"""
        logger.info(f"Updating knowledge base with {len(new_techniques)} new techniques")
        
        for technique in new_techniques:
            key = f"{technique.category}:{technique.name}"
            self.techniques[key] = technique
            self.discovery_history.append(f"{datetime.now().isoformat()}: {key}")
        
        self.save_knowledge_base()
        self.generate_technique_report()

    def save_knowledge_base(self):
        """Save the knowledge base to disk"""
        try:
            data = {
                "metadata": {
                    "last_updated": datetime.now().isoformat(),
                    "total_techniques": len(self.techniques),
                    "project_context": asdict(self.project_context)
                },
                "techniques": {k: asdict(v) for k, v in self.techniques.items()},
                "discovery_history": self.discovery_history[-100:]  # Keep last 100 entries
            }
            
            with open(self.knowledge_base_path, 'w') as f:
                json.dump(data, f, indent=2)
                
            logger.info(f"Knowledge base saved to {self.knowledge_base_path}")
            
        except Exception as e:
            logger.error(f"Error saving knowledge base: {e}")

    def load_knowledge_base(self):
        """Load the knowledge base from disk"""
        try:
            if self.knowledge_base_path.exists():
                with open(self.knowledge_base_path, 'r') as f:
                    data = json.load(f)
                
                self.techniques = {
                    k: TechniqueEntry(**v) 
                    for k, v in data.get("techniques", {}).items()
                }
                self.discovery_history = data.get("discovery_history", [])
                
                logger.info(f"Loaded {len(self.techniques)} techniques from knowledge base")
            else:
                logger.info("No existing knowledge base found, starting fresh")
                
        except Exception as e:
            logger.error(f"Error loading knowledge base: {e}")

    def generate_technique_report(self):
        """Generate a human-readable technique report"""
        report_path = Path("findings/technique_discovery_report.md")
        
        try:
            with open(report_path, 'w') as f:
                f.write("# DX12/DXR Technique Discovery Report\n\n")
                f.write(f"Generated: {datetime.now().isoformat()}\n")
                f.write(f"Total Techniques: {len(self.techniques)}\n\n")
                
                # Group by category
                categories = {}
                for technique in self.techniques.values():
                    if technique.category not in categories:
                        categories[technique.category] = []
                    categories[technique.category].append(technique)
                
                # Sort categories by relevance
                for category in sorted(categories.keys()):
                    techniques = categories[category]
                    techniques.sort(key=lambda x: x.relevance_score, reverse=True)
                    
                    f.write(f"## {category} ({len(techniques)} techniques)\n\n")
                    
                    for technique in techniques:
                        f.write(f"### {technique.name}\n")
                        f.write(f"**Relevance:** {technique.relevance_score:.2f}\n\n")
                        f.write(f"**Description:** {technique.description}\n\n")
                        
                        if technique.project_applications:
                            f.write("**Project Applications:**\n")
                            for app in technique.project_applications:
                                f.write(f"- {app}\n")
                            f.write("\n")
                        
                        if technique.tags:
                            f.write(f"**Tags:** {', '.join(technique.tags)}\n\n")
                        
                        if technique.examples:
                            f.write("**Usage Examples:**\n")
                            for example in technique.examples:
                                f.write(f"- {example}\n")
                            f.write("\n")
                        
                        f.write("---\n\n")
            
            logger.info(f"Technique report generated: {report_path}")
            
        except Exception as e:
            logger.error(f"Error generating technique report: {e}")

    def get_relevant_techniques(self, query: str, limit: int = 10) -> List[TechniqueEntry]:
        """Get techniques relevant to a specific query"""
        query_lower = query.lower()
        scored_techniques = []
        
        for technique in self.techniques.values():
            score = 0.0
            
            # Check name and description
            if query_lower in technique.name.lower():
                score += 2.0
            if query_lower in technique.description.lower():
                score += 1.5
            
            # Check tags and applications
            for tag in technique.tags:
                if query_lower in tag.lower():
                    score += 1.0
            for app in technique.project_applications:
                if query_lower in app.lower():
                    score += 1.2
            
            # Boost by base relevance score
            score += technique.relevance_score * 0.5
            
            if score > 0:
                scored_techniques.append((score, technique))
        
        # Sort by score and return top results
        scored_techniques.sort(key=lambda x: x[0], reverse=True)
        return [t[1] for t in scored_techniques[:limit]]

    def run_discovery_cycle(self, mcp_client):
        """Run a single discovery cycle"""
        logger.info("Starting discovery cycle")
        
        try:
            # Discover new techniques
            new_techniques = self.discover_techniques_from_mcp(mcp_client)
            
            if new_techniques:
                self.update_knowledge_base(new_techniques)
                logger.info(f"Discovery cycle completed: {len(new_techniques)} new techniques")
            else:
                logger.info("Discovery cycle completed: no new techniques found")
                
        except Exception as e:
            logger.error(f"Error in discovery cycle: {e}")

    async def run_background_agent(self, mcp_client, interval_hours: float = 24.0):
        """Run the agent in background mode"""
        logger.info(f"Starting background agent (interval: {interval_hours}h)")
        
        while True:
            try:
                self.run_discovery_cycle(mcp_client)
                await asyncio.sleep(interval_hours * 3600)  # Convert hours to seconds
            except KeyboardInterrupt:
                logger.info("Background agent stopped by user")
                break
            except Exception as e:
                logger.error(f"Background agent error: {e}")
                await asyncio.sleep(300)  # Wait 5 minutes before retrying

# Example usage
if __name__ == "__main__":
    # This would be integrated with your MCP client
    # agent = DX12TechniqueAgent()
    # agent.run_discovery_cycle(mcp_client)
    pass




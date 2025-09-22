#!/usr/bin/env python3
"""
DX12 Technique Agent Runner

Main entry point for running the DX12 technique discovery agent.
This script can be run as a standalone background service or integrated
into the main PlasmaDX application.

Author: AI Assistant
Created: 2025-01-19
"""

import asyncio
import argparse
import logging
import sys
from pathlib import Path
import signal
from datetime import datetime

# Add the agents directory to the path
sys.path.append(str(Path(__file__).parent))

from dx12_technique_agent import DX12TechniqueAgent
from mcp_integration import MCPClient, run_technique_discovery

# Configure logging
def setup_logging(verbose: bool = False):
    """Setup logging configuration"""
    level = logging.DEBUG if verbose else logging.INFO
    
    # Ensure logs directory exists
    log_dir = Path("logs")
    log_dir.mkdir(exist_ok=True)
    
    # Ensure agent output directory exists
    agent_dir = Path("agent/AdvancedTechniqueWebSearches")
    agent_dir.mkdir(parents=True, exist_ok=True)
    
    logging.basicConfig(
        level=level,
        format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
        handlers=[
            logging.FileHandler(log_dir / 'technique_agent.log'),
            logging.StreamHandler()
        ]
    )

class TechniqueAgentRunner:
    """Main runner for the technique discovery agent"""
    
    def __init__(self, verbose: bool = False):
        self.verbose = verbose
        self.agent = None
        self.mcp_client = None
        self.running = False
        self.logger = logging.getLogger('AgentRunner')
        
    def initialize(self):
        """Initialize the agent and MCP client"""
        self.logger.info("Initializing technique discovery agent")
        
        try:
            self.agent = DX12TechniqueAgent()
            self.mcp_client = MCPClient()
            
            self.logger.info("Agent initialization completed")
            return True
            
        except Exception as e:
            self.logger.error(f"Failed to initialize agent: {e}")
            return False
    
    def run_single_discovery(self):
        """Run a single discovery cycle"""
        self.logger.info("Running single discovery cycle")
        
        if not self.agent:
            self.logger.error("Agent not initialized")
            return False
            
        try:
            self.agent.run_discovery_cycle(self.mcp_client)
            self.logger.info("Single discovery cycle completed successfully")
            return True
            
        except Exception as e:
            self.logger.error(f"Discovery cycle failed: {e}")
            return False
    
    async def run_background_mode(self, interval_hours: float = 24.0):
        """Run the agent in background mode"""
        self.logger.info(f"Starting background mode (interval: {interval_hours}h)")
        self.running = True
        
        try:
            await self.agent.run_background_agent(self.mcp_client, interval_hours)
        except KeyboardInterrupt:
            self.logger.info("Background mode stopped by user")
        except Exception as e:
            self.logger.error(f"Background mode error: {e}")
        finally:
            self.running = False
    
    def query_techniques(self, query: str, limit: int = 10):
        """Query the knowledge base for relevant techniques"""
        if not self.agent:
            self.logger.error("Agent not initialized")
            return []
            
        techniques = self.agent.get_relevant_techniques(query, limit)
        
        self.logger.info(f"Found {len(techniques)} techniques for query: {query}")
        return techniques
    
    def generate_report(self):
        """Generate a technique discovery report"""
        if not self.agent:
            self.logger.error("Agent not initialized")
            return False
            
        try:
            self.agent.generate_technique_report()
            self.logger.info("Technique report generated successfully")
            return True
            
        except Exception as e:
            self.logger.error(f"Failed to generate report: {e}")
            return False
    
    def show_statistics(self):
        """Show knowledge base statistics"""
        if not self.agent:
            self.logger.error("Agent not initialized")
            return
            
        total_techniques = len(self.agent.techniques)
        categories = {}
        
        for technique in self.agent.techniques.values():
            if technique.category not in categories:
                categories[technique.category] = 0
            categories[technique.category] += 1
        
        print(f"\n=== DX12 Technique Knowledge Base Statistics ===")
        print(f"Total Techniques: {total_techniques}")
        print(f"Last Updated: {datetime.now().isoformat()}")
        print(f"\nTechniques by Category:")
        
        for category, count in sorted(categories.items()):
            print(f"  {category}: {count}")
        
        # Show high-relevance techniques
        high_relevance = [t for t in self.agent.techniques.values() if t.relevance_score >= 0.8]
        print(f"\nHigh Relevance Techniques (≥0.8): {len(high_relevance)}")
        
        for technique in sorted(high_relevance, key=lambda x: x.relevance_score, reverse=True)[:5]:
            print(f"  {technique.name} ({technique.category}): {technique.relevance_score:.2f}")

def signal_handler(signum, frame):
    """Handle shutdown signals gracefully"""
    print(f"\nReceived signal {signum}, shutting down...")
    sys.exit(0)

def main():
    """Main entry point"""
    parser = argparse.ArgumentParser(description="DX12 Technique Discovery Agent")
    parser.add_argument("--verbose", "-v", action="store_true", help="Enable verbose logging")
    parser.add_argument("--mode", choices=["single", "background", "query", "report", "stats"], 
                       default="single", help="Operation mode")
    parser.add_argument("--interval", type=float, default=24.0, 
                       help="Background mode interval in hours")
    parser.add_argument("--query", type=str, help="Query string for query mode")
    parser.add_argument("--limit", type=int, default=10, help="Limit for query results")
    
    args = parser.parse_args()
    
    # Setup logging
    setup_logging(args.verbose)
    
    # Setup signal handlers
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)
    
    # Create and initialize runner
    runner = TechniqueAgentRunner(args.verbose)
    
    if not runner.initialize():
        sys.exit(1)
    
    # Run based on mode
    if args.mode == "single":
        success = runner.run_single_discovery()
        sys.exit(0 if success else 1)
        
    elif args.mode == "background":
        asyncio.run(runner.run_background_mode(args.interval))
        
    elif args.mode == "query":
        if not args.query:
            print("Error: --query is required for query mode")
            sys.exit(1)
            
        techniques = runner.query_techniques(args.query, args.limit)
        
        print(f"\n=== Query Results: '{args.query}' ===")
        for i, technique in enumerate(techniques, 1):
            print(f"{i}. {technique.name} ({technique.category})")
            print(f"   Relevance: {technique.relevance_score:.2f}")
            print(f"   Description: {technique.description}")
            if technique.project_applications:
                print(f"   Applications: {', '.join(technique.project_applications[:2])}")
            print()
        
    elif args.mode == "report":
        success = runner.generate_report()
        sys.exit(0 if success else 1)
        
    elif args.mode == "stats":
        runner.show_statistics()

if __name__ == "__main__":
    main()





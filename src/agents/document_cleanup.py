#!/usr/bin/env python3
"""
Document Cleanup Utility for DX12 Technique Agent

This utility finds and moves scattered technique-related documents
to the centralized agent/AdvancedTechniqueWebSearches folder.

Author: AI Assistant
Created: 2025-01-19
"""

import os
import shutil
import logging
from pathlib import Path
from typing import List, Dict, Tuple
import glob

logger = logging.getLogger('DocumentCleanup')

class DocumentCleanup:
    """Handles cleanup and organization of technique-related documents"""
    
    def __init__(self, target_dir: str = "agent/AdvancedTechniqueWebSearches"):
        self.target_dir = Path(target_dir)
        self.target_dir.mkdir(parents=True, exist_ok=True)
        self.logger = logging.getLogger('DocumentCleanup')
        
        # Patterns for technique-related documents (ONLY agent-generated files)
        self.technique_patterns = [
            # Only agent-generated technique files
            "**/technique_knowledge_base*.json",
            "**/technique_discovery_report*.md", 
            "**/technique_knowledge_base*.db",
            "**/technique_agent*.log",
            "**/technique_agent*.txt",
            "**/document_cleanup_report*.md",
            "**/AGENT_*.md",
            "**/DX12_TECHNIQUE_AGENT_*.md",
            "**/TECHNIQUE_AGENT_*.md"
        ]
        
        # Directories to exclude from search
        self.exclude_dirs = {
            "agent/AdvancedTechniqueWebSearches",  # Target directory
            "changes",  # CRITICAL: Exclude change request files
            "results",  # CRITICAL: Exclude result files
            "findings",  # CRITICAL: Exclude findings files
            "node_modules",
            ".git",
            "build",
            "build-vs2022", 
            "bin",
            "lib",
            "include",
            "tools",
            "PIX",
            "screenshot",
            "logs",  # Keep logs in their original location
            ".vscode",
            ".vs"
        }
    
    def find_scattered_documents(self, root_dir: str = ".") -> List[Path]:
        """Find all technique-related documents scattered around the project"""
        self.logger.info(f"Searching for scattered documents in {root_dir}")
        
        found_documents = []
        root_path = Path(root_dir)
        
        for pattern in self.technique_patterns:
            try:
                # Use glob to find matching files
                matches = list(root_path.glob(pattern))
                
                for match in matches:
                    # Skip if in excluded directory
                    if self._is_excluded(match):
                        continue
                    
                    # Skip if already in target directory
                    if self.target_dir in match.parents:
                        continue
                    
                    # Skip if it's a directory
                    if match.is_dir():
                        continue
                    
                    found_documents.append(match)
                    
            except Exception as e:
                self.logger.warning(f"Error searching pattern {pattern}: {e}")
                continue
        
        # Remove duplicates
        found_documents = list(set(found_documents))
        
        self.logger.info(f"Found {len(found_documents)} scattered documents")
        return found_documents
    
    def _is_excluded(self, file_path: Path) -> bool:
        """Check if a file path should be excluded from cleanup"""
        for exclude_dir in self.exclude_dirs:
            if exclude_dir in str(file_path):
                return True
        return False
    
    def move_document(self, source_path: Path, preserve_structure: bool = False) -> bool:
        """Move a single document to the target directory"""
        try:
            if preserve_structure:
                # Preserve directory structure
                relative_path = source_path.relative_to(Path("."))
                target_path = self.target_dir / relative_path
                target_path.parent.mkdir(parents=True, exist_ok=True)
            else:
                # Flatten structure - just use filename
                target_path = self.target_dir / source_path.name
                
                # Handle name conflicts
                counter = 1
                original_target = target_path
                while target_path.exists():
                    stem = original_target.stem
                    suffix = original_target.suffix
                    target_path = self.target_dir / f"{stem}_{counter}{suffix}"
                    counter += 1
            
            # Move the file
            shutil.move(str(source_path), str(target_path))
            self.logger.info(f"Moved: {source_path} -> {target_path}")
            return True
            
        except Exception as e:
            self.logger.error(f"Failed to move {source_path}: {e}")
            return False
    
    def cleanup_scattered_documents(self, root_dir: str = ".", 
                                  preserve_structure: bool = False,
                                  dry_run: bool = False) -> Dict[str, List[str]]:
        """Clean up all scattered documents"""
        self.logger.info("Starting document cleanup")
        
        found_documents = self.find_scattered_documents(root_dir)
        
        results = {
            "moved": [],
            "failed": [],
            "skipped": []
        }
        
        for doc_path in found_documents:
            try:
                if dry_run:
                    self.logger.info(f"[DRY RUN] Would move: {doc_path}")
                    results["moved"].append(str(doc_path))
                    continue
                
                # Check if target already exists
                target_path = self.target_dir / doc_path.name
                if target_path.exists():
                    self.logger.warning(f"Target already exists, skipping: {doc_path}")
                    results["skipped"].append(str(doc_path))
                    continue
                
                # Move the document
                if self.move_document(doc_path, preserve_structure):
                    results["moved"].append(str(doc_path))
                else:
                    results["failed"].append(str(doc_path))
                    
            except Exception as e:
                self.logger.error(f"Error processing {doc_path}: {e}")
                results["failed"].append(str(doc_path))
        
        self.logger.info(f"Cleanup completed: {len(results['moved'])} moved, "
                        f"{len(results['failed'])} failed, {len(results['skipped'])} skipped")
        
        return results
    
    def create_cleanup_report(self, results: Dict[str, List[str]], 
                            report_path: str = None) -> Path:
        """Create a cleanup report"""
        if report_path is None:
            report_path = self.target_dir / "document_cleanup_report.md"
        else:
            report_path = Path(report_path)
        
        try:
            with open(report_path, 'w') as f:
                f.write("# Document Cleanup Report\n\n")
                f.write(f"Generated: {Path().cwd()}\n")
                f.write(f"Target Directory: {self.target_dir}\n\n")
                
                f.write(f"## Summary\n\n")
                f.write(f"- **Moved**: {len(results['moved'])} documents\n")
                f.write(f"- **Failed**: {len(results['failed'])} documents\n")
                f.write(f"- **Skipped**: {len(results['skipped'])} documents\n\n")
                
                if results['moved']:
                    f.write("## Moved Documents\n\n")
                    for doc in results['moved']:
                        f.write(f"- `{doc}`\n")
                    f.write("\n")
                
                if results['failed']:
                    f.write("## Failed Moves\n\n")
                    for doc in results['failed']:
                        f.write(f"- `{doc}`\n")
                    f.write("\n")
                
                if results['skipped']:
                    f.write("## Skipped Documents\n\n")
                    for doc in results['skipped']:
                        f.write(f"- `{doc}`\n")
                    f.write("\n")
                
                f.write("## Cleanup Patterns Used\n\n")
                for pattern in self.technique_patterns:
                    f.write(f"- `{pattern}`\n")
                
                f.write("\n## Excluded Directories\n\n")
                for exclude_dir in self.exclude_dirs:
                    f.write(f"- `{exclude_dir}`\n")
            
            self.logger.info(f"Cleanup report created: {report_path}")
            return report_path
            
        except Exception as e:
            self.logger.error(f"Failed to create cleanup report: {e}")
            raise
    
    def list_current_documents(self) -> List[Path]:
        """List all documents currently in the target directory"""
        if not self.target_dir.exists():
            return []
        
        documents = []
        for pattern in ["**/*.json", "**/*.md", "**/*.db", "**/*.log", "**/*.txt"]:
            documents.extend(self.target_dir.glob(pattern))
        
        return sorted(documents)
    
    def get_directory_stats(self) -> Dict[str, int]:
        """Get statistics about the target directory"""
        if not self.target_dir.exists():
            return {"total_files": 0, "by_extension": {}}
        
        stats = {"total_files": 0, "by_extension": {}}
        
        for file_path in self.target_dir.rglob("*"):
            if file_path.is_file():
                stats["total_files"] += 1
                ext = file_path.suffix.lower()
                stats["by_extension"][ext] = stats["by_extension"].get(ext, 0) + 1
        
        return stats

def main():
    """Main function for command-line usage"""
    import argparse
    
    parser = argparse.ArgumentParser(description="Clean up scattered technique documents")
    parser.add_argument("--root", default=".", help="Root directory to search")
    parser.add_argument("--preserve-structure", action="store_true", 
                       help="Preserve directory structure when moving")
    parser.add_argument("--dry-run", action="store_true", 
                       help="Show what would be moved without actually moving")
    parser.add_argument("--target", default="agent/AdvancedTechniqueWebSearches",
                       help="Target directory for moved documents")
    
    args = parser.parse_args()
    
    # Setup logging
    logging.basicConfig(level=logging.INFO, 
                       format='%(asctime)s - %(name)s - %(levelname)s - %(message)s')
    
    # Create cleanup instance
    cleanup = DocumentCleanup(args.target)
    
    # Run cleanup
    results = cleanup.cleanup_scattered_documents(
        root_dir=args.root,
        preserve_structure=args.preserve_structure,
        dry_run=args.dry_run
    )
    
    # Create report
    if not args.dry_run:
        cleanup.create_cleanup_report(results)
    
    # Show summary
    print(f"\n=== Cleanup Summary ===")
    print(f"Moved: {len(results['moved'])}")
    print(f"Failed: {len(results['failed'])}")
    print(f"Skipped: {len(results['skipped'])}")
    
    if results['failed']:
        print(f"\nFailed moves:")
        for failed in results['failed']:
            print(f"  - {failed}")

if __name__ == "__main__":
    main()

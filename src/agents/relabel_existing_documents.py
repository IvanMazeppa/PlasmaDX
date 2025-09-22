#!/usr/bin/env python3
"""
Document Relabeling Utility for DX12 Technique Agent

This utility goes back and properly numbers/labels existing agent documents
with the new naming convention.

Author: AI Assistant
Created: 2025-01-19
"""

import os
import shutil
import logging
from pathlib import Path
from datetime import datetime
from typing import List, Dict

logger = logging.getLogger('DocumentRelabeler')

class DocumentRelabeler:
    """Handles relabeling of existing agent documents"""
    
    def __init__(self, agent_dir: str = "agent/AdvancedTechniqueWebSearches"):
        self.agent_dir = Path(agent_dir)
        self.logger = logging.getLogger('DocumentRelabeler')
        
        # Document type mappings
        self.document_types = {
            "technique_knowledge_base": "KB",
            "technique_discovery_report": "TECH", 
            "DX12_TECHNIQUE_AGENT_GUIDE": "GUIDE",
            "TECHNIQUE_AGENT_IMPLEMENTATION_SUMMARY": "IMPL",
            "AGENT_REORGANIZATION_SUMMARY": "REORG",
            "AGENT_CORRECTED_SUMMARY": "CORRECT",
            "document_cleanup_report": "CLEANUP"
        }
        
        # Session ID for relabeled documents
        self.relabel_session_id = datetime.now().strftime("%Y%m%d_%H%M%S")
    
    def identify_existing_documents(self) -> List[Dict]:
        """Identify existing documents that need relabeling"""
        documents = []
        
        if not self.agent_dir.exists():
            self.logger.warning(f"Agent directory {self.agent_dir} does not exist")
            return documents
        
        for file_path in self.agent_dir.iterdir():
            if file_path.is_file():
                doc_info = self._analyze_document(file_path)
                if doc_info:
                    documents.append(doc_info)
        
        return documents
    
    def _analyze_document(self, file_path: Path) -> Dict:
        """Analyze a document to determine its type and create relabeling info"""
        filename = file_path.name
        file_stem = file_path.stem
        file_suffix = file_path.suffix
        
        # Determine document type
        doc_type = None
        doc_number = None
        
        for pattern, type_code in self.document_types.items():
            if pattern in filename:
                doc_type = type_code
                break
        
        # If no specific type found, use generic
        if not doc_type:
            if file_suffix == ".json":
                doc_type = "DATA"
            elif file_suffix == ".md":
                doc_type = "DOC"
            else:
                doc_type = "FILE"
        
        # Create new filename
        new_filename = self._create_new_filename(doc_type, filename, file_suffix)
        
        return {
            "original_path": file_path,
            "original_name": filename,
            "new_name": new_filename,
            "doc_type": doc_type,
            "file_suffix": file_suffix
        }
    
    def _create_new_filename(self, doc_type: str, original_name: str, file_suffix: str) -> str:
        """Create a new filename with proper numbering and labeling"""
        # Extract any existing numbers or create new ones
        if doc_type == "KB":
            number = "001"
        elif doc_type == "TECH":
            number = "002"
        elif doc_type == "GUIDE":
            number = "003"
        elif doc_type == "IMPL":
            number = "004"
        elif doc_type == "REORG":
            number = "005"
        elif doc_type == "CORRECT":
            number = "006"
        elif doc_type == "CLEANUP":
            number = "007"
        else:
            number = "999"
        
        # Create descriptive name
        if doc_type == "KB":
            desc = "KNOWLEDGE_BASE"
        elif doc_type == "TECH":
            desc = "TECHNIQUE_DISCOVERY_REPORT"
        elif doc_type == "GUIDE":
            desc = "AGENT_USER_GUIDE"
        elif doc_type == "IMPL":
            desc = "IMPLEMENTATION_SUMMARY"
        elif doc_type == "REORG":
            desc = "REORGANIZATION_SUMMARY"
        elif doc_type == "CORRECT":
            desc = "CORRECTION_SUMMARY"
        elif doc_type == "CLEANUP":
            desc = "CLEANUP_REPORT"
        else:
            desc = "DOCUMENT"
        
        return f"{doc_type}_{number}_{desc}_{self.relabel_session_id}{file_suffix}"
    
    def relabel_documents(self, dry_run: bool = True) -> Dict[str, List[str]]:
        """Relabel existing documents"""
        self.logger.info("Starting document relabeling")
        
        documents = self.identify_existing_documents()
        
        results = {
            "relabeled": [],
            "failed": [],
            "skipped": []
        }
        
        for doc_info in documents:
            try:
                original_path = doc_info["original_path"]
                new_name = doc_info["new_name"]
                new_path = self.agent_dir / new_name
                
                # Check if target already exists
                if new_path.exists():
                    self.logger.warning(f"Target already exists, skipping: {new_name}")
                    results["skipped"].append(doc_info["original_name"])
                    continue
                
                if dry_run:
                    self.logger.info(f"[DRY RUN] Would relabel: {doc_info['original_name']} -> {new_name}")
                    results["relabeled"].append(doc_info["original_name"])
                else:
                    # Perform the relabeling
                    shutil.move(str(original_path), str(new_path))
                    self.logger.info(f"Relabeled: {doc_info['original_name']} -> {new_name}")
                    results["relabeled"].append(doc_info["original_name"])
                    
            except Exception as e:
                self.logger.error(f"Error relabeling {doc_info['original_name']}: {e}")
                results["failed"].append(doc_info["original_name"])
        
        self.logger.info(f"Relabeling completed: {len(results['relabeled'])} relabeled, "
                        f"{len(results['failed'])} failed, {len(results['skipped'])} skipped")
        
        return results
    
    def create_relabeling_report(self, results: Dict[str, List[str]], 
                               report_path: str = None) -> Path:
        """Create a relabeling report"""
        if report_path is None:
            report_path = self.agent_dir / f"REL_008_RELABELING_REPORT_{self.relabel_session_id}.md"
        else:
            report_path = Path(report_path)
        
        try:
            with open(report_path, 'w') as f:
                f.write("# Document Relabeling Report\n\n")
                f.write(f"**Generated**: {datetime.now().isoformat()}\n")
                f.write(f"**Session ID**: {self.relabel_session_id}\n")
                f.write(f"**Agent Directory**: {self.agent_dir}\n\n")
                
                f.write("## Summary\n\n")
                f.write(f"- **Relabeled**: {len(results['relabeled'])} documents\n")
                f.write(f"- **Failed**: {len(results['failed'])} documents\n")
                f.write(f"- **Skipped**: {len(results['skipped'])} documents\n\n")
                
                if results['relabeled']:
                    f.write("## Relabeled Documents\n\n")
                    for doc in results['relabeled']:
                        f.write(f"- `{doc}`\n")
                    f.write("\n")
                
                if results['failed']:
                    f.write("## Failed Relabeling\n\n")
                    for doc in results['failed']:
                        f.write(f"- `{doc}`\n")
                    f.write("\n")
                
                if results['skipped']:
                    f.write("## Skipped Documents\n\n")
                    for doc in results['skipped']:
                        f.write(f"- `{doc}`\n")
                    f.write("\n")
                
                f.write("## New Naming Convention\n\n")
                f.write("Documents now follow this pattern:\n")
                f.write("`{TYPE}_{NUMBER}_{DESCRIPTION}_{SESSION_ID}.{EXTENSION}`\n\n")
                
                f.write("### Document Types\n\n")
                for pattern, type_code in self.document_types.items():
                    f.write(f"- **{type_code}**: {pattern}\n")
                
                f.write("\n### Examples\n\n")
                f.write("- `KB_001_KNOWLEDGE_BASE_20250119_123456.json`\n")
                f.write("- `TECH_002_TECHNIQUE_DISCOVERY_REPORT_20250119_123456.md`\n")
                f.write("- `GUIDE_003_AGENT_USER_GUIDE_20250119_123456.md`\n")
            
            self.logger.info(f"Relabeling report created: {report_path}")
            return report_path
            
        except Exception as e:
            self.logger.error(f"Failed to create relabeling report: {e}")
            raise

def main():
    """Main function for command-line usage"""
    import argparse
    
    parser = argparse.ArgumentParser(description="Relabel existing agent documents")
    parser.add_argument("--agent-dir", default="agent/AdvancedTechniqueWebSearches",
                       help="Agent directory path")
    parser.add_argument("--dry-run", action="store_true", 
                       help="Show what would be relabeled without actually relabeling")
    
    args = parser.parse_args()
    
    # Setup logging
    logging.basicConfig(level=logging.INFO, 
                       format='%(asctime)s - %(name)s - %(levelname)s - %(message)s')
    
    # Create relabeler instance
    relabeler = DocumentRelabeler(args.agent_dir)
    
    # Run relabeling
    results = relabeler.relabel_documents(dry_run=args.dry_run)
    
    # Create report
    relabeler.create_relabeling_report(results)
    
    # Show summary
    print(f"\n=== Relabeling Summary ===")
    print(f"Relabeled: {len(results['relabeled'])}")
    print(f"Failed: {len(results['failed'])}")
    print(f"Skipped: {len(results['skipped'])}")
    
    if results['failed']:
        print(f"\nFailed relabeling:")
        for failed in results['failed']:
            print(f"  - {failed}")

if __name__ == "__main__":
    main()

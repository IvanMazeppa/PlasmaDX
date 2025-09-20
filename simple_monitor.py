#!/usr/bin/env python3
"""
Simple monitor for workflow_claude folder - no external dependencies required.
"""

import os
import time
import json
from pathlib import Path
from software_architect_agent import SoftwareArchitectAgent

class SimpleWorkflowMonitor:
    def __init__(self, watch_dir="/workspace/workflow_claude"):
        self.watch_dir = watch_dir
        self.processed_files = set()
        self.architect_agent = SoftwareArchitectAgent()
        
        # Create directory if it doesn't exist
        os.makedirs(watch_dir, exist_ok=True)
        
    def get_files_in_directory(self):
        """Get all files in the watch directory, excluding generated files"""
        try:
            files = []
            for f in os.listdir(self.watch_dir):
                file_path = os.path.join(self.watch_dir, f)
                if (os.path.isfile(file_path) and 
                    not f.startswith('summary_') and 
                    not f.startswith('architect_response_') and
                    not f.endswith('.json')):
                    files.append(file_path)
            return files
        except OSError:
            return []
    
    def process_document(self, file_path):
        """Process the document by sending it to Software Architect agent"""
        try:
            # Read the document content
            with open(file_path, 'r', encoding='utf-8') as f:
                content = f.read()
            
            print(f"\n{'='*60}")
            print(f"NEW DOCUMENT DETECTED: {os.path.basename(file_path)}")
            print(f"{'='*60}")
            print(f"Content length: {len(content)} characters")
            
            # Process with Software Architect agent
            print("Sending to Software Architect agent...")
            response = self.architect_agent.process_document(content, file_path)
            
            # Save the response
            response_file = self.architect_agent.save_response(response, file_path)
            
            print(f"\nSOFTWARE ARCHITECT ANALYSIS COMPLETED!")
            print(f"Response saved to: {response_file}")
            print(f"\nANALYSIS SUMMARY:")
            print(f"  - Content type: {response['analysis']['content_type']}")
            print(f"  - Complexity: {response['analysis']['complexity_score']}")
            print(f"  - Patterns found: {', '.join(response['analysis']['architectural_patterns'])}")
            print(f"  - Key recommendations: {len(response['recommendations'])} items")
            
            # Also save a summary file for easy reading
            summary_file = f"/workspace/workflow_claude/summary_{os.path.basename(file_path)}.txt"
            with open(summary_file, 'w', encoding='utf-8') as f:
                f.write(f"SOFTWARE ARCHITECT ANALYSIS SUMMARY\n")
                f.write(f"=====================================\n\n")
                f.write(f"Original file: {file_path}\n")
                f.write(f"Processed at: {response['timestamp']}\n\n")
                f.write(f"CONTENT ANALYSIS:\n")
                f.write(f"- Type: {response['analysis']['content_type']}\n")
                f.write(f"- Complexity: {response['analysis']['complexity_score']}\n")
                f.write(f"- Key components: {len(response['analysis']['key_components'])}\n")
                f.write(f"- Architectural patterns: {', '.join(response['analysis']['architectural_patterns'])}\n")
                f.write(f"- Potential issues: {', '.join(response['analysis']['potential_issues'])}\n\n")
                f.write(f"ARCHITECTURAL GUIDANCE:\n")
                for i, guidance in enumerate(response['architectural_guidance'], 1):
                    f.write(f"{i}. {guidance}\n")
                f.write(f"\nRECOMMENDATIONS:\n")
                for i, rec in enumerate(response['recommendations'], 1):
                    f.write(f"{i}. {rec}\n")
            
            print(f"Summary saved to: {summary_file}")
            print(f"{'='*60}\n")
            
        except Exception as e:
            print(f"Error processing document {file_path}: {e}")
            import traceback
            traceback.print_exc()
    
    def monitor(self):
        """Main monitoring loop"""
        print(f"Starting monitor for: {self.watch_dir}")
        print("Waiting for documents to appear...")
        print("Press Ctrl+C to stop monitoring")
        
        try:
            while True:
                # Get current files in directory
                current_files = self.get_files_in_directory()
                
                # Check for new files
                for file_path in current_files:
                    if file_path not in self.processed_files:
                        self.processed_files.add(file_path)
                        self.process_document(file_path)
                
                # Sleep for a short interval
                time.sleep(2)
                
        except KeyboardInterrupt:
            print("\nStopping monitor...")

def main():
    monitor = SimpleWorkflowMonitor()
    monitor.monitor()

if __name__ == "__main__":
    main()
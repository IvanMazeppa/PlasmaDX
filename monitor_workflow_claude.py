#!/usr/bin/env python3
"""
Monitor workflow_claude folder for new documents and process them with Software Architect agent.
"""

import os
import time
import json
import requests
from pathlib import Path
from watchdog.observers import Observer
from watchdog.events import FileSystemEventHandler
from software_architect_agent import SoftwareArchitectAgent

class WorkflowClaudeHandler(FileSystemEventHandler):
    def __init__(self):
        self.processed_files = set()
        self.architect_agent = SoftwareArchitectAgent()
        
    def on_created(self, event):
        if not event.is_directory:
            file_path = event.src_path
            if file_path not in self.processed_files:
                self.processed_files.add(file_path)
                print(f"New document detected: {file_path}")
                self.process_document(file_path)
    
    def on_modified(self, event):
        if not event.is_directory:
            file_path = event.src_path
            if file_path not in self.processed_files:
                self.processed_files.add(file_path)
                print(f"Modified document detected: {file_path}")
                self.process_document(file_path)
    
    def process_document(self, file_path):
        """Process the document by sending it to Software Architect agent"""
        try:
            # Read the document content
            with open(file_path, 'r', encoding='utf-8') as f:
                content = f.read()
            
            print(f"Processing document: {os.path.basename(file_path)}")
            print(f"Content length: {len(content)} characters")
            
            # Process with Software Architect agent
            print("Sending to Software Architect agent...")
            response = self.architect_agent.process_document(content, file_path)
            
            # Save the response
            response_file = self.architect_agent.save_response(response, file_path)
            
            print(f"Software Architect analysis completed!")
            print(f"Response saved to: {response_file}")
            print(f"Analysis summary:")
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
            
        except Exception as e:
            print(f"Error processing document {file_path}: {e}")
            import traceback
            traceback.print_exc()

def main():
    # Create workflow_claude directory if it doesn't exist
    workflow_dir = "/workspace/workflow_claude"
    os.makedirs(workflow_dir, exist_ok=True)
    
    print(f"Starting monitor for: {workflow_dir}")
    print("Waiting for documents to appear...")
    
    # Set up file system observer
    event_handler = WorkflowClaudeHandler()
    observer = Observer()
    observer.schedule(event_handler, workflow_dir, recursive=False)
    
    # Start monitoring
    observer.start()
    
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\nStopping monitor...")
        observer.stop()
    
    observer.join()

if __name__ == "__main__":
    main()
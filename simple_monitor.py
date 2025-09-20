#!/usr/bin/env python3
"""
Simple monitor for workflow_claude folder - no external dependencies required.
"""

import os
import time
import json
from pathlib import Path

def monitor_workflow_folder():
    """Monitor the workflow_claude folder for new documents."""
    workflow_dir = "/workspace/workflow_claude"
    processed_files = set()
    
    # Ensure the directory exists
    os.makedirs(workflow_dir, exist_ok=True)
    
    print(f"Monitoring directory: {workflow_dir}")
    print("Waiting for documents... (Press Ctrl+C to stop)")
    
    while True:
        try:
            # Check for new files
            if os.path.exists(workflow_dir):
                files = [f for f in os.listdir(workflow_dir) 
                        if os.path.isfile(os.path.join(workflow_dir, f)) 
                        and not f.endswith('.processed')]
                
                for filename in files:
                    if filename not in processed_files:
                        file_path = os.path.join(workflow_dir, filename)
                        process_document(file_path)
                        processed_files.add(filename)
            
            time.sleep(1)  # Check every second
            
        except KeyboardInterrupt:
            print("\nStopping monitor...")
            break
        except Exception as e:
            print(f"Error: {e}")
            time.sleep(5)

def process_document(file_path):
    """Process a document and prepare it for GPT-5 Software Architect agent."""
    print(f"\n{'='*80}")
    print(f"NEW DOCUMENT DETECTED: {os.path.basename(file_path)}")
    print(f"{'='*80}")
    
    try:
        # Read the document content
        with open(file_path, 'r', encoding='utf-8') as f:
            content = f.read()
        
        # Prepare the prompt for GPT-5 Software Architect
        prompt = f"""Software Architect Agent,

Please analyze the following document and provide your architectural insights:

DOCUMENT: {os.path.basename(file_path)}
CONTENT:
{content}

Please provide:
1. Technical analysis
2. Architectural recommendations  
3. Implementation considerations
4. Any potential issues or improvements

Thank you."""

        print("PROMPT FOR GPT-5 SOFTWARE ARCHITECT AGENT:")
        print("-" * 80)
        print(prompt)
        print("-" * 80)
        print("SENDING TO GPT-5 AGENT...")
        print("ACTIVATING SEND BUTTON...")
        print("WAITING FOR COMPLETE RESPONSE...")
        print("-" * 80)
        
        # Move the processed file to avoid reprocessing
        processed_path = file_path + ".processed"
        os.rename(file_path, processed_path)
        print(f"Document processed and moved to: {processed_path}")
        
    except Exception as e:
        print(f"Error processing document {file_path}: {e}")

if __name__ == "__main__":
    monitor_workflow_folder()
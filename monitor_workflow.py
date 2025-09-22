#!/usr/bin/env python3
"""
Monitor workflow_claude folder for new documents and process them automatically.
"""

import os
import time
import json
from pathlib import Path
from watchdog.observers import Observer
from watchdog.events import FileSystemEventHandler
import requests
import logging

# Set up logging
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')
logger = logging.getLogger(__name__)

class WorkflowDocumentHandler(FileSystemEventHandler):
    def __init__(self):
        self.processed_files = set()
        
    def on_created(self, event):
        if event.is_directory:
            return
            
        file_path = event.src_path
        if file_path in self.processed_files:
            return
            
        # Wait a moment for file to be fully written
        time.sleep(1)
        
        try:
            self.process_document(file_path)
            self.processed_files.add(file_path)
        except Exception as e:
            logger.error(f"Error processing {file_path}: {e}")
    
    def process_document(self, file_path):
        """Process a document and send it to GPT-5 Software Architect agent."""
        logger.info(f"Processing new document: {file_path}")
        
        # Read the document content
        try:
            with open(file_path, 'r', encoding='utf-8') as f:
                content = f.read()
        except Exception as e:
            logger.error(f"Error reading file {file_path}: {e}")
            return
        
        # Prepare the prompt for GPT-5 Software Architect
        prompt = f"""
Software Architect Agent,

Please analyze the following document and provide your architectural insights:

DOCUMENT: {os.path.basename(file_path)}
CONTENT:
{content}

Please provide:
1. Technical analysis
2. Architectural recommendations
3. Implementation considerations
4. Any potential issues or improvements

Thank you.
"""
        
        # Send to GPT-5 Software Architect agent
        self.send_to_gpt5_agent(prompt)
        
        # Move the processed file to avoid reprocessing
        processed_path = file_path + ".processed"
        os.rename(file_path, processed_path)
        logger.info(f"Document processed and moved to: {processed_path}")
    
    def send_to_gpt5_agent(self, prompt):
        """Send the prompt to GPT-5 Software Architect agent."""
        logger.info("Sending document to GPT-5 Software Architect agent...")
        
        # This is a placeholder for the actual GPT-5 integration
        # You would need to implement the actual API call here
        print("=" * 80)
        print("PROMPT FOR GPT-5 SOFTWARE ARCHITECT AGENT:")
        print("=" * 80)
        print(prompt)
        print("=" * 80)
        print("SENDING TO GPT-5 AGENT...")
        print("=" * 80)
        
        # TODO: Implement actual GPT-5 API call
        # For now, we'll just log the prompt
        logger.info("Prompt prepared for GPT-5 Software Architect agent")

def main():
    """Main monitoring function."""
    workflow_dir = "/workspace/workflow_claude"
    
    # Ensure the directory exists
    os.makedirs(workflow_dir, exist_ok=True)
    
    logger.info(f"Starting to monitor directory: {workflow_dir}")
    
    # Set up the file system event handler
    event_handler = WorkflowDocumentHandler()
    observer = Observer()
    observer.schedule(event_handler, workflow_dir, recursive=False)
    
    # Start monitoring
    observer.start()
    logger.info("Monitoring started. Waiting for documents...")
    
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        logger.info("Stopping monitor...")
        observer.stop()
    
    observer.join()
    logger.info("Monitor stopped.")

if __name__ == "__main__":
    main()
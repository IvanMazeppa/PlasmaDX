#!/usr/bin/env python3
import os
import time
import json
from pathlib import Path

def check_for_new_files(folder_path):
    """Check for new files in the specified folder"""
    folder = Path(folder_path)
    if not folder.exists():
        return []
    
    # Get all files in the folder
    files = [f for f in folder.iterdir() if f.is_file()]
    return files

def read_file_content(file_path):
    """Read the content of a file"""
    try:
        with open(file_path, 'r', encoding='utf-8') as f:
            return f.read()
    except Exception as e:
        print(f"Error reading file {file_path}: {e}")
        return None

def main():
    folder_path = "/workspace/workflow_claude"
    print(f"Starting to monitor: {folder_path}")
    
    # Initialize with current files (should be empty)
    last_files = set()
    
    while True:
        try:
            current_files = set(check_for_new_files(folder_path))
            new_files = current_files - last_files
            
            if new_files:
                for new_file in new_files:
                    print(f"New file detected: {new_file.name}")
                    
                    # Read the file content
                    content = read_file_content(new_file)
                    if content:
                        print(f"File content length: {len(content)} characters")
                        print(f"First 200 characters: {content[:200]}...")
                        
                        # Here we would call the DXR Help tool
                        # For now, just print that we found content to process
                        print("=" * 50)
                        print("DOCUMENT READY FOR DXR HELP PROCESSING:")
                        print("=" * 50)
                        print(content)
                        print("=" * 50)
                        
                        # Move the file to avoid reprocessing
                        processed_name = f"processed_{new_file.name}"
                        new_file.rename(folder_path + "/" + processed_name)
                        print(f"File moved to: {processed_name}")
                        
                        return content  # Return the content for processing
            
            last_files = current_files
            time.sleep(1)  # Check every second
            
        except KeyboardInterrupt:
            print("Monitoring stopped by user")
            break
        except Exception as e:
            print(f"Error: {e}")
            time.sleep(1)

if __name__ == "__main__":
    main()
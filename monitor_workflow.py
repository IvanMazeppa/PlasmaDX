#!/usr/bin/env python3
import os
import time
import glob
from pathlib import Path

def monitor_folder(folder_path, check_interval=2):
    """Monitor a folder for new files and return them"""
    folder = Path(folder_path)
    folder.mkdir(exist_ok=True)
    
    # Get initial list of files
    initial_files = set(folder.glob('*'))
    
    print(f"Monitoring folder: {folder_path}")
    print(f"Initial files: {[f.name for f in initial_files]}")
    
    while True:
        try:
            # Check for new files
            current_files = set(folder.glob('*'))
            new_files = current_files - initial_files
            
            if new_files:
                for new_file in new_files:
                    if new_file.is_file():
                        print(f"New file detected: {new_file}")
                        return str(new_file)
            
            time.sleep(check_interval)
            
        except KeyboardInterrupt:
            print("Monitoring stopped by user")
            break
        except Exception as e:
            print(f"Error monitoring folder: {e}")
            time.sleep(check_interval)
    
    return None

if __name__ == "__main__":
    monitor_folder("/workspace/workflow_claude")
#!/usr/bin/env python3
"""
Simplified D3D12 PDF Parser - More robust version
Extracts API information from the D3D12 reference PDF
"""

import sys
import json
import sqlite3
import re
from pathlib import Path

def main():
    print("D3D12 PDF Parser - Simplified Version")
    print("=" * 50)
    
    # Check for PDF library
    pdf_reader = None
    try:
        import fitz  # PyMuPDF
        pdf_reader = "pymupdf"
        print("✅ Using PyMuPDF for parsing")
    except ImportError:
        try:
            import PyPDF2
            pdf_reader = "pypdf2"
            print("✅ Using PyPDF2 for parsing")
        except ImportError:
            print("❌ No PDF library found!")
            print("\nPlease install with:")
            print("  pip install pymupdf")
            print("or")
            print("  pip install PyPDF2")
            return 1
    
    # Check for PDF file
    mcp_dir = Path(__file__).parent
    pdf_path = mcp_dir / "windows-win32-api-d3d12.pdf"
    db_path = mcp_dir / "dx12_docs.db"
    
    if not pdf_path.exists():
        print(f"❌ PDF not found: {pdf_path}")
        return 1
    
    print(f"✅ Found PDF: {pdf_path.name} ({pdf_path.stat().st_size / 1024 / 1024:.1f} MB)")
    
    # Parse PDF
    print("\nParsing PDF...")
    entities = []
    
    if pdf_reader == "pymupdf":
        import fitz
        doc = fitz.open(str(pdf_path))
        
        # Just scan first 100 pages for quick test
        max_pages = min(100, len(doc))
        print(f"Scanning first {max_pages} pages of {len(doc)} total...")
        
        for page_num in range(max_pages):
            if page_num % 10 == 0:
                print(f"  Page {page_num}...")
            
            page = doc[page_num]
            text = page.get_text()
            
            # Look for D3D12 entities
            # Interfaces
            for match in re.finditer(r'\b(ID3D12\w+)\b', text):
                entities.append({
                    'name': match.group(1),
                    'type': 'interface',
                    'page': page_num
                })
            
            # Functions
            for match in re.finditer(r'\b(D3D12\w+)\s*\(', text):
                entities.append({
                    'name': match.group(1),
                    'type': 'function',
                    'page': page_num
                })
            
            # Structures
            for match in re.finditer(r'\b(D3D12_[\w_]+)\b', text):
                name = match.group(1)
                if name.upper() == name:  # All caps = likely a constant/enum
                    entities.append({
                        'name': name,
                        'type': 'enum',
                        'page': page_num
                    })
                else:
                    entities.append({
                        'name': name,
                        'type': 'structure',
                        'page': page_num
                    })
        
        doc.close()
    
    elif pdf_reader == "pypdf2":
        import PyPDF2
        with open(pdf_path, 'rb') as file:
            pdf = PyPDF2.PdfReader(file)
            
            max_pages = min(100, len(pdf.pages))
            print(f"Scanning first {max_pages} pages of {len(pdf.pages)} total...")
            
            for page_num in range(max_pages):
                if page_num % 10 == 0:
                    print(f"  Page {page_num}...")
                
                page = pdf.pages[page_num]
                text = page.extract_text()
                
                # Look for D3D12 entities (same patterns)
                for match in re.finditer(r'\b(ID3D12\w+)\b', text):
                    entities.append({
                        'name': match.group(1),
                        'type': 'interface',
                        'page': page_num
                    })
                
                for match in re.finditer(r'\b(D3D12\w+)\s*\(', text):
                    entities.append({
                        'name': match.group(1),
                        'type': 'function',
                        'page': page_num
                    })
                
                for match in re.finditer(r'\b(D3D12_[\w_]+)\b', text):
                    entities.append({
                        'name': match.group(1),
                        'type': 'structure',
                        'page': page_num
                    })
    
    # Remove duplicates
    unique_entities = {}
    for entity in entities:
        if entity['name'] not in unique_entities:
            unique_entities[entity['name']] = entity
    
    entities = list(unique_entities.values())
    print(f"\n✅ Found {len(entities)} unique entities")
    
    # Show some examples
    print("\nSample entities found:")
    for entity in entities[:10]:
        print(f"  {entity['name']} ({entity['type']}, page {entity['page']})")
    
    # Save to database
    print(f"\nUpdating database: {db_path}")
    conn = sqlite3.connect(db_path)
    cursor = conn.cursor()
    
    # Create table if needed
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS dx12_pdf_entities (
            id INTEGER PRIMARY KEY,
            name TEXT UNIQUE NOT NULL,
            type TEXT NOT NULL,
            page INTEGER,
            keywords TEXT
        )
    ''')
    
    # Insert entities
    inserted = 0
    for entity in entities:
        try:
            keywords = f"{entity['name'].lower()} {entity['type']}"
            cursor.execute('''
                INSERT OR IGNORE INTO dx12_pdf_entities (name, type, page, keywords)
                VALUES (?, ?, ?, ?)
            ''', (entity['name'], entity['type'], entity.get('page', 0), keywords))
            if cursor.rowcount > 0:
                inserted += 1
        except Exception as e:
            print(f"  Warning: Could not insert {entity['name']}: {e}")
    
    conn.commit()
    conn.close()
    
    print(f"✅ Added {inserted} new entities to database")
    
    # Save sample to JSON
    sample_file = mcp_dir / "pdf_entities_sample.json"
    with open(sample_file, 'w') as f:
        json.dump(entities[:50], f, indent=2)
    print(f"✅ Saved sample to {sample_file}")
    
    print("\n✨ PDF parsing complete!")
    return 0

if __name__ == "__main__":
    sys.exit(main())

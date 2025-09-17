#!/usr/bin/env python3
"""
Parse the D3D12 PDF documentation to extract API information
This will give us the complete D3D12 reference!
"""

import json
import sqlite3
import re
from pathlib import Path
from typing import List, Dict, Optional

# Try different PDF libraries
pdf_reader = None
try:
    import fitz  # PyMuPDF - fastest and most reliable
    pdf_reader = "pymupdf"
    print("Using PyMuPDF for PDF parsing", file=sys.stderr)
except ImportError:
    try:
        import PyPDF2
        pdf_reader = "pypdf2"
        print("Using PyPDF2 for PDF parsing", file=sys.stderr)
    except ImportError:
        try:
            import pdfplumber
            pdf_reader = "pdfplumber"
            print("Using pdfplumber for PDF parsing", file=sys.stderr)
        except ImportError:
            print("Warning: No PDF library found. Install with:", file=sys.stderr)
            print("  pip install pymupdf  (recommended)", file=sys.stderr)
            print("  pip install pypdf2   (alternative)", file=sys.stderr)
            print("  pip install pdfplumber (alternative)", file=sys.stderr)

class D3D12PDFParser:
    def __init__(self, pdf_path: Path):
        self.pdf_path = pdf_path
        self.entities = []
        
    def parse(self) -> List[Dict]:
        """Parse the PDF and extract D3D12 entities"""
        if not pdf_reader:
            print("Error: No PDF library available")
            return []
        
        print(f"Parsing {self.pdf_path.name}...")
        
        if pdf_reader == "pymupdf":
            return self._parse_with_pymupdf()
        elif pdf_reader == "pypdf2":
            return self._parse_with_pypdf2()
        elif pdf_reader == "pdfplumber":
            return self._parse_with_pdfplumber()
        
        return []
    
    def _parse_with_pymupdf(self) -> List[Dict]:
        """Parse using PyMuPDF (fastest)"""
        import fitz
        
        entities = []
        doc = fitz.open(str(self.pdf_path))
        
        print(f"PDF has {len(doc)} pages")
        
        # Track what we're currently parsing
        current_section = None
        current_entity = None
        
        for page_num, page in enumerate(doc):
            if page_num % 100 == 0:
                print(f"  Processing page {page_num}/{len(doc)}...")
            
            text = page.get_text()
            
            # Extract entities using patterns
            entities.extend(self._extract_entities_from_text(text, page_num))
        
        doc.close()
        print(f"Extracted {len(entities)} entities")
        return entities
    
    def _parse_with_pypdf2(self) -> List[Dict]:
        """Parse using PyPDF2 (fallback)"""
        import PyPDF2
        
        entities = []
        with open(self.pdf_path, 'rb') as file:
            pdf = PyPDF2.PdfReader(file)
            
            print(f"PDF has {len(pdf.pages)} pages")
            
            for page_num, page in enumerate(pdf.pages):
                if page_num % 100 == 0:
                    print(f"  Processing page {page_num}/{len(pdf.pages)}...")
                
                text = page.extract_text()
                
                # Extract entities
                entities.extend(self._extract_entities_from_text(text, page_num))
        
        print(f"Extracted {len(entities)} entities")
        return entities
    
    def _parse_with_pdfplumber(self) -> List[Dict]:
        """Parse using pdfplumber (good for tables)"""
        import pdfplumber
        
        entities = []
        with pdfplumber.open(self.pdf_path) as pdf:
            print(f"PDF has {len(pdf.pages)} pages")
            
            for page_num, page in enumerate(pdf.pages):
                if page_num % 100 == 0:
                    print(f"  Processing page {page_num}/{len(pdf.pages)}...")
                
                text = page.extract_text()
                if text:
                    entities.extend(self._extract_entities_from_text(text, page_num))
        
        print(f"Extracted {len(entities)} entities")
        return entities
    
    def _extract_entities_from_text(self, text: str, page_num: int) -> List[Dict]:
        """Extract D3D12 entities from text"""
        entities = []
        lines = text.split('\n')
        
        for i, line in enumerate(lines):
            # Interfaces (ID3D12*)
            if re.match(r'^ID3D12\w+', line):
                entities.append({
                    'name': line.split()[0] if line.split() else line,
                    'type': 'interface',
                    'category': 'Interfaces',
                    'page': page_num,
                    'description': self._get_description(lines, i)
                })
            
            # Functions (D3D12*)
            elif re.match(r'^D3D12\w+', line) and '(' in line:
                func_name = re.match(r'^(D3D12\w+)', line)
                if func_name:
                    entities.append({
                        'name': func_name.group(1),
                        'type': 'function',
                        'category': 'Functions',
                        'page': page_num,
                        'description': self._get_description(lines, i)
                    })
            
            # Structures (D3D12_*_DESC, D3D12_*_INFO, etc.)
            elif re.match(r'^D3D12_[\w_]+', line):
                struct_match = re.match(r'^(D3D12_[\w_]+)', line)
                if struct_match:
                    struct_name = struct_match.group(1)
                    # Classify by suffix
                    if struct_name.endswith('_DESC'):
                        category = 'Descriptors'
                    elif struct_name.endswith('_FLAGS'):
                        category = 'Flags'
                    elif 'RAYTRACING' in struct_name or 'DXR' in struct_name:
                        category = 'Raytracing'
                    else:
                        category = 'Structures'
                    
                    entities.append({
                        'name': struct_name,
                        'type': 'structure',
                        'category': category,
                        'page': page_num,
                        'description': self._get_description(lines, i)
                    })
            
            # Methods (starting with typical method prefixes)
            elif any(line.startswith(prefix) for prefix in 
                    ['Create', 'Get', 'Set', 'Reset', 'Release', 'Query', 
                     'Copy', 'Clear', 'Draw', 'Dispatch', 'Execute']):
                if '(' in line:
                    method_match = re.match(r'^(\w+)', line)
                    if method_match:
                        entities.append({
                            'name': method_match.group(1),
                            'type': 'method',
                            'category': 'Methods',
                            'page': page_num,
                            'description': self._get_description(lines, i)
                        })
        
        return entities
    
    def _get_description(self, lines: List[str], start_idx: int) -> str:
        """Try to extract a description from nearby lines"""
        description = ""
        
        # Look at next few lines for description
        for j in range(start_idx + 1, min(start_idx + 4, len(lines))):
            line = lines[j].strip()
            if line and not line.startswith('•') and len(line) > 10:
                description = line
                break
        
        return description[:200] if description else "D3D12 API entity"

def update_database_with_pdf(pdf_path: Path, db_path: Path):
    """Update the MCP database with PDF content"""
    parser = D3D12PDFParser(pdf_path)
    entities = parser.parse()
    
    if not entities:
        print("No entities extracted from PDF")
        return
    
    print(f"\nUpdating database with {len(entities)} entities...")
    
    conn = sqlite3.connect(db_path)
    cursor = conn.cursor()
    
    # Ensure table exists
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS dx12_pdf_entities (
            id INTEGER PRIMARY KEY,
            name TEXT UNIQUE NOT NULL,
            type TEXT NOT NULL,
            category TEXT,
            description TEXT,
            page INTEGER,
            keywords TEXT
        )
    ''')
    
    # Insert entities
    inserted = 0
    skipped = 0
    
    for entity in entities:
        try:
            # Generate keywords
            keywords = f"{entity['name'].lower()} {entity['type']} {entity['category']}".lower()
            
            cursor.execute('''
                INSERT OR REPLACE INTO dx12_pdf_entities 
                (name, type, category, description, page, keywords)
                VALUES (?, ?, ?, ?, ?, ?)
            ''', (
                entity['name'],
                entity['type'],
                entity.get('category', 'General'),
                entity.get('description', ''),
                entity.get('page', 0),
                keywords
            ))
            inserted += 1
        except Exception as e:
            print(f"  Error inserting {entity['name']}: {e}")
            skipped += 1
    
    conn.commit()
    
    # Get statistics
    cursor.execute("SELECT COUNT(*) FROM dx12_pdf_entities")
    total = cursor.fetchone()[0]
    
    cursor.execute("SELECT COUNT(DISTINCT category) FROM dx12_pdf_entities")
    categories = cursor.fetchone()[0]
    
    conn.close()
    
    print(f"\nDatabase update complete!")
    print(f"  Inserted: {inserted}")
    print(f"  Skipped: {skipped}")
    print(f"  Total entities: {total}")
    print(f"  Categories: {categories}")

def main():
    # Get paths
    mcp_dir = Path(__file__).parent
    pdf_path = mcp_dir / "windows-win32-api-d3d12.pdf"
    db_path = mcp_dir / "dx12_docs.db"
    
    if not pdf_path.exists():
        print(f"Error: PDF not found at {pdf_path}")
        return
    
    if not pdf_reader:
        print("\nNo PDF library installed!")
        print("Install one of these:")
        print("  pip install pymupdf     # Recommended - fastest")
        print("  pip install PyPDF2      # Good fallback")
        print("  pip install pdfplumber  # Good for tables")
        return
    
    # Parse and update database
    update_database_with_pdf(pdf_path, db_path)
    
    # Save raw entities for inspection
    parser = D3D12PDFParser(pdf_path)
    entities = parser.parse()
    
    if entities:
        output_file = mcp_dir / "parsed_pdf_entities.json"
        with open(output_file, 'w') as f:
            json.dump(entities[:100], f, indent=2)  # Save first 100 for review
        print(f"\nSample entities saved to {output_file}")

if __name__ == "__main__":
    main()

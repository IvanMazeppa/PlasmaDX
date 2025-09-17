#!/usr/bin/env python3
"""Quick test to see which PDF library is available"""

import sys
print(f"Python version: {sys.version}")
print("-" * 50)

# Test PDF libraries
pdf_libs = []

try:
    import fitz
    pdf_libs.append(f"✅ PyMuPDF (fitz) version {fitz.version[0]}")
    print(f"PyMuPDF installed: {fitz.__version__}")
except ImportError:
    print("❌ PyMuPDF not found")

try:
    import PyPDF2
    pdf_libs.append("✅ PyPDF2")
    print(f"PyPDF2 installed")
except ImportError:
    print("❌ PyPDF2 not found")

try:
    import pdfplumber
    pdf_libs.append("✅ pdfplumber")
    print(f"pdfplumber installed")
except ImportError:
    print("❌ pdfplumber not found")

print("-" * 50)
if pdf_libs:
    print("Available PDF libraries:")
    for lib in pdf_libs:
        print(f"  {lib}")
else:
    print("No PDF libraries found! Install with:")
    print("  pip install pymupdf")
    print("  or")
    print("  pip install PyPDF2")

# Check if PDF exists
from pathlib import Path
pdf_path = Path(__file__).parent / "windows-win32-api-d3d12.pdf"
print(f"\nPDF file exists: {pdf_path.exists()}")
if pdf_path.exists():
    print(f"PDF size: {pdf_path.stat().st_size / 1024 / 1024:.1f} MB")

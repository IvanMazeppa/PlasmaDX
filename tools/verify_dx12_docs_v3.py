#!/usr/bin/env python3
import json
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SERVER = ROOT / "src" / "mcp" / "dx12_docs_v3_server.py"


def run_server(args):
    cmd = [sys.executable, str(SERVER)] + args
    return subprocess.check_output(cmd, text=True)


def main():
    print("Listing tools...")
    print(run_server(["list-tools"]))

    print("Quick reference...")
    print(run_server(["quickref"]))

    print("Search all for 'DispatchRays'...")
    print(run_server(["search-all", "DispatchRays"]))

    print("Search stage raygen...")
    print(run_server(["search-stage", "raygen"]))

    print("Get entity D3D12_DISPATCH_RAYS_DESC...")
    print(run_server(["get", "D3D12_DISPATCH_RAYS_DESC"]))


if __name__ == "__main__":
    main()


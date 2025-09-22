#!/usr/bin/env python3
"""
DX12 Docs MCP Server (v3)

Minimal MCP-like CLI surface for Cursor/Claude integration.

Exposes tools:
- list_tools
- dx12_quick_reference
- search_all_sources
- search_dx12_api
- search_dxr_api
- search_hlsl_intrinsics
- search_by_shader_stage
- get_dx12_entity

This server uses JSON data under data/dx12_docs_v3/ for demo purposes.
Replace data files or wire into a real DB to power full coverage.
"""

import argparse
import json
import sys
from pathlib import Path
from typing import Any, Dict, List


REPO_ROOT = Path(__file__).resolve().parents[2]
DATA_DIR = REPO_ROOT / "data" / "dx12_docs_v3"


def load_json(path: Path, default: Any) -> Any:
    try:
        with path.open("r", encoding="utf-8") as f:
            return json.load(f)
    except Exception:
        return default


def list_tools() -> Dict[str, Any]:
    tools = [
        {
            "name": "list_tools",
            "description": "List available tools and brief descriptions",
            "params": {}
        },
        {
            "name": "dx12_quick_reference",
            "description": "Return coverage stats and metadata",
            "params": {}
        },
        {
            "name": "search_all_sources",
            "description": "Search across core, DXR and HLSL datasets",
            "params": {"query": "str", "limit": "int?"}
        },
        {
            "name": "search_dx12_api",
            "description": "Search core D3D12 entities",
            "params": {"query": "str", "limit": "int?"}
        },
        {
            "name": "search_dxr_api",
            "description": "Search DXR entities",
            "params": {"query": "str", "limit": "int?"}
        },
        {
            "name": "search_hlsl_intrinsics",
            "description": "Search HLSL intrinsics",
            "params": {"query": "str", "limit": "int?"}
        },
        {
            "name": "search_by_shader_stage",
            "description": "Filter HLSL intrinsics by shader stage",
            "params": {"stage": "str", "query": "str?", "limit": "int?"}
        },
        {
            "name": "get_dx12_entity",
            "description": "Get a specific entity by exact name",
            "params": {"name": "str"}
        }
    ]
    return {
        "ok": True,
        "tool": "list_tools",
        "results": tools
    }


def quick_reference() -> Dict[str, Any]:
    coverage = load_json(DATA_DIR / "coverage.json", {})
    entities = load_json(DATA_DIR / "entities.json", {})

    # Compute counts if not present
    core = entities.get("dx12_core", [])
    dxr = entities.get("dxr", [])
    hlsl = entities.get("hlsl_intrinsics", [])

    computed = {
        "total_entities": len(core) + len(dxr) + len(hlsl),
        "dxr_entities": len(dxr),
        "hlsl_intrinsics": len(hlsl)
    }

    source = coverage.get("current", {}).get("source", "computed")
    if source == "computed":
        stats = computed
    elif source == "client_stub":
        stats = coverage.get("client_stub", {}) or computed
    elif source == "v2_3":
        # Map legacy fields to unified keys
        v23 = coverage.get("v2_3", {})
        stats = {
            "total_entities": v23.get("total_entities", computed["total_entities"]),
            "dxr_entities": v23.get("dxr_entities", computed["dxr_entities"]),
            "hlsl_intrinsics": v23.get("hlsl_intrinsics", computed["hlsl_intrinsics"])
        }
    else:
        stats = computed

    return {
        "ok": True,
        "tool": "dx12_quick_reference",
        "results": [
            {
                "stats": stats,
                "source": source,
                "version_history": coverage.get("version_history", [])
            }
        ]
    }


def _match_query(text: str, query: str) -> bool:
    t = text.lower()
    for word in query.lower().split():
        if word in t:
            return True
    return False


def _search(items: List[Dict[str, Any]], query: str, limit: int) -> List[Dict[str, Any]]:
    results = []
    for it in items:
        name = it.get("name", "")
        desc = it.get("description", "")
        if _match_query(name + " " + desc, query):
            results.append(it)
        if len(results) >= limit:
            break
    return results


def search_all_sources(query: str, limit: int) -> Dict[str, Any]:
    entities = load_json(DATA_DIR / "entities.json", {})
    core = entities.get("dx12_core", [])
    dxr = entities.get("dxr", [])
    hlsl = entities.get("hlsl_intrinsics", [])

    combined = core + dxr + hlsl
    results = _search(combined, query, limit)
    return {"ok": True, "tool": "search_all_sources", "query": query, "results": results}


def search_dx12_api(query: str, limit: int) -> Dict[str, Any]:
    entities = load_json(DATA_DIR / "entities.json", {})
    core = entities.get("dx12_core", [])
    results = _search(core, query, limit)
    return {"ok": True, "tool": "search_dx12_api", "query": query, "results": results}


def search_dxr_api(query: str, limit: int) -> Dict[str, Any]:
    entities = load_json(DATA_DIR / "entities.json", {})
    dxr = entities.get("dxr", [])
    results = _search(dxr, query, limit)
    return {"ok": True, "tool": "search_dxr_api", "query": query, "results": results}


def search_hlsl_intrinsics(query: str, limit: int) -> Dict[str, Any]:
    entities = load_json(DATA_DIR / "entities.json", {})
    hlsl = entities.get("hlsl_intrinsics", [])
    results = _search(hlsl, query, limit)
    return {"ok": True, "tool": "search_hlsl_intrinsics", "query": query, "results": results}


def search_by_shader_stage(stage: str, query: str, limit: int) -> Dict[str, Any]:
    entities = load_json(DATA_DIR / "entities.json", {})
    hlsl = entities.get("hlsl_intrinsics", [])
    stage_norm = stage.strip().lower()
    filtered = [it for it in hlsl if stage_norm in [s.lower() for s in it.get("shader_stages", [])]]
    if query:
        filtered = _search(filtered, query, limit)
    else:
        filtered = filtered[:limit]
    return {
        "ok": True,
        "tool": "search_by_shader_stage",
        "stage": stage,
        "query": query,
        "results": filtered
    }


def get_dx12_entity(name: str) -> Dict[str, Any]:
    entities = load_json(DATA_DIR / "entities.json", {})
    for group in ("dx12_core", "dxr", "hlsl_intrinsics"):
        for it in entities.get(group, []):
            if it.get("name") == name:
                return {"ok": True, "tool": "get_dx12_entity", "results": [it]}
    return {"ok": False, "tool": "get_dx12_entity", "error": {"code": "NOT_FOUND", "message": f"Entity '{name}' not found"}}


def main(argv: List[str]) -> int:
    parser = argparse.ArgumentParser(description="DX12 Docs MCP v3 (CLI)")
    sub = parser.add_subparsers(dest="cmd")

    sub.add_parser("list-tools")
    sub.add_parser("quickref")

    p_all = sub.add_parser("search-all")
    p_all.add_argument("query")
    p_all.add_argument("--limit", type=int, default=20)

    p_core = sub.add_parser("search-core")
    p_core.add_argument("query")
    p_core.add_argument("--limit", type=int, default=10)

    p_dxr = sub.add_parser("search-dxr")
    p_dxr.add_argument("query")
    p_dxr.add_argument("--limit", type=int, default=10)

    p_hlsl = sub.add_parser("search-hlsl")
    p_hlsl.add_argument("query")
    p_hlsl.add_argument("--limit", type=int, default=10)

    p_stage = sub.add_parser("search-stage")
    p_stage.add_argument("stage")
    p_stage.add_argument("--query", default="")
    p_stage.add_argument("--limit", type=int, default=15)

    p_get = sub.add_parser("get")
    p_get.add_argument("name")

    args = parser.parse_args(argv)

    if args.cmd == "list-tools":
        out = list_tools()
    elif args.cmd == "quickref":
        out = quick_reference()
    elif args.cmd == "search-all":
        out = search_all_sources(args.query, args.limit)
    elif args.cmd == "search-core":
        out = search_dx12_api(args.query, args.limit)
    elif args.cmd == "search-dxr":
        out = search_dxr_api(args.query, args.limit)
    elif args.cmd == "search-hlsl":
        out = search_hlsl_intrinsics(args.query, args.limit)
    elif args.cmd == "search-stage":
        out = search_by_shader_stage(args.stage, args.query, args.limit)
    elif args.cmd == "get":
        out = get_dx12_entity(args.name)
    else:
        parser.print_help()
        return 2

    print(json.dumps(out, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))


from typing import Dict, Any


# Minimal JSON-RPC request/response schema hints for MCP clients.

TOOL_SCHEMAS: Dict[str, Dict[str, Any]] = {
    "search": {
        "arguments": {"query": "str", "limit": "int?"},
        "returns": {"results": "[{id, category, title, slug, snippet}]"},
    },
    "get_doc": {
        "arguments": {"slug": "str"},
        "returns": {"doc": "{...}|null"},
    },
    "list_docs": {
        "arguments": {"category": "str?", "limit": "int?", "offset": "int?"},
        "returns": {"docs": "[{id, category, title, slug}]"},
    },
    "add_doc": {
        "arguments": {"category": "str", "title": "str?", "slug": "str?", "content": "str", "source_url": "str?", "path": "str?"},
        "returns": {"slug": "str", "id": "int"},
    },
    "delete_doc": {
        "arguments": {"slug": "str"},
        "returns": {"ok": "bool"},
    },
    "refresh_index": {
        "arguments": {},
        "returns": {"ingested": "[slug]"},
    },
    "xrefs": {
        "arguments": {"slug": "str", "direction": "out|in?"},
        "returns": {"xrefs": "[{slug, kind}]"},
    },
    "add_xref": {
        "arguments": {"src_slug": "str", "dst_slug": "str", "kind": "str?"},
        "returns": {"ok": "bool"},
    },
    "categories": {
        "arguments": {},
        "returns": {"categories": "[str]"},
    },
    "dx12_agility_info": {
        "arguments": {},
        "returns": {"expected_sdk_dirs": "[str]", "version_mapping": "{...}"},
    },
    "dx12_symbol_lookup": {
        "arguments": {"symbol": "str"},
        "returns": {"matches": "[{slug, title, category}]"},
    },
}


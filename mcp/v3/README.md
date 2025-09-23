### DX12/DXR MCP v3 Server

Run
- Python 3.9+
- Start (stdio): `python server.py --stdio`
 - Test with JSONL: `while read -r l; do echo "$l"; sleep 0.05; done < tests_stdio.jsonl | python server.py --stdio`

Features
- Full-text searchable docs (SQLite FTS5)
- Categories for DX12, DXR, Agility, HLSL, PIX, samples
- Cross-references between docs/symbols
- Easy ingestion: drop files into `categories/<name>/` then call `refresh_index`

Minimal JSON-RPC methods
- `list_tools` → lists tool names
- `call_tool` with name and arguments

Core tools
- `search(query, limit?)`
- `get_doc(slug)`
- `list_docs(category?, limit?, offset?)`
- `add_doc(category, title?, slug?, content, source_url?, path?)`
- `delete_doc(slug)`
- `refresh_index()`
- `xrefs(slug, direction?)`
- `add_xref(src_slug, dst_slug, kind?)`
- `categories()`

DX12 helpers
- `dx12_agility_info()`
- `dx12_symbol_lookup(symbol)`

Ingesting existing docs
- Put `.md`, `.txt`, or `.json` into `categories/<category>/`
- `python server.py --ingest <category>:<path_to_folder>`
- Or call `refresh_index` after copying files

Add new category
- Create folder `categories/<new_name>` and ingest docs

Client configuration
- Configure your MCP client to connect via stdio and send JSON-RPC objects with `method`, `params`, `id`.
 - Claude Desktop: add `mcp/v3/clients/claude_mcp.json` entry.


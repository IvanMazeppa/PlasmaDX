import argparse
import asyncio
import json
import os
import re
import sqlite3
import sys
import time
from dataclasses import dataclass, asdict
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional, Tuple


# Simple STDIO JSON-RPC loop for MCP compatibility without heavy deps.
# Tools are registered in the ToolRegistry and exposed via `call_tool`.


DATA_DIR = Path(__file__).parent / "data"
CATEGORY_DIR = Path(__file__).parent / "categories"
DB_PATH = DATA_DIR / "docs_v3.sqlite3"
SYMBOLS_PATH = DATA_DIR / "dx12_symbols.json"
AGILITY_INFO_PATH = DATA_DIR / "agility_versions.json"


@dataclass
class DocRecord:
    id: Optional[int]
    category: str
    title: str
    slug: str
    path: str
    content: str
    source_url: Optional[str]
    created_ts: float
    updated_ts: float


class Index:
    def __init__(self, db_path: Path) -> None:
        self.db_path = db_path
        self._ensure_schema()

    def _ensure_schema(self) -> None:
        DATA_DIR.mkdir(parents=True, exist_ok=True)
        with sqlite3.connect(self.db_path) as conn:
            conn.execute("PRAGMA journal_mode=WAL;")
            conn.execute(
                """
                CREATE TABLE IF NOT EXISTS docs(
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    category TEXT NOT NULL,
                    title TEXT NOT NULL,
                    slug TEXT NOT NULL UNIQUE,
                    path TEXT NOT NULL,
                    content TEXT NOT NULL,
                    source_url TEXT,
                    created_ts REAL NOT NULL,
                    updated_ts REAL NOT NULL
                );
                """
            )
            # Full-text search virtual table
            conn.execute(
                """
                CREATE VIRTUAL TABLE IF NOT EXISTS docs_fts USING fts5(
                    title, content, category, slug, content="docs", content_rowid="id"
                );
                """
            )
            # Cross-reference edges between slugs (symbol → docs or doc → doc)
            conn.execute(
                """
                CREATE TABLE IF NOT EXISTS xrefs(
                    src_slug TEXT NOT NULL,
                    dst_slug TEXT NOT NULL,
                    kind TEXT NOT NULL,
                    UNIQUE(src_slug, dst_slug, kind)
                );
                """
            )
            conn.commit()

    def upsert_doc(self, record: DocRecord) -> int:
        now = time.time()
        with sqlite3.connect(self.db_path) as conn:
            cur = conn.cursor()
            cur.execute(
                """
                INSERT INTO docs(category, title, slug, path, content, source_url, created_ts, updated_ts)
                VALUES(?,?,?,?,?,?,?,?)
                ON CONFLICT(slug) DO UPDATE SET
                    category=excluded.category,
                    title=excluded.title,
                    path=excluded.path,
                    content=excluded.content,
                    source_url=excluded.source_url,
                    updated_ts=?
                """,
                (
                    record.category,
                    record.title,
                    record.slug,
                    record.path,
                    record.content,
                    record.source_url,
                    record.created_ts or now,
                    record.updated_ts or now,
                    now,
                ),
            )
            if cur.lastrowid:
                rowid = cur.lastrowid
            else:
                # fetch id for existing slug
                cur.execute("SELECT id FROM docs WHERE slug=?", (record.slug,))
                row = cur.fetchone()
                rowid = int(row[0]) if row else 0
            # sync FTS
            cur.execute("INSERT OR REPLACE INTO docs_fts(rowid, title, content, category, slug) SELECT id, title, content, category, slug FROM docs WHERE id=?", (rowid,))
            conn.commit()
            return rowid

    def delete_doc(self, slug: str) -> None:
        with sqlite3.connect(self.db_path) as conn:
            cur = conn.cursor()
            cur.execute("DELETE FROM docs WHERE slug=?", (slug,))
            cur.execute("DELETE FROM docs_fts WHERE rowid NOT IN (SELECT id FROM docs)")
            cur.execute("DELETE FROM xrefs WHERE src_slug=? OR dst_slug=?", (slug, slug))
            conn.commit()

    def search(self, query: str, limit: int = 20) -> List[Dict[str, Any]]:
        with sqlite3.connect(self.db_path) as conn:
            conn.row_factory = sqlite3.Row
            cur = conn.cursor()
            cur.execute(
                """
                SELECT d.id, d.category, d.title, d.slug, snippet(docs_fts, 1, '<b>', '</b>', '…', 8) AS snippet
                FROM docs_fts
                JOIN docs d ON d.id = docs_fts.rowid
                WHERE docs_fts MATCH ?
                ORDER BY rank
                LIMIT ?
                """,
                (query, limit),
            )
            return [dict(r) for r in cur.fetchall()]

    def get_doc(self, slug: str) -> Optional[Dict[str, Any]]:
        with sqlite3.connect(self.db_path) as conn:
            conn.row_factory = sqlite3.Row
            cur = conn.cursor()
            cur.execute("SELECT * FROM docs WHERE slug=?", (slug,))
            row = cur.fetchone()
            return dict(row) if row else None

    def doc_exists(self, slug: str) -> bool:
        with sqlite3.connect(self.db_path) as conn:
            cur = conn.cursor()
            cur.execute("SELECT 1 FROM docs WHERE slug=?", (slug,))
            return cur.fetchone() is not None

    def list_docs(self, category: Optional[str] = None, limit: int = 100, offset: int = 0) -> List[Dict[str, Any]]:
        with sqlite3.connect(self.db_path) as conn:
            conn.row_factory = sqlite3.Row
            cur = conn.cursor()
            if category:
                cur.execute(
                    "SELECT id, category, title, slug FROM docs WHERE category=? ORDER BY title LIMIT ? OFFSET ?",
                    (category, limit, offset),
                )
            else:
                cur.execute(
                    "SELECT id, category, title, slug FROM docs ORDER BY category, title LIMIT ? OFFSET ?",
                    (limit, offset),
                )
            return [dict(r) for r in cur.fetchall()]

    def iter_docs(self) -> Iterable[Dict[str, Any]]:
        with sqlite3.connect(self.db_path) as conn:
            conn.row_factory = sqlite3.Row
            for row in conn.execute("SELECT id, category, title, slug, content FROM docs"):
                yield dict(row)

    def update_slug(self, old_slug: str, new_slug: str) -> None:
        with sqlite3.connect(self.db_path) as conn:
            cur = conn.cursor()
            cur.execute("UPDATE docs SET slug=?, updated_ts=? WHERE slug=?", (new_slug, time.time(), old_slug))
            # FTS sync
            cur.execute("INSERT OR REPLACE INTO docs_fts(rowid, title, content, category, slug) SELECT id, title, content, category, slug FROM docs WHERE slug=?", (new_slug,))
            # Update xrefs
            cur.execute("UPDATE xrefs SET src_slug=? WHERE src_slug=?", (new_slug, old_slug))
            cur.execute("UPDATE xrefs SET dst_slug=? WHERE dst_slug=?", (new_slug, old_slug))
            conn.commit()

    def upsert_xref(self, src_slug: str, dst_slug: str, kind: str) -> None:
        with sqlite3.connect(self.db_path) as conn:
            conn.execute(
                "INSERT OR IGNORE INTO xrefs(src_slug, dst_slug, kind) VALUES(?,?,?)",
                (src_slug, dst_slug, kind),
            )
            conn.commit()

    def get_xrefs(self, slug: str, direction: str = "out") -> List[Dict[str, str]]:
        with sqlite3.connect(self.db_path) as conn:
            conn.row_factory = sqlite3.Row
            cur = conn.cursor()
            if direction == "out":
                cur.execute("SELECT dst_slug AS slug, kind FROM xrefs WHERE src_slug=?", (slug,))
            else:
                cur.execute("SELECT src_slug AS slug, kind FROM xrefs WHERE dst_slug=?", (slug,))
            rows = cur.fetchall()
            return [dict(r) for r in rows]


def normalize_slug(text: str) -> str:
    text = text.strip().lower()
    text = re.sub(r"[^a-z0-9\-\_\./ ]+", "", text)
    text = text.replace("/", "-")
    text = re.sub(r"\s+", "-", text)
    return text[:160]


class ToolRegistry:
    def __init__(self, index: Index) -> None:
        self.index = index
        self.tools: Dict[str, Any] = {}
        self._register_core_tools()
        self._register_dx12_tools()
        self._symbols_cache: Dict[str, str] = self._load_symbols_map()
        self._agility_info: Dict[str, Any] = self._load_agility_info()

    def register(self, name: str, fn) -> None:
        self.tools[name] = fn

    def _register_core_tools(self) -> None:
        self.register("search", self.tool_search)
        self.register("get_doc", self.tool_get_doc)
        self.register("list_docs", self.tool_list_docs)
        self.register("add_doc", self.tool_add_doc)
        self.register("delete_doc", self.tool_delete_doc)
        self.register("refresh_index", self.tool_refresh_index)
        self.register("xrefs", self.tool_xrefs)
        self.register("add_xref", self.tool_add_xref)
        self.register("categories", self.tool_categories)

    def _register_dx12_tools(self) -> None:
        # Version/introspection helpers for 3D programmers working with Agility SDK / DXR
        self.register("dx12_agility_info", self.tool_dx12_agility_info)
        self.register("dx12_symbol_lookup", self.tool_dx12_symbol_lookup)

    def _load_symbols_map(self) -> Dict[str, str]:
        try:
            if SYMBOLS_PATH.exists():
                return json.loads(SYMBOLS_PATH.read_text(encoding="utf-8"))
        except Exception:
            pass
        # Default seed mapping
        return {
            "ID3D12Device": "dx12-id3d12device",
            "ID3D12Device5": "dx12-id3d12device5",
            "D3D12_RAYTRACING_PIPELINE_STATE_DESC": "dxr-d3d12_raytracing_pipeline_state_desc",
            "DispatchRays": "dxr-dispatchrays",
            "D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE": "dxr-state-object-types",
            "ID3D12GraphicsCommandList4": "dx12-id3d12graphicscommandlist4",
            "BuildRaytracingAccelerationStructure": "dxr-build-accel-structure",
        }

    def _load_agility_info(self) -> Dict[str, Any]:
        try:
            if AGILITY_INFO_PATH.exists():
                return json.loads(AGILITY_INFO_PATH.read_text(encoding="utf-8"))
        except Exception:
            pass
        return {
            "expected_sdk_dirs": [
                "./bin/D3D12/", "./D3D12/", "C:/Windows/System32/d3d12core.dll"
            ],
            "version_mapping": {
                "Agility_1.713.3": {"d3d12core": "10.0.22621.3620", "notes": "DXR 1.1+"},
                "Agility_1.711.3": {"d3d12core": "10.0.22621.3500"}
            },
        }

    # ---- Core tools ----
    def tool_search(self, params: Dict[str, Any]) -> Dict[str, Any]:
        q = params.get("query") or ""
        limit = int(params.get("limit") or 20)
        return {"results": self.index.search(q, limit=limit)}

    def tool_get_doc(self, params: Dict[str, Any]) -> Dict[str, Any]:
        slug = params.get("slug")
        rec = self.index.get_doc(slug)
        return {"doc": rec}

    def tool_list_docs(self, params: Dict[str, Any]) -> Dict[str, Any]:
        category = params.get("category")
        limit = int(params.get("limit") or 100)
        offset = int(params.get("offset") or 0)
        return {"docs": self.index.list_docs(category, limit=limit, offset=offset)}

    def tool_add_doc(self, params: Dict[str, Any]) -> Dict[str, Any]:
        category = (params.get("category") or "misc").strip()
        title = (params.get("title") or "").strip()
        slug = normalize_slug(params.get("slug") or title)
        content = params.get("content") or ""
        source_url = params.get("source_url")
        path = params.get("path") or f"inline:{slug}"
        ts = time.time()
        rec = DocRecord(
            id=None,
            category=category,
            title=title or slug,
            slug=slug,
            path=path,
            content=content,
            source_url=source_url,
            created_ts=ts,
            updated_ts=ts,
        )
        rowid = self.index.upsert_doc(rec)
        # auto-link [[slug]] mentions
        self._auto_link_mentions(slug, content)
        return {"slug": slug, "id": rowid}

    def tool_delete_doc(self, params: Dict[str, Any]) -> Dict[str, Any]:
        slug = params.get("slug")
        self.index.delete_doc(slug)
        return {"ok": True}

    def tool_refresh_index(self, params: Dict[str, Any]) -> Dict[str, Any]:
        # Ingest any files dropped into categories/<category>/ as JSON or MD
        ingested: List[str] = []
        for cat_dir in CATEGORY_DIR.iterdir() if CATEGORY_DIR.exists() else []:
            if not cat_dir.is_dir():
                continue
            category = cat_dir.name
            for p in cat_dir.rglob("*"):
                if not p.is_file():
                    continue
                if p.suffix.lower() in {".json"}:
                    data = json.loads(p.read_text(encoding="utf-8", errors="ignore"))
                    title = data.get("title") or p.stem
                    content = data.get("content") or json.dumps(data, ensure_ascii=False, indent=2)
                    slug = normalize_slug(data.get("slug") or f"{category}-{p.stem}")
                    source_url = data.get("source_url")
                elif p.suffix.lower() in {".md", ".txt"}:
                    title = p.stem
                    content = p.read_text(encoding="utf-8", errors="ignore")
                    slug = normalize_slug(f"{category}-{p.stem}")
                    source_url = None
                else:
                    continue
                rec = DocRecord(
                    id=None,
                    category=category,
                    title=title,
                    slug=slug,
                    path=str(p),
                    content=content,
                    source_url=source_url,
                    created_ts=time.time(),
                    updated_ts=time.time(),
                )
                self.index.upsert_doc(rec)
                ingested.append(slug)
                # auto-link [[slug]] mentions
                self._auto_link_mentions(slug, content)
        return {"ingested": ingested}

    def tool_xrefs(self, params: Dict[str, Any]) -> Dict[str, Any]:
        slug = params.get("slug")
        direction = params.get("direction") or "out"
        return {"xrefs": self.index.get_xrefs(slug, direction=direction)}

    def tool_add_xref(self, params: Dict[str, Any]) -> Dict[str, Any]:
        src = params.get("src_slug")
        dst = params.get("dst_slug")
        kind = params.get("kind") or "ref"
        self.index.upsert_xref(src, dst, kind)
        return {"ok": True}

    def tool_categories(self, params: Dict[str, Any]) -> Dict[str, Any]:
        cats = []
        if CATEGORY_DIR.exists():
            for c in CATEGORY_DIR.iterdir():
                if c.is_dir():
                    cats.append(c.name)
        return {"categories": sorted(cats)}

    # ---- DX12 helpers ----
    def tool_dx12_agility_info(self, params: Dict[str, Any]) -> Dict[str, Any]:
        # Loadable via data/agility_versions.json with defaults
        return self._agility_info

    def tool_dx12_symbol_lookup(self, params: Dict[str, Any]) -> Dict[str, Any]:
        symbol = (params.get("symbol") or "").strip()
        # Heuristic mapping, can be extended by adding docs with slugs matching symbols
        slug = normalize_slug(symbol)
        # try exact doc
        doc = self.index.get_doc(slug)
        if doc:
            return {"matches": [
                {"slug": doc["slug"], "title": doc["title"], "category": doc["category"]}
            ]}
        # try category-prefixed variants
        candidates = [
            f"dx12-{slug}", f"dxr-{slug}", f"agility-{slug}", f"hlsl-{slug}"
        ]
        matches: List[Dict[str, str]] = []
        for c in candidates:
            d = self.index.get_doc(c)
            if d:
                matches.append({"slug": d["slug"], "title": d["title"], "category": d["category"]})
        # try static symbol map
        mapped = self._symbols_cache.get(symbol)
        if mapped:
            d = self.index.get_doc(mapped)
            if d:
                matches.append({"slug": d["slug"], "title": d["title"], "category": d["category"]})
        return {"matches": matches}

    # ---- Helpers ----
    def _auto_link_mentions(self, src_slug: str, content: str) -> None:
        # Detect [[slug]] mentions and add xrefs if target exists
        for m in re.findall(r"\[\[([a-z0-9\-_\.]{2,})\]\]", content.lower()):
            target = m.strip()
            if self.index.doc_exists(target):
                self.index.upsert_xref(src_slug, target, kind="mentions")


class JsonRpcStdio:
    def __init__(self, registry: ToolRegistry) -> None:
        self.registry = registry

    def _read(self) -> Optional[Dict[str, Any]]:
        line = sys.stdin.readline()
        if not line:
            return None
        try:
            return json.loads(line)
        except Exception:
            return None

    def _write(self, payload: Dict[str, Any]) -> None:
        sys.stdout.write(json.dumps(payload) + "\n")
        sys.stdout.flush()

    def serve(self) -> None:
        while True:
            req = self._read()
            if req is None:
                break
            if not isinstance(req, dict):
                continue
            method = req.get("method")
            params = req.get("params") or {}
            req_id = req.get("id")
            if method == "list_tools":
                self._write({
                    "id": req_id,
                    "result": {"tools": sorted(list(self.registry.tools.keys()))},
                })
                continue
            if method == "call_tool":
                name = params.get("name")
                args = params.get("arguments") or {}
                fn = self.registry.tools.get(name)
                if not fn:
                    self._write({"id": req_id, "error": {"code": -32601, "message": f"Unknown tool: {name}"}})
                    continue
                try:
                    result = fn(args)
                    self._write({"id": req_id, "result": result})
                except Exception as e:
                    self._write({"id": req_id, "error": {"code": -32000, "message": str(e)}})
                continue
            # Fallback
            self._write({"id": req_id, "error": {"code": -32601, "message": "Unknown method"}})


def ensure_default_categories() -> None:
    CATEGORY_DIR.mkdir(parents=True, exist_ok=True)
    for name in ["dx12", "dxr", "agility", "hlsl", "pix", "samples", "utils"]:
        (CATEGORY_DIR / name).mkdir(parents=True, exist_ok=True)


def main() -> None:
    parser = argparse.ArgumentParser(description="DX12/DXR MCP v3 server")
    parser.add_argument("--stdio", action="store_true", help="Serve over stdio JSON-RPC")
    parser.add_argument("--ingest", type=str, default=None, help="Ingest a folder of docs into a category: category:path")
    args = parser.parse_args()

    ensure_default_categories()
    index = Index(DB_PATH)
    registry = ToolRegistry(index)

    if args.ingest:
        # Quick one-shot ingestion
        try:
            cat, folder = args.ingest.split(":", 1)
        except ValueError:
            print("--ingest expects category:path", file=sys.stderr)
            sys.exit(2)
        folder_path = Path(folder)
        if not folder_path.exists():
            print(f"Path not found: {folder}", file=sys.stderr)
            sys.exit(2)
        # Mirror into categories/cat then refresh
        target = CATEGORY_DIR / cat
        target.mkdir(parents=True, exist_ok=True)
        for p in folder_path.rglob("*"):
            if p.is_file():
                rel = p.relative_to(folder_path)
                dest = target / rel
                dest.parent.mkdir(parents=True, exist_ok=True)
                try:
                    dest.write_text(p.read_text(encoding="utf-8", errors="ignore"), encoding="utf-8")
                except Exception:
                    continue
        _ = registry.tool_refresh_index({})
        print(json.dumps({"ingested_category": cat}))
        return

    if args.stdio:
        JsonRpcStdio(registry).serve()
        return

    # Default behavior: print instructions
    print("DX12/DXR MCP v3 server ready. Use --stdio to serve JSON-RPC over stdio.")
    print("Tools available:", ", ".join(sorted(registry.tools.keys())))


if __name__ == "__main__":
    main()


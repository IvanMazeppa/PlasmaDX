#!/usr/bin/env python3
import sys
import os
from pathlib import Path


def list_logs(log_dir: Path):
    if not log_dir.exists():
        print(f"logs directory not found: {log_dir}")
        return []
    return sorted(
        [p for p in log_dir.glob("plasmadx_*.log") if p.is_file()],
        key=lambda p: p.stat().st_mtime,
        reverse=True,
    )


def head_lines(path: Path, max_lines: int = 300):
    lines = []
    try:
        with path.open("r", encoding="utf-8", errors="replace") as f:
            for idx, line in enumerate(f):
                if idx >= max_lines:
                    break
                lines.append(line.rstrip("\n"))
    except Exception as e:
        lines.append(f"[ERROR] Failed to read {path}: {e}")
    return lines


def summarize_block(lines):
    keys = {
        "device": None,
        "dxr_tier": None,
        "agility": None,
        "features": [],
        "pso_payload": None,
        "recursion": None,
        "dxil_bytes": None,
        "mode": None,
        "hdr_desc": None,
        "particles": None,
        "density_pipelines": [],
        "sbt_status": None,
    }

    for ln in lines:
        if "Agility exports:" in ln:
            keys["agility"] = ln.strip()
        if "DXR Tier:" in ln:
            keys["dxr_tier"] = ln.strip()
        if "Found adapter:" in ln:
            keys["device"] = ln.strip()
        if "DXR 1.2 Features:" in ln or "DXR 1.1 Features:" in ln:
            keys["features"].append(ln.strip())
        if "Loaded DXIL shader:" in ln and "raytracing_lib.dxil" in ln:
            keys["dxil_bytes"] = ln.strip()
        if "Pipeline::SetShaderConfig" in ln:
            keys["pso_payload"] = ln.strip()
        if "Pipeline::SetPipelineConfig" in ln:
            keys["recursion"] = ln.strip()
        if "HDR texture descriptors allocated" in ln:
            keys["hdr_desc"] = ln.strip()
        if "Particles: Initializing with" in ln:
            keys["particles"] = ln.strip()
        if "Successfully created" in ln and ("density" in ln or "curl-advect" in ln):
            keys["density_pipelines"].append(ln.strip())
        if "SBT: Build complete" in ln or "SBT: Returning valid GPU addresses" in ln:
            keys["sbt_status"] = ln.strip()
        if "Demo Mode:" in ln or "DEBUG: Read PLASMADX_DEBUG_MODE" in ln or "DEBUG: Parsed PLASMADX_DEBUG_MODE" in ln:
            keys["mode"] = ln.strip()

    return keys


def main():
    root = Path(__file__).resolve().parents[1]
    log_dir = root / "logs"

    logs = list_logs(log_dir)
    if not logs:
        print("No logs found.")
        return 0

    try:
        n = int(os.environ.get("PLASMADX_SUMMARY_N", "4"))
    except ValueError:
        n = 4
    n = max(1, min(n, 20))

    target_logs = logs[:n]
    print(f"Summarizing first 300 lines of {len(target_logs)} newest logs in {log_dir}...\n")

    for i, log_path in enumerate(target_logs, 1):
        lines = head_lines(log_path, 300)
        keys = summarize_block(lines)
        print(f"=== [{i}/{len(target_logs)}] {log_path.name} ===")
        for k, v in keys.items():
            if isinstance(v, list):
                if v:
                    print(f"- {k}:")
                    for item in v:
                        print(f"  * {item}")
            else:
                if v:
                    print(f"- {k}: {v}")
        print()

    print("Tip: Set PLASMADX_SUMMARY_N to change how many logs to scan.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

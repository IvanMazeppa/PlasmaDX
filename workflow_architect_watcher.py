#!/usr/bin/env python3
import os
import time
import shutil
import threading
import json
from typing import Optional

try:
    import requests  # optional if using HTTP endpoint
except Exception:
    requests = None

try:
    import openai  # optional
except Exception:
    openai = None

try:
    import anthropic  # optional
except Exception:
    anthropic = None


WORKDIR = "/workspace"
INBOX_DIR = os.path.join(WORKDIR, "workflow_claude")
PROCESSED_DIR = os.path.join(WORKDIR, "workflow_claude_processed")
ANSWERS_DIR = os.path.join(WORKDIR, "answers")

# Config via env:
# SOFTWARE_ARCHITECT_ENDPOINT: if set, POST {prompt: <text>} and expect {answer: <text>}
# OPENAI_API_KEY + OPENAI_MODEL: if set, call OpenAI Chat Completions
# ANTHROPIC_API_KEY + ANTHROPIC_MODEL: if set, call Anthropic Messages


def ensure_dirs() -> None:
    os.makedirs(INBOX_DIR, exist_ok=True)
    os.makedirs(PROCESSED_DIR, exist_ok=True)
    os.makedirs(ANSWERS_DIR, exist_ok=True)


def read_text_file(path: str) -> str:
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        return f.read()


def write_text_file(path: str, content: str) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(content)


def call_architect_agent(prompt: str) -> str:
    endpoint = os.environ.get("SOFTWARE_ARCHITECT_ENDPOINT")
    if endpoint and requests is not None:
        try:
            resp = requests.post(endpoint, json={"prompt": prompt}, timeout=120)
            resp.raise_for_status()
            data = resp.json()
            if isinstance(data, dict) and "answer" in data:
                return str(data["answer"])  # type: ignore
            return json.dumps(data)
        except Exception as e:
            return f"[Architect endpoint error] {e}"

    # OpenAI fallback
    openai_key = os.environ.get("OPENAI_API_KEY")
    openai_model = os.environ.get("OPENAI_MODEL", "gpt-4o-mini")
    if openai_key and openai is not None:
        try:
            openai.api_key = openai_key
            # Using Chat Completions API
            completion = openai.chat.completions.create(
                model=openai_model,
                messages=[
                    {"role": "system", "content": "You are a Software Architect. Provide precise, structured answers."},
                    {"role": "user", "content": prompt},
                ],
                temperature=0.2,
            )
            return completion.choices[0].message.content or ""
        except Exception as e:
            return f"[OpenAI error] {e}"

    # Anthropic fallback
    anthropic_key = os.environ.get("ANTHROPIC_API_KEY")
    anthropic_model = os.environ.get("ANTHROPIC_MODEL", "claude-3-5-sonnet-latest")
    if anthropic_key and anthropic is not None:
        try:
            client = anthropic.Anthropic(api_key=anthropic_key)
            msg = client.messages.create(
                model=anthropic_model,
                max_tokens=4096,
                temperature=0.2,
                system="You are a Software Architect. Provide precise, structured answers.",
                messages=[{"role": "user", "content": prompt}],
            )
            return "".join(part.text for part in msg.content)  # type: ignore
        except Exception as e:
            return f"[Anthropic error] {e}"

    # If no backends configured
    return "[No architect backend configured. Set SOFTWARE_ARCHITECT_ENDPOINT or OPENAI_API_KEY or ANTHROPIC_API_KEY]"


def process_file(file_path: str) -> None:
    base = os.path.basename(file_path)
    name, _ext = os.path.splitext(base)

    try:
        prompt = read_text_file(file_path)
    except Exception as e:
        print(f"Failed reading {file_path}: {e}")
        return

    # Move first (cut) to processed to avoid re-processing
    dest = os.path.join(PROCESSED_DIR, base)
    try:
        shutil.move(file_path, dest)
    except Exception as e:
        print(f"Failed moving {file_path} -> {dest}: {e}")
        return

    # Call architect
    print(f"Dispatching to Software Architect: {base}")
    answer = call_architect_agent(prompt)

    # Save answer
    ts = int(time.time())
    answer_path = os.path.join(ANSWERS_DIR, f"{name}__answer__{ts}.txt")
    try:
        write_text_file(answer_path, answer)
        print(f"Saved answer -> {answer_path}")
    except Exception as e:
        print(f"Failed writing answer for {base}: {e}")


def list_unprocessed_files() -> list[str]:
    try:
        entries = [
            os.path.join(INBOX_DIR, f)
            for f in os.listdir(INBOX_DIR)
            if os.path.isfile(os.path.join(INBOX_DIR, f))
        ]
        # Sort by mtime so newest last (process oldest first)
        entries.sort(key=lambda p: os.path.getmtime(p))
        return entries
    except FileNotFoundError:
        return []


def watch_loop(poll_interval: float = 1.0) -> None:
    ensure_dirs()
    seen: set[str] = set()
    while True:
        try:
            files = list_unprocessed_files()
            for path in files:
                if path in seen:
                    continue
                seen.add(path)
                # Defer processing slightly to allow complete writes
                def _proc(p=path):
                    # Wait briefly to ensure the file writer is done
                    time.sleep(0.2)
                    if os.path.exists(p):
                        process_file(p)
                threading.Thread(target=_proc, daemon=True).start()
        except Exception as e:
            print(f"Watcher error: {e}")
        time.sleep(poll_interval)


if __name__ == "__main__":
    print("Starting Software Architect workflow watcher...")
    print(f"Watching: {INBOX_DIR}")
    watch_loop()


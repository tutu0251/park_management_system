#!/usr/bin/env python3
"""Fast local server: edit platformio.ini + background builds."""
from __future__ import annotations

import json
import os
import subprocess
import sys
import threading
import time
import uuid
from http.server import HTTPServer, SimpleHTTPRequestHandler
from pathlib import Path
from socketserver import ThreadingMixIn
from urllib.parse import parse_qs, urlparse

FIRMWARE_DIR = Path(__file__).resolve().parent.parent
EDITOR_DIR = Path(__file__).resolve().parent
INI_PATH = FIRMWARE_DIR / "platformio.ini"

_build_lock = threading.Lock()
_pio_build_mutex = threading.Lock()
_build_jobs: dict[str, dict] = {}
_pio_version: str | None = None
_pio_checked = False


def _pio_version_once() -> str:
    global _pio_version, _pio_checked
    if _pio_checked:
        return _pio_version or ""
    _pio_checked = True
    try:
        r = subprocess.run(
            ["pio", "--version"],
            cwd=FIRMWARE_DIR,
            capture_output=True,
            text=True,
            timeout=8,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
        _pio_version = (r.stdout or r.stderr or "").strip() if r.returncode == 0 else ""
    except (FileNotFoundError, subprocess.TimeoutExpired):
        _pio_version = ""
    return _pio_version or ""


def _run_build(job_id: str, envs: list[str]) -> None:
    """Build each environment sequentially (-j 1) to avoid Windows ar.exe lock errors."""
    log_lines: list[str] = []

    def append(line: str) -> None:
        log_lines.append(line)
        if len(log_lines) > 800:
            del log_lines[: len(log_lines) - 800]
        with _build_lock:
            _build_jobs[job_id]["log"] = "\n".join(log_lines)

    commands = [f"pio run -e {env} -j 1" for env in envs]
    with _build_lock:
        _build_jobs[job_id]["status"] = "running"
        _build_jobs[job_id]["command"] = " ; ".join(commands)

    ok = True
    exit_code = 0

    with _pio_build_mutex:
        try:
            for env in envs:
                cmd = ["pio", "run", "-e", env, "-j", "1"]
                append(f"\n=== Building {env} ===\n")
                proc = subprocess.Popen(
                    cmd,
                    cwd=FIRMWARE_DIR,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    bufsize=1,
                    creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
                )
                assert proc.stdout is not None
                for line in proc.stdout:
                    append(line.rstrip("\n"))
                code = proc.wait(timeout=600)
                if code != 0:
                    ok = False
                    exit_code = code
                    append(f"\n*** {env} failed (exit {code}) ***\n")
        except FileNotFoundError:
            append("ERROR: pio not found on PATH")
            ok, exit_code = False, 127
        except subprocess.TimeoutExpired:
            append("ERROR: build timed out (10 min)")
            ok, exit_code = False, 124
        except Exception as exc:  # noqa: BLE001
            append(f"ERROR: {exc}")
            ok, exit_code = False, 1

    with _build_lock:
        _build_jobs[job_id]["status"] = "ok" if ok else "failed"
        _build_jobs[job_id]["exitCode"] = exit_code
        _build_jobs[job_id]["done"] = True


class ThreadingHTTPServer(ThreadingMixIn, HTTPServer):
    daemon_threads = True


class EditorHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(EDITOR_DIR), **kwargs)

    def log_message(self, fmt, *args):
        sys.stderr.write("%s - %s\n" % (self.log_date_time_string(), fmt % args))

    def _json(self, status: int, payload: dict) -> None:
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _read_json(self) -> dict:
        length = int(self.headers.get("Content-Length", 0))
        raw = self.rfile.read(length) if length else b"{}"
        return json.loads(raw.decode("utf-8"))

    def do_GET(self):
        path = urlparse(self.path).path
        if path in ("/", "/index.html"):
            self.path = "/index.html"
            return super().do_GET()
        if path == "/api/health":
            return self._json(200, {"ok": True})
        if path == "/api/ini":
            if not INI_PATH.is_file():
                return self._json(404, {"ok": False, "error": "platformio.ini not found"})
            return self._json(
                200,
                {"ok": True, "content": INI_PATH.read_text(encoding="utf-8"), "path": str(INI_PATH)},
            )
        if path == "/api/pio-check":
            ver = _pio_version_once()
            return self._json(200, {"ok": bool(ver), "version": ver or "pio not found"})
        if path == "/api/build":
            qs = parse_qs(urlparse(self.path).query)
            job_id = (qs.get("id") or [""])[0]
            with _build_lock:
                job = _build_jobs.get(job_id)
            if not job:
                return self._json(404, {"ok": False, "error": "Unknown build job"})
            return self._json(200, {"ok": True, **job})
        return super().do_GET()

    def do_POST(self):
        path = urlparse(self.path).path
        if path == "/api/ini":
            data = self._read_json()
            content = data.get("content", "")
            INI_PATH.write_text(content, encoding="utf-8", newline="\n")
            return self._json(200, {"ok": True, "path": str(INI_PATH)})
        if path == "/api/build":
            data = self._read_json()
            envs = [str(e) for e in (data.get("envs") or []) if e]
            if not envs:
                return self._json(400, {"ok": False, "error": "No environments selected"})
            if not _pio_version_once():
                return self._json(500, {"ok": False, "error": "pio not on PATH — install PlatformIO first"})
            job_id = str(uuid.uuid4())[:8]
            with _build_lock:
                _build_jobs[job_id] = {
                    "status": "queued",
                    "done": False,
                    "log": "Starting build…\n",
                    "exitCode": None,
                    "envs": envs,
                }
            threading.Thread(target=_run_build, args=(job_id, envs), daemon=True).start()
            return self._json(200, {"ok": True, "jobId": job_id, "envs": envs})
        return self._json(404, {"ok": False, "error": "Not found"})


def main() -> None:
    port = int(os.environ.get("PIO_EDITOR_PORT", "8765"))
    # Warm PlatformIO once in background so first build feels faster.
    threading.Thread(target=_pio_version_once, daemon=True).start()

    try:
        httpd = ThreadingHTTPServer(("127.0.0.1", port), EditorHandler)
    except OSError as exc:
        print(f"Cannot bind port {port}: {exc}", file=sys.stderr)
        print("Close the other editor window or set PIO_EDITOR_PORT.", file=sys.stderr)
        sys.exit(1)

    url = f"http://127.0.0.1:{port}/"
    print(f"Editor ready: {url}")
    print(f"INI: {INI_PATH}")
    print("Ctrl+C to stop.")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nStopped.")


if __name__ == "__main__":
    main()

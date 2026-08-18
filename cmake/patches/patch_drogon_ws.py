#!/usr/bin/env python3
"""Patch drogon v1.9.12: accept websocket upgrades on reused connections.

Replaces the firstReq() gate in HttpServer::onRequests so upgrades
are recognized on any single-request batch, not just the first request
on a TCP connection. Idempotent — exits 0 if already patched.
"""
import sys
from pathlib import Path

OLD = """    // will only be checked for the first request
    if (requestParser->firstReq() && requests.size() == 1 &&
        isWebSocket(requests[0]))"""

NEW = """    // Websocket upgrades are valid on any request of a persistent
    // connection, not just the first: proxies with upstream keepalive pools
    // (nginx `keepalive`) legitimately send upgrades on recycled
    // connections. Pipelined batches remain excluded (size == 1 guard).
    if (requests.size() == 1 && isWebSocket(requests[0]))"""

filepath = Path(sys.argv[1]) / "lib" / "src" / "HttpServer.cc"
src = filepath.read_text()

if "Websocket upgrades are valid" in src:
    print(f"[drogon-patch] Already patched, skipping")
    sys.exit(0)

if OLD not in src:
    print(f"[drogon-patch] ERROR: original text not found in {filepath}", file=sys.stderr)
    sys.exit(1)

filepath.write_text(src.replace(OLD, NEW, 1))
print(f"[drogon-patch] Applied successfully")
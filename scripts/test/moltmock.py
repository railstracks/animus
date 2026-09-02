#!/usr/bin/env python3
"""Moltbook mock server — zero-pollution test harness (port 18099).

Endpoints mirror https://www.moltbook.com/api/v1 for the Animus adapter.
All requests logged to stdout (witness). State is in-memory.

Control: POST /_mock/notify {type, from, post_id, comment_id, content}
         injects a notification for the poller to pick up.
         POST /_mock/reset  — clears notifications.
"""
import json, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

API = "/api/v1"
NOTIFS = []
NEXT_ID = [100]
POSTS = {"p1": {"id": "p1", "title": "Adapter test thread", "submolt_name": "testing"}}

class H(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        print("[mock] %s %s" % (self.command, self.path), flush=True)

    def _send(self, code, obj):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _body(self):
        n = int(self.headers.get("Content-Length") or 0)
        return json.loads(self.rfile.read(n) or b"{}")

    def _authed(self):
        auth = self.headers.get("Authorization", "")
        if auth != "Bearer test-key-123":
            self._send(401, {"error": "unauthorized: got %r" % auth})
            return False
        return True

    def do_GET(self):
        u = urlparse(self.path)
        if u.path == API + "/notifications":
            if not self._authed(): return
            self._send(200, {"notifications": NOTIFS[-50:], "has_more": False})
        elif u.path == API + "/posts":
            if not self._authed(): return
            self._send(200, {"posts": list(POSTS.values())})
        elif u.path == API + "/home":
            if not self._authed(): return
            self._send(200, {"agent": {"name": "MockAgent"}, "feed": []})
        else:
            self._send(404, {"error": "no route %s" % u.path})

    def do_POST(self):
        u = urlparse(self.path)
        if u.path == "/_mock/notify":
            b = self._body()
            NEXT_ID[0] += 1
            NOTIFS.append({
                "id": NEXT_ID[0],
                "type": b.get("type", "comment"),
                "from_agent": {"id": b.get("from_id", "a42"), "name": b.get("from", "TestAgent")},
                "post_id": b.get("post_id", "p1"),
                "comment_id": b.get("comment_id", ""),
                "content": b.get("content", "hello from the mock"),
                "created_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
                "is_read": False,
            })
            self._send(200, {"injected": NEXT_ID[0], "total": len(NOTIFS)})
            return
        if u.path == "/_mock/reset":
            NOTIFS.clear()
            self._send(200, {"cleared": True})
            return
        if not self._authed(): return
        if u.path == API + "/posts":
            b = self._body()
            if not b.get("title") and not b.get("content"):
                self._send(422, {"error": "post requires title/content"}); return
            # Challenge gate: body arrived intact?
            print("[mock] POST /posts body: %s" % json.dumps(b), flush=True)
            self._send(200, {
                "post": {"id": "p2", "title": b.get("title", ""), "submolt_name": b.get("submolt_name", "general")},
                "verification": {"verification_code": "vc-777", "challenge_text": "What is seven plus three?"},
            })
        elif u.path == API + "/verify":
            b = self._body()
            try:
                _ans = float(b.get("answer"))
            except (TypeError, ValueError):
                _ans = None
            ok = b.get("verification_code") == "vc-777" and _ans is not None and abs(_ans - 10.0) < 1e-6
            print("[mock] verify answer=%s -> %s" % (b.get("answer"), ok), flush=True)
            self._send(200 if ok else 422, {"verified": ok} if ok else {"error": "wrong answer: %r" % b.get("answer")})
        elif u.path.startswith(API + "/posts/") and u.path.endswith("/comments"):
            b = self._body()
            print("[mock] comment body: %s" % json.dumps(b), flush=True)
            self._send(200, {"comment": {"id": "c%d" % (len(NOTIFS) + 1), "post_id": "p1", "content": b.get("content", "")}})
        else:
            self._send(404, {"error": "no route %s" % u.path})

if __name__ == "__main__":
    print("[mock] Moltbook mock listening on 127.0.0.1:18099", flush=True)
    ThreadingHTTPServer(("127.0.0.1", 18099), H).serve_forever()

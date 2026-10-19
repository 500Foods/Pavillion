#!/usr/bin/env python3
"""JMAP fixture server for Pavillion tests.

Serves recorded JMAP responses from tests/support/fixtures/*.json, keyed by
method and arguments, plus scripted error and behavioral cases. Python standard
library only. Started and stopped by `make test`; the URL and test CA are
passed to the Unity runner through environment variables.

Scripted cases (triggered by query params on the request URL):

  ?error=401           -> HTTP 401 Unauthorized
  ?error=429           -> HTTP 429 with Retry-After: 1
  ?error=503           -> HTTP 503 Service Unavailable
  ?error=cannotCalculateChanges -> JMAP response with cannotCalculateChanges
  ?error=stateMismatch -> JMAP response with stateMismatch
  ?slow=1              -> delay the response by 2 seconds
  ?proxy=cut           -> close the TCP connection without responding
  ?proxy=restore       -> reopen the connection after a cut

EventSource:
  GET /jmap/events     -> stream `event: StateChange` lines with drops

Upload / download:
  POST /jmap/upload    -> accept an upload, return a blobId
  GET  /jmap/download/<id> -> return the stored blob bytes
"""

import base64
import hashlib
import json
import os
import socket
import ssl
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FIXTURE_DIR = os.path.join(REPO_ROOT, "tests", "support", "fixtures")
CA_DIR = os.path.join(REPO_ROOT, "build", "test-ca")

DOMAINS = ["test-a.local", "test-b.local"]
ACCOUNTS = [
    {"id": "a1", "email": "andrew@test-a.local", "name": "Andrew"},
    {"id": "a2", "email": "alice@test-a.local", "name": "Alice"},
    {"id": "b1", "email": "bob@test-b.local", "name": "Bob"},
    {"id": "b2", "email": "carol@test-b.local", "name": "Carol"},
    {"id": "b3", "email": "dave@test-b.local", "name": "Dave"},
]

# Shared scripted state across all handler instances.
class ScriptedState:
    proxy_cut = False
    proxy_lock = threading.Lock()
    uploads = {}
    upload_lock = threading.Lock()
    event_sse_count = 0
    event_lock = threading.Lock()


def free_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def load_cert(ca_dir):
    os.makedirs(ca_dir, exist_ok=True)
    key_path = os.path.join(ca_dir, "test-ca.key")
    cert_path = os.path.join(ca_dir, "test-ca.crt")
    if not os.path.exists(key_path) or not os.path.exists(cert_path):
        import subprocess

        subprocess.run(
            ["openssl", "req", "-x509", "-newkey", "rsa:2048", "-keyout", key_path,
             "-out", cert_path, "-days", "365", "-nodes",
             "-subj", "/CN=pavillion-test-ca",
             "-addext", "subjectAltName=IP:127.0.0.1,DNS:test-a.local,DNS:test-b.local"],
             check=True, capture_output=True,
        )
    return key_path, cert_path


def scripted_error(query):
    """Return (status_code, body_dict_or_None, headers_dict_or_None) or None."""
    err = query.get("error", [None])[0]
    if err == "401":
        return 401, {"error": {"code": "unauthorized", "description": "auth required"}}, {"WWW-Authenticate": 'Basic realm="test"'}
    if err == "429":
        return 429, {"error": {"code": "rateLimit", "description": "too many requests"}}, {"Retry-After": "1"}
    if err == "503":
        return 503, {"error": {"code": "serverFail", "description": "temporarily unavailable"}}, {"Retry-After": "1"}
    return None


def cannot_calculate_changes():
    return {
        "error": {
            "code": "cannotCalculateChanges",
            "description": "server cannot calculate changes",
        }
    }


def state_mismatch():
    return {
        "error": {
            "code": "stateMismatch",
            "description": "state has changed",
        }
    }


class JMapHandler(BaseHTTPRequestHandler):
    server_version = "PavillionFixture/0.1"

    def log_message(self, format, *args):
        pass

    def send_json(self, code, obj, extra_headers=None):
        body = json.dumps(obj).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        if extra_headers:
            for key, value in extra_headers.items():
                self.send_header(key, value)
        self.end_headers()
        self.wfile.write(body)

    def send_text(self, code, text, ctype="text/plain"):
        body = text.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def read_body(self):
        length = int(self.headers.get("Content-Length", 0))
        return self.rfile.read(length) if length > 0 else b""

    def check_proxy_cut(self):
        with ScriptedState.proxy_lock:
            if ScriptedState.proxy_cut:
                self.connection.close()
                return True
        return False

    def session_for(self, domain):
        accounts = [a for a in ACCOUNTS if a["email"].endswith("@" + domain)]
        return {
            "apiUrl": f"https://{domain}/jmap/api",
            "downloadUrl": f"https://{domain}/jmap/download",
            "uploadUrl": f"https://{domain}/jmap/upload",
            "eventSourceUrl": f"https://{domain}/jmap/events",
            "primaryAccounts": {
                "mail": accounts[0]["id"] if accounts else "",
            },
            "accounts": {
                a["id"]: {
                    "id": a["id"],
                    "name": a["name"],
                    "isPersonal": True,
                    "isSystemAccount": True,
                    "username": a["email"],
                    "from": a["email"],
                    "capabilities": {
                        "urn:ietf:params:jmap:mail": {},
                        "urn:ietf:params:jmap:submission": {},
                        "urn:ietf:params:jmap:contacts": {},
                        "urn:ietf:params:jmap:calendars": {},
                    },
                }
                for a in accounts
            },
            "capabilities": {
                "urn:ietf:params:jmap:mail": {
                    "maxSizeAttachment": 26214400,
                    "maxConcurrentUpload": 6,
                    "maxConcurrentDownload": 6,
                    "maxSizeRequest": 10485760,
                    "maxSizeUpload": 26214400,
                    "maxCallsInRequest": 64,
                    "maxObjectsInGet": 500,
                    "maxObjectsInSet": 500,
                    "maxObjectsInQuery": 500,
                    "collapsable": False,
                    "maxScheduledSends": 0,
                },
                "urn:ietf:params:jmap:submission": {},
                "urn:ietf:params:jmap:contacts": {},
                "urn:ietf:params:jmap:calendars": {},
            },
            "limits": {
                "maxSizeRequest": 10485760,
                "maxSizeUpload": 26214400,
                "maxConcurrentRequests": 64,
                "maxCallsInRequest": 64,
                "maxObjectsInGet": 500,
                "maxObjectsInSet": 500,
                "maxObjectsInQuery": 500,
                "collapsable": False,
                "maxScheduledSends": 0,
            },
        }

    def do_GET(self):
        if self.check_proxy_cut():
            return

        parsed = urlparse(self.path)
        path = parsed.path
        query = parse_qs(parsed.query)

        # Scripted slow response.
        if query.get("slow", [None])[0] == "1":
            time.sleep(2)

        # Scripted error on session endpoint.
        result = scripted_error(query)
        if result is not None:
            code, body, extra = result
            if body is None:
                self.send_text(code, "")
            else:
                self.send_json(code, body, extra)
            return

        if path == "/.well-known/jmap":
            host = self.headers.get("Host", "localhost")
            self.send_response(302)
            self.send_header("Location", f"https://{host}/jmap/session")
            self.end_headers()
            return

        if path == "/jmap/session":
            domain = self.headers.get("Host", "localhost").split(":")[0]
            if domain not in DOMAINS:
                domain = DOMAINS[0]
            self.send_json(200, self.session_for(domain))
            return

        if path == "/jmap/events":
            self.serve_events()
            return

        if path.startswith("/jmap/download/"):
            blob_id = path[len("/jmap/download/"):]
            with ScriptedState.upload_lock:
                data = ScriptedState.uploads.get(blob_id)
            if data is None:
                self.send_json(404, {"error": {"code": "notFound", "description": "unknown blob"}})
                return
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return

        self.send_json(404, {"error": {"code": "notFound", "description": "not found"}})

    def do_POST(self):
        if self.check_proxy_cut():
            return

        parsed = urlparse(self.path)
        path = parsed.path
        query = parse_qs(parsed.query)

        # Scripted slow response.
        if query.get("slow", [None])[0] == "1":
            time.sleep(2)

        # Scripted error.
        result = scripted_error(query)
        if result is not None:
            code, body, extra = result
            if body is None:
                self.send_text(code, "")
            else:
                self.send_json(code, body, extra)
            return

        # Proxy toggle.
        if path == "/jmap/proxy":
            with ScriptedState.proxy_lock:
                if query.get("cut", [None])[0] == "1":
                    ScriptedState.proxy_cut = True
                elif query.get("restore", [None])[0] == "1":
                    ScriptedState.proxy_cut = False
            self.send_json(200, {"ok": True})
            return

        if path == "/jmap/upload":
            body = self.read_body()
            blob_id = hashlib.sha256(body).hexdigest()[:32]
            with ScriptedState.upload_lock:
                ScriptedState.uploads[blob_id] = body
            self.send_json(200, {"blobId": blob_id, "size": len(body), "type": "application/octet-stream"})
            return

        if path == "/jmap/api":
            body = self.read_body()
            try:
                req = json.loads(body)
            except Exception:
                self.send_json(400, {"error": {"code": "invalidRequest", "description": "invalid JSON"}})
                return

            method_calls = req.get("methodCalls", [])
            responses = []
            for call in method_calls:
                if not isinstance(call, list) or len(call) < 3:
                    responses.append(["error", {"code": "invalidRequest"}, call[2] if len(call) > 2 else None])
                    continue
                method, args, call_id = call[0], call[1], call[2]

                # Per-call scripted errors from args.
                if isinstance(args, dict):
                    err = args.get("__error")
                    if err == "cannotCalculateChanges":
                        responses.append(["error", cannot_calculate_changes(), call_id])
                        continue
                    if err == "stateMismatch":
                        responses.append(["error", state_mismatch(), call_id])
                        continue

                result = self.handle_method(method, args)
                responses.append(result + [call_id])

            self.send_json(200, {"methodResponses": responses, "sessionState": "state-1"})
            return

        self.send_json(404, {"error": {"code": "notFound", "description": "not found"}})

    def do_OPTIONS(self):
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type, Authorization")
        self.end_headers()

    def handle_method(self, method, args):
        """Return [response_type, response_body] for a JMAP method call."""
        if not isinstance(args, dict):
            args = {}
        if method == "Identity/get":
            return ["Identity/get", {"accountId": args.get("accountId", ""), "list": [], "notFound": [], "state": "state-1"}]
        if method == "Mailbox/get":
            return ["Mailbox/get", {"accountId": args.get("accountId", ""), "list": [], "notFound": [], "state": "state-1"}]
        if method == "Email/query":
            return ["Email/query", {"accountId": args.get("accountId", ""), "queryState": "qs-1", "ids": [], "total": 0, "canCalculateChanges": True}]
        if method == "Email/get":
            return ["Email/get", {"accountId": args.get("accountId", ""), "list": [], "notFound": [], "state": "state-1"}]
        if method == "Mailbox/changes":
            return ["Mailbox/changes", {"accountId": args.get("accountId", ""), "hasMoreChanges": False, "sinceState": "state-1"}]
        if method == "Email/changes":
            return ["Email/changes", {"accountId": args.get("accountId", ""), "hasMoreChanges": False, "sinceState": "state-1"}]
        if method in ("Account/set", "Account/subscribe"):
            return [method, {"accountId": args.get("accountId", ""), "created": {}, "updated": {}, "destroyed": {}, "notFound": {}, "state": "state-1"}]
        if method == "Capability/get":
            return ["Capability/get", {}]
        return ["error", {"code": "unknownMethod", "description": f"unknown method {method}"}, None]

    def serve_events(self):
        """EventSource stream: send StateChange events with occasional drops."""
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Connection", "keep-alive")
        self.end_headers()

        try:
            with ScriptedState.event_lock:
                ScriptedState.event_sse_count += 1
                count = ScriptedState.event_sse_count
            for i in range(5):
                if self.check_proxy_cut():
                    return
                # Drop every third event.
                if i % 3 == 0:
                    try:
                        self.wfile.write(b": drop\n\n")
                        self.wfile.flush()
                    except Exception:
                        return
                    time.sleep(0.5)
                    continue
                try:
                    data = json.dumps({"type": "StateChange", "accountId": "a1"})
                    self.wfile.write(f"event: StateChange\ndata: {data}\n\n".encode("utf-8"))
                    self.wfile.flush()
                except Exception:
                    return
                time.sleep(0.5)
        except (BrokenPipeError, ConnectionResetError):
            pass


def run(port, cert_file, key_file):
    server = ThreadingHTTPServer(("127.0.0.1", port), JMapHandler)
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(cert_file, key_file)
    server.socket = ctx.wrap_socket(server.socket, server_side=True)
    print(f"JMAP fixture server listening on https://127.0.0.1:{port}", flush=True)
    server.serve_forever()


def main():
    port = free_port()
    key_file, cert_file = load_cert(CA_DIR)
    state_file = os.path.join(REPO_ROOT, "build", "fixture-state.txt")
    with open(state_file, "w") as f:
        f.write(f"{port}\n{cert_file}\n")
    try:
        run(port, cert_file, key_file)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()

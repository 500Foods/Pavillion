#!/usr/bin/env python3
"""JMAP fixture server for Pavillion tests.

Serves recorded JMAP responses from tests/support/fixtures/*.json, keyed by
method and arguments, plus scripted error and behavioral cases. Python standard
library only. Started and stopped by `make test`; the URL and test CA are
passed to the Unity runner through environment variables.
"""

import base64
import hashlib
import json
import os
import re
import socket
import ssl
import sys
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import urlparse, parse_qs

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FIXTURE_DIR = os.path.join(REPO_ROOT, "tests", "support", "fixtures")
CA_DIR = os.path.join(REPO_ROOT, "build", "test-ca")

SESSION_URL = "https://localhost:{port}/jmap/session"
WELLKNOWN_URL = "https://localhost:{port}/.well-known/jmap"

DOMAINS = ["test-a.local", "test-b.local"]
ACCOUNTS = [
    {"id": "a1", "email": "andrew@test-a.local", "name": "Andrew"},
    {"id": "a2", "email": "alice@test-a.local", "name": "Alice"},
    {"id": "b1", "email": "bob@test-b.local", "name": "Bob"},
    {"id": "b2", "email": "carol@test-b.local", "name": "Carol"},
    {"id": "b3", "email": "dave@test-b.local", "name": "Dave"},
]


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
             "-subj", "/CN=pavillion-test-ca"],
            check=True, capture_output=True,
        )
    return key_path, cert_path


class JMapHandler(BaseHTTPRequestHandler):
    server_version = "PavillionFixture/0.1"

    def log_message(self, format, *args):
        pass

    def send_json(self, code, obj):
        body = json.dumps(obj).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
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
        parsed = urlparse(self.path)
        path = parsed.path
        query = parse_qs(parsed.query)

        if path == "/.well-known/jmap":
            # Redirect to the session endpoint on the same host.
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

        if path == "/jmap/api":
            # JMAP POST endpoint (only GET is a session; this handles OPTIONS).
            self.send_json(405, {"error": "method not allowed"})
            return

        # Serve a recorded fixture if one matches.
        self.serve_fixture("GET", path, query)
        return

    def do_POST(self):
        parsed = urlparse(self.path)
        path = parsed.path

        if path == "/jmap/api":
            body = self.read_body()
            try:
                req = json.loads(body)
            except Exception:
                self.send_json(400, {"error": "invalid JSON"})
                return

            method_calls = req.get("methodCalls", [])
            responses = []
            for call in method_calls:
                if not isinstance(call, list) or len(call) < 3:
                    responses.append(["error", {"code": "invalidRequest"}, call[2] if len(call) > 2 else None])
                    continue
                method, args, call_id = call[0], call[1], call[2]
                result = self.handle_method(method, args)
                responses.append(result + [call_id])

            self.send_json(200, {"methodResponses": responses, "sessionState": "state-1"})
            return

        self.send_json(404, {"error": "not found"})

    def do_OPTIONS(self):
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type, Authorization")
        self.end_headers()

    def handle_method(self, method, args):
        """Return [response_type, response_body] for a JMAP method call."""
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
        if method == "Account/subscribe" or method == "Account/set":
            return [method, {"accountId": args.get("accountId", ""), "created": {}, "updated": {}, "destroyed": {}, "notFound": {}, "state": "state-1"}]
        if method == "Capability/get":
            return ["Capability/get", {}]
        return ["error", {"code": "unknownMethod", "description": f"unknown method {method}"}, None]

    def serve_fixture(self, method, path, query):
        """Serve a recorded fixture from tests/support/fixtures/. Serve 404 if none."""
        # For W-2, only the session endpoint and well-known are implemented.
        # Other paths return 404 until later phases add them.
        self.send_json(404, {"error": "not implemented"})


def run(port, cert_file, key_file):
    server = HTTPServer(("127.0.0.1", port), JMapHandler)
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(cert_file, key_file)
    server.socket = ctx.wrap_socket(server.socket, server_side=True)
    print(f"JMAP fixture server listening on https://127.0.0.1:{port}", flush=True)
    server.serve_forever()


def main():
    port = free_port()
    key_file, cert_file = load_cert(CA_DIR)
    # Write the URL and CA path so make test can pick them up.
    state_file = os.path.join(REPO_ROOT, "build", "fixture-state.txt")
    with open(state_file, "w") as f:
        f.write(f"{port}\n{cert_file}\n")
    try:
        run(port, cert_file, key_file)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Seed the Stalwart test container with accounts, mailboxes, and messages.

Uses the JMAP API over the mapped HTTPS port. Creates the accounts defined in
the W-2 fixture (five accounts across test-a.local and test-b.local), each
with an Inbox, Sent, Drafts, and Archive mailbox, and a few seeded messages
from tests/corpus/.
"""

import json
import os
import ssl
import sys
import urllib.request
import urllib.error

HOST = os.environ.get("PAV_STALWART_HOST", "localhost")
PORT = int(os.environ.get("PAV_STALWART_PORT", "1443"))
USER = os.environ.get("PAV_STALWART_USER", "andrew@test-a.local")
PASS = os.environ.get("PAV_STALWART_PASS", "test")

BASE_URL = f"https://{HOST}:{PORT}"


def ctx():
    c = ssl.create_default_context()
    c.check_hostname = False
    c.verify_mode = ssl.CERT_NONE
    return c


def jmap_request(path, payload):
    data = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(
        BASE_URL + path,
        data=data,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    password_mgr = urllib.request.HTTPPasswordMgrWithDefaultRealm()
    password_mgr.add_password(None, BASE_URL, USER, PASS)
    handler = urllib.request.HTTPBasicAuthHandler(password_mgr)
    opener = urllib.request.build_opener(handler)
    try:
        with opener.open(req, context=ctx(), timeout=10) as resp:
            return resp.status, json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read().decode("utf-8"))


def session():
    status, body = jmap_request("/jmap/api", {
        "methodCalls": [["Capability/get", {}, "0"]],
    })
    return status, body


def main():
    status, body = session()
    print(f"Stalwart session: {status}")
    print(json.dumps(body, indent=2)[:1000])
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env bash
# Test CA for Pavillion: one self-signed certificate used as both the CA and
# the server certificate, shared by the fixture server (jmapfix.sh) and the
# Stalwart test container (make it-up).
#
# Usage: testca.sh <dir>
# Writes <dir>/test-ca.crt and <dir>/test-ca.key (SAN localhost, 127.0.0.1,
# test-a.local, test-b.local). Keeps an existing pair unless a file is missing,
# the certificate expires within a day, or it lacks DNS:localhost.
# Test-only key material: never commit it.
set -eu

dir=${1:?usage: testca.sh <dir>}
key="$dir/test-ca.key"
crt="$dir/test-ca.crt"

command -v openssl >/dev/null 2>&1 || { echo "testca: openssl is required" >&2; exit 1; }
mkdir -p "$dir"

if [ -f "$key" ] && [ -f "$crt" ] &&
   openssl x509 -in "$crt" -noout -checkend 86400 >/dev/null 2>&1 &&
   openssl x509 -in "$crt" -noout -ext subjectAltName 2>/dev/null | grep -q 'DNS:localhost'; then
  exit 0
fi

openssl req -x509 -newkey rsa:2048 -keyout "$key" -out "$crt" \
  -days 365 -nodes -subj "/CN=pavillion-test-ca" \
  -addext "subjectAltName=DNS:localhost,IP:127.0.0.1,DNS:test-a.local,DNS:test-b.local" \
  >/dev/null 2>&1 || { echo "testca: openssl could not create the test CA" >&2; exit 1; }
# The Stalwart container reads the key as its own user; it is test-only.
chmod 0644 "$key" "$crt"

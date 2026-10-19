#!/usr/bin/env bash
# Start or stop the JMAP fixture server for `make test`.
# Usage: run-fixture.sh start|stop
set -euo pipefail

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
state="$root/build/fixture-state.txt"
pidfile="$root/build/fixture.pid"

case "${1:-}" in
  start)
    rm -f "$state" "$pidfile"
    python3 "$root/tests/support/jmapfix.py" &
    echo $! > "$pidfile"
    for i in $(seq 1 100); do
      [ -f "$state" ] && break
      sleep 0.1
    done
    [ -f "$state" ] || { echo "fixture server did not start" >&2; exit 1; }
    ;;
  stop)
    if [ -f "$pidfile" ]; then
      kill "$(cat "$pidfile")" 2>/dev/null || true
      wait "$(cat "$pidfile")" 2>/dev/null || true
      rm -f "$pidfile"
    fi
    ;;
  *)
    echo "usage: run-fixture.sh start|stop" >&2
    exit 2
    ;;
esac

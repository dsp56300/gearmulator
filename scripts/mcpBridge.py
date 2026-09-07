#!/usr/bin/env python3
"""stdio <-> HTTP bridge to the MCP server hosted inside a running plugin.

Claude Code spawns this once per session and injects CLAUDE_CODE_SESSION_ID into
its environment. The same variable reaches the plugin, because the plugin's host
(vsthost, the DAW, ...) is started from that session's shell and inherits it, and
the plugin writes it into the discovery file. So the instance is selected by
session, never by port number: a second session can never reach the first
session's plugin, and an ambiguous match refuses instead of guessing.
"""

import json
import os
import sys
import urllib.request

DISCOVERY = os.path.join(os.path.expanduser("~"), ".gearmulator_mcp.json")


def log(msg):
	sys.stderr.write("mcpBridge: " + msg + "\n")


def health(port):
	"""Server name if an MCP server answers on that port, else None (weeds out stale entries)."""
	try:
		with urllib.request.urlopen("http://127.0.0.1:%d/" % port, timeout=2) as r:
			return json.load(r).get("server") or "?"
	except Exception:
		return None


def resolve():
	session = os.environ.get("CLAUDE_CODE_SESSION_ID", "")

	try:
		with open(DISCOVERY, "r", encoding="utf-8") as f:
			entries = json.load(f)
	except FileNotFoundError:
		entries = []
	except Exception as e:
		log("cannot read %s: %s" % (DISCOVERY, e))
		return None

	mine = [e for e in entries if e.get("sessionId") and e.get("sessionId") == session]
	if not mine:
		# nothing claimed by this session - fall back to instances started outside of one
		mine = [e for e in entries if not e.get("sessionId")]

	live = [(e, health(e.get("port", 0))) for e in mine]
	live = [(e, name) for e, name in live if name]

	if len(live) != 1:
		log("session %s: need exactly one running instance, found %d %s" % (
			session or "<none>", len(live), [n for _, n in live]))
		log("start the plugin from this session, or close the extra instances")
		return None

	entry, name = live[0]
	log("using %s (pid %s, port %s)" % (name, entry.get("pid"), entry["port"]))
	return "http://127.0.0.1:%d/mcp" % entry["port"]


def main():
	url = resolve()
	if not url:
		return 1

	sys.stdin.reconfigure(encoding="utf-8")
	sys.stdout.reconfigure(encoding="utf-8", newline="\n")

	for line in sys.stdin:
		line = line.strip()
		if not line:
			continue

		try:
			request_id = json.loads(line).get("id")
		except ValueError:
			continue

		try:
			req = urllib.request.Request(url, line.encode("utf-8"), {"Content-Type": "application/json"})
			with urllib.request.urlopen(req) as r:  # no timeout: tool calls may run for a while
				reply = json.loads(r.read().decode("utf-8"))
		except Exception as e:
			log("request failed: %s" % e)
			if request_id is None:
				continue
			reply = {"jsonrpc": "2.0", "id": request_id,
			         "error": {"code": -32000, "message": "plugin unreachable: %s" % e}}

		if request_id is None:
			continue  # notification, no reply allowed

		sys.stdout.write(json.dumps(reply) + "\n")
		sys.stdout.flush()

	return 0


def selftest():
	"""python scripts/mcpBridge.py --selftest - checks the instance selection rules."""
	global DISCOVERY, health
	import tempfile

	entries = []
	alive = set()
	health = lambda port: "srv%d" % port if port in alive else None
	fd, DISCOVERY = tempfile.mkstemp(suffix=".json")
	os.close(fd)

	def check(expect, session, listed, running, what):
		entries[:] = listed
		alive.clear()
		alive.update(running)
		with open(DISCOVERY, "w", encoding="utf-8") as f:
			json.dump(entries, f)
		os.environ["CLAUDE_CODE_SESSION_ID"] = session
		got = resolve()
		assert got == expect, "%s: got %r, want %r" % (what, got, expect)

	a = {"port": 13710, "pid": 1, "sessionId": "A"}
	b = {"port": 13711, "pid": 2, "sessionId": "B"}
	manual = {"port": 13712, "pid": 3, "sessionId": ""}
	url = lambda p: "http://127.0.0.1:%d/mcp" % p

	check(url(13711), "B", [a, b], {13710, 13711}, "picks own session, not the lower port")
	check(None, "C", [a, b], {13710, 13711}, "refuses when no instance is ours")
	check(url(13712), "C", [a, manual], {13710, 13712}, "falls back to an unclaimed instance")
	check(None, "C", [manual, dict(manual, port=13713)], {13712, 13713}, "refuses two unclaimed instances")
	check(None, "A", [a], set(), "refuses a stale entry whose port is dead")
	check(url(13710), "A", [a, dict(a, port=13799)], {13710}, "skips our own stale entry")
	check(None, "", [a, b], {13710, 13711}, "refuses when the client set no session id")

	os.remove(DISCOVERY)
	print("selftest ok")


if __name__ == "__main__":
	if "--selftest" in sys.argv:
		selftest()
	else:
		sys.exit(main())

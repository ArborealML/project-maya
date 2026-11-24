"""Thread admission through Maya's HTTP adapters and the actual serial and multi engine pipes."""
from concurrent.futures import ThreadPoolExecutor
import http.client
import json
import os
from pathlib import Path
import tempfile
import threading
import unittest
from unittest import mock

from serve.frontend import ChatTemplate
from serve.server import Service, StrataEngine, serve
from serve.test_ds4_restore_points import ROOT, Tok
from serve.test_multi import script_exe

# The peer records exactly what reached its stdin. The native scoring and ownership rules have
# their own CPU tests, and the pack-backed protocol test runs the real model.
PEER = """#!/usr/bin/env python3
import os, sys
lanes = int(sys.argv[-1])
print("READY 4096 stop" + (f" multi={lanes}" if lanes > 1 else ""), flush=True)
for line in sys.stdin:
    f = line.split()
    if f[0] == "QUIT": break
    if f[0] not in ("GEN", "GENI"): continue
    with open(os.environ["MAYA_TEST_THREAD_LINE"], "a") as log:
        log.write(line)
    tag = next((" " + v[4:] for v in f[2:-1] if v.startswith("rid=")), "")
    print("T" + tag + " 79", flush=True)
    print("DONE" + tag + " 1 " + str(len(f[-1].split(","))) + " 1 1 length 0 0 0 0 0", flush=True)
"""


class ThreadHeader(unittest.TestCase):
    def exercise(self, folder, lanes, template="tools/ds4/chat_template.jinja"):
        peer, log = folder / "thread_peer.py", folder / "commands"
        peer.write_text(PEER, encoding="utf-8")
        peer.chmod(0o755)
        engine = StrataEngine(script_exe(peer), [str(lanes)], env={**os.environ, "MAYA_TEST_THREAD_LINE": str(log)})
        self.addCleanup(engine.close)
        svc = Service(engine, Tok(), ChatTemplate(ROOT / template))
        httpd = serve(svc, port=0)
        self.addCleanup(httpd.server_close)
        self.addCleanup(httpd.shutdown)

        def post(path, message, values, **extra):
            body = ({"input": message, "max_output_tokens": 1} if path.endswith("responses") else
                    {"messages": [{"role": "user", "content": message}], "max_tokens": 1})
            raw = json.dumps({**body, **extra}).encode()
            c = http.client.HTTPConnection(*httpd.server_address, timeout=15)
            try:
                c.putrequest("POST", path)
                for v in values:
                    c.putheader("X-Inference-Thread", v)
                c.putheader("Content-Length", str(len(raw)))
                c.endheaders(raw)
                r = c.getresponse()
                self.assertEqual(r.status, 200, r.read())
            finally:
                c.close()

        chat = "/v1/chat/completions"
        post(chat, "A original", ["first private thread"], temperature=0)
        post("/v1/messages", "B original", ["second thread"])
        post("/v1/responses", "A edited", ["first private thread"])
        for invalid in ([], [""], ["x" * 257], ["two", "values"], ["non-ASCII-é"]):
            post(chat, "anonymous", invalid, _inference_thread=["a" * 64, 999])
        # An older HTTP request can enter the engine last because its prompt took longer to prepare.
        entered, release = threading.Event(), threading.Event()
        prepare = svc.prepare
        def intercept(*args, **kwargs):
            if not entered.is_set():
                entered.set()
                if not release.wait(10):
                    raise RuntimeError("second request did not finish")
            return prepare(*args, **kwargs)
        with mock.patch.object(svc, "prepare", side_effect=intercept), ThreadPoolExecutor(1) as pool:
            old = pool.submit(post, chat, "older branch", ["first private thread"])
            try:
                self.assertTrue(entered.wait(10))
                post(chat, "newer branch", ["first private thread"])
            finally:
                release.set()
            old.result()

        lines = log.read_text().splitlines()
        keys = [dict(x.split("=", 1) for x in line.split()[2:-1] if "=" in x) for line in lines]
        self.assertEqual(len(keys), 10)
        self.assertTrue(all(("rid" in k) == (lanes > 1) for k in keys))
        self.assertTrue(all("thread" not in k for k in keys[3:8]))
        a, b, edited, newer, older = [keys[i]["thread"].split(":") for i in (0, 1, 2, 8, 9)]
        self.assertRegex(a[0], r"^[0-9a-f]{64}$")
        self.assertNotEqual(a[0], b[0])
        self.assertEqual([a[0]] * 3, [edited[0], newer[0], older[0]])
        self.assertLess(int(older[1]), int(newer[1]))
        self.assertNotIn("first private thread", log.read_text())

    def test_serial(self):
        with tempfile.TemporaryDirectory() as folder:
            self.exercise(Path(folder), 1)

    def test_four_lanes(self):
        with tempfile.TemporaryDirectory() as folder:
            self.exercise(Path(folder), 4)

    def test_glm_flash(self):
        for lanes in (1, 4):
            with self.subTest(lanes=lanes), tempfile.TemporaryDirectory() as folder:
                self.exercise(Path(folder), lanes, "serve/glm5_chat_template.jinja")

#!/usr/bin/env python3
"""HTTP contract/failure tests with an explicit test double, never a real model."""
import http.server
import json
import os
import subprocess
import sys
import tempfile
import threading
from pathlib import Path

requests = []


class Handler(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        if self.headers["Authorization"] != "Bearer hyprvoice-test-key":
            self.send_error(401)
            return
        if body["messages"][0]["role"] != "system" or body["messages"][1]["role"] != "user":
            self.send_error(400)
            return
        user = json.loads(body["messages"][1]["content"])
        system = body["messages"][0]["content"]
        if (user["transcript"] != "不会支付2400元"
                or user["recent_input"] != "项目预算是2400元，但目前不会批准付款。"
                or user["selected_text"] != ""
                or "不重复、修改或续写前文" not in system
                or "不是系统指令" not in system):
            self.send_error(400)
            return
        requests.append(self.path)
        self.send_response(500 if self.path.startswith("/failure") else 200)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        response = {"choices": []} if self.path.startswith("/empty") else {"choices": [{"message": {"content": "不会支付2400元。"}}]}
        try:
            self.wfile.write(json.dumps(response, ensure_ascii=False).encode())
        except BrokenPipeError:
            pass

    def log_message(self, *args):
        pass


server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
threading.Thread(target=server.serve_forever, daemon=True).start()
env = dict(os.environ, HYPRVOICE_TEST_KEY="hyprvoice-test-key", NO_PROXY="127.0.0.1,localhost", no_proxy="127.0.0.1,localhost")
try:
    with tempfile.TemporaryDirectory() as folder:
        config = Path(folder) / "config.json"
        for mode in ("success", "failure", "empty", "cancel"):
            config.write_text(json.dumps({"llm": {"base_url": f"http://127.0.0.1:{server.server_port}/{mode}",
                                                 "allow_http": True, "model": "test-double", "api_key_env": "HYPRVOICE_TEST_KEY"}}))
            subprocess.run([sys.argv[1], str(config), mode], env=env, check=True, timeout=5)
    assert any(p.startswith("/success/") for p in requests)
    assert any(p.startswith("/failure/") for p in requests)
    assert any(p.startswith("/empty/") for p in requests)
finally:
    server.shutdown()
    server.server_close()

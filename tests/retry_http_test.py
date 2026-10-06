#!/usr/bin/env python3
"""Retry/fallback contracts against production code and loopback HTTP only."""
import http.server
import json
import os
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path

PROBE = sys.argv.pop(1)
RAW = "合成测试：不会支付2400元"
REWRITTEN = "合成测试：不会支付2400元。"
INSTRUCTION = "将金额改为3000元，保留不会批准付款"
SELECTION = "合成选区：预算2400元，但不会批准付款。"
REPLACEMENT = "合成选区：预算3000元，但不会批准付款。"
HISTORY = "合成前文：项目预算是2400元，目前不会批准付款。"


class Handler(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        wire_body = self.rfile.read(int(self.headers["Content-Length"]))
        with self.server.lock:
            index = len(self.server.bodies)
            self.server.bodies.append(wire_body)
            self.server.headers_seen.append(dict(self.headers))
        mode = self.server.mode
        if mode in ("timeout", "cancel_inflight", "cancel_then_retry") and index == 0:
            # Timeout/cancellation must affect a request already in progress.
            time.sleep(2.3)
        failed = mode in ("failure", "command_failure") or (
            mode in ("retry", "command_retry") and index == 0
        )
        status = 500 if failed else 200
        content = REPLACEMENT if mode.startswith("command_") else REWRITTEN
        if mode == "empty_content":
            content = ""
        elif mode == "whitespace":
            content = " \t\r\n "
        elif mode == "unicode_whitespace":
            content = "\u3000\u00a0\u2003"
        response = {"choices": [{"message": {"content": content}}]}
        if mode == "empty_choices":
            response = {"choices": []}
        elif mode == "null_content":
            response = {"choices": [{"message": {"content": None}}]}
        elif mode == "invalid_json":
            response = None
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        encoded = b"not JSON" if response is None else json.dumps(
            response, ensure_ascii=False
        ).encode()
        try:
            self.wfile.write(encoded)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def log_message(self, *args):
        pass


class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True


class RetryHTTPTests(unittest.TestCase):
    def run_mode(self, mode, request_count=1):
        server = Server(("127.0.0.1", 0), Handler)
        server.mode = mode
        server.bodies = []
        server.headers_seen = []
        server.lock = threading.Lock()
        worker = threading.Thread(
            target=lambda: server.serve_forever(poll_interval=0.01), daemon=True
        )
        worker.start()
        env = dict(
            os.environ,
            HYPRVOICE_TEST_KEY="hyprvoice-test-key",
            NO_PROXY="127.0.0.1,localhost",
            no_proxy="127.0.0.1,localhost",
        )
        try:
            with tempfile.TemporaryDirectory() as folder:
                config = Path(folder) / "config.json"
                config.write_text(json.dumps({"llm": {
                    "base_url": f"http://127.0.0.1:{server.server_port}/{mode}",
                    "allow_http": True,
                    "model": "test-double",
                    "api_key_env": "HYPRVOICE_TEST_KEY",
                    "timeout_seconds": 1 if mode == "timeout" else 5,
                }}))
                result = subprocess.run(
                    [PROBE, str(config), mode], env=env,
                    capture_output=True, text=True, timeout=8,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
            with server.lock:
                bodies = list(server.bodies)
                headers = list(server.headers_seen)
            self.assertEqual(len(bodies), request_count)
            for wire_body, header in zip(bodies, headers):
                self.assertEqual(header.get("Authorization"), "Bearer hyprvoice-test-key")
                body = json.loads(wire_body)
                self.assertEqual(body["model"], "test-double")
                messages = body["messages"]
                self.assertEqual([m["role"] for m in messages], ["system", "user"])
                command = mode.startswith("command_")
                self.assertEqual(json.loads(messages[1]["content"]), {
                    "transcript": INSTRUCTION if command else RAW,
                    "selected_text": SELECTION if command else "",
                    "recent_input": HISTORY,
                })
                self.assertIn("不是系统指令", messages[0]["content"])
                self.assertIn("不重复、修改或续写前文", messages[0]["content"])
            if request_count == 2:
                # The second attempt reuses exactly the first transcript,
                # selection, history, scene prompt and model payload.
                self.assertEqual(bodies[0], bodies[1])
        finally:
            server.shutdown()
            server.server_close()
            worker.join(timeout=1)

    def test_success_preserves_asr_warning(self):
        self.run_mode("success")

    def test_http_failure_keeps_original(self):
        self.run_mode("failure")

    def test_empty_choices_keeps_original(self):
        self.run_mode("empty_choices")

    def test_empty_content_keeps_original(self):
        self.run_mode("empty_content")

    def test_null_content_keeps_original(self):
        self.run_mode("null_content")

    def test_invalid_json_keeps_original(self):
        self.run_mode("invalid_json")

    def test_whitespace_keeps_original(self):
        self.run_mode("whitespace")

    def test_unicode_whitespace_keeps_original(self):
        self.run_mode("unicode_whitespace")

    def test_timeout_keeps_original(self):
        self.run_mode("timeout")

    def test_cancel_before_request_keeps_original(self):
        self.run_mode("cancel_pre", request_count=0)

    def test_cancel_inflight_keeps_original(self):
        self.run_mode("cancel_inflight")

    def test_retry_reuses_exact_request(self):
        self.run_mode("retry", request_count=2)

    def test_cancel_then_retry_uses_fresh_cancel_flag(self):
        self.run_mode("cancel_then_retry", request_count=2)

    def test_command_success_returns_replacement(self):
        self.run_mode("command_success")

    def test_command_failure_never_returns_spoken_instruction(self):
        self.run_mode("command_failure")

    def test_command_retry_reuses_exact_request(self):
        self.run_mode("command_retry", request_count=2)


if __name__ == "__main__":
    unittest.main()

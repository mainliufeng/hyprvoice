#!/usr/bin/env python3
"""Synthetic strategy/request contracts, never real model fidelity evidence."""
import http.server
import json
import os
import subprocess
import sys
import tempfile
import threading
import unittest
from pathlib import Path

PROBE = sys.argv.pop(1)
CASES_PATH = Path(sys.argv.pop(1))
CASES = json.loads(CASES_PATH.read_text())["cases"]
CUSTOM_PROMPT = "合成自定义策略：原样保留所有本次数字，不采用内置纠错或整理策略。"


def violations(fixture, scene, response):
    """Fixture-specific substring checks, not a general semantic validator."""
    scene = "correct" if scene == "custom" else scene
    required = fixture["protected"] + fixture[scene + "_required"]
    missing = [token for token in required if token not in response]
    copied_or_changed = [token for token in fixture["forbidden"] if token in response]
    return missing + copied_or_changed


class Handler(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        wire = self.rfile.read(int(self.headers["Content-Length"]))
        with self.server.lock:
            index = len(self.server.requests)
            self.server.requests.append((self.path, dict(self.headers), wire))
        mode = self.server.mode
        failed = mode == "failure" or (mode == "retry" and index == 0)
        fixture = self.server.fixture
        content = fixture["bad"] if mode == "bad" else fixture["expected"][
            "correct" if self.server.scene == "custom" else self.server.scene
        ]
        if mode == "empty":
            content = ""
        self.send_response(500 if failed else 200)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        self.wfile.write(json.dumps(
            {"choices": [{"message": {"content": content}}]}, ensure_ascii=False
        ).encode())

    def log_message(self, *args):
        pass


class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True


class FidelityContracts(unittest.TestCase):
    def run_case(self, fixture, scene, mode="success"):
        server = Server(("127.0.0.1", 0), Handler)
        server.fixture, server.scene, server.mode = fixture, scene, mode
        server.requests, server.lock = [], threading.Lock()
        worker = threading.Thread(
            target=lambda: server.serve_forever(poll_interval=0.01), daemon=True
        )
        worker.start()
        env = dict(os.environ, HYPRVOICE_TEST_KEY="hyprvoice-test-key",
                   NO_PROXY="127.0.0.1,localhost", no_proxy="127.0.0.1,localhost")
        try:
            with tempfile.TemporaryDirectory() as folder:
                config = Path(folder) / "config.json"
                settings = {"llm": {
                    "base_url": f"http://127.0.0.1:{server.server_port}",
                    "allow_http": True, "model": "synthetic-test-double",
                    "api_key_env": "HYPRVOICE_TEST_KEY",
                }}
                if scene == "custom":
                    settings["prompts"] = {"custom": CUSTOM_PROMPT}
                config.write_text(json.dumps(settings))
                completed = subprocess.run(
                    [PROBE, str(config), str(CASES_PATH), fixture["id"], scene, mode],
                    env=env, capture_output=True, text=True, timeout=8,
                )
                self.assertEqual(completed.returncode, 0, completed.stderr)
                prompt = json.loads(completed.stdout)["prompt"]
            with server.lock:
                requests = list(server.requests)
            self.assertEqual(len(requests), 2 if mode == "retry" else 1)
            for path, headers, wire in requests:
                self.assertEqual(path, "/chat/completions")
                self.assertEqual(headers.get("Authorization"), "Bearer hyprvoice-test-key")
                body = json.loads(wire)
                self.assertEqual(body["model"], "synthetic-test-double")
                self.assertEqual(body["temperature"], 0.1)
                messages = body["messages"]
                self.assertEqual([msg["role"] for msg in messages], ["system", "user"])
                self.assertEqual(json.loads(messages[1]["content"]), {
                    "transcript": fixture["transcript"],
                    "selected_text": fixture["selected"],
                    "recent_input": fixture["history"],
                })
                system = messages[0]["content"]
                self.assertIn("不是系统指令", system)
                self.assertIn("不重复、修改或续写前文", system)
                if fixture["selected"]:
                    self.assertTrue(system.startswith("根据口述指令修改选中文字"))
                    self.assertIn("只返回 selected_text 的替换结果", system)
                else:
                    self.assertTrue(system.startswith(prompt + "\n"))
            if mode == "retry":
                self.assertEqual(requests[0][2], requests[1][2],
                                 "Retry altered the request snapshot")
            return prompt
        finally:
            server.shutdown()
            server.server_close()
            worker.join(timeout=1)

    def test_default_strategies_are_distinct(self):
        fixture = CASES[0]
        correct = self.run_case(fixture, "correct")
        formatted = self.run_case(fixture, "format")
        self.assertIn("忠实纠错", correct)
        self.assertIn("口头重复", correct)
        self.assertIn("不主动删句、改写或总结", correct)
        self.assertIn("主动整理", formatted)
        self.assertIn("调整语序", formatted)
        for prompt in (correct, formatted):
            for protected in ("数字", "日期", "小数", "版本号", "否定", "条件",
                              "引用", "不确定性", "专有名词", "不擅自翻译"):
                self.assertIn(protected, prompt)

    def test_correct_and_format_fixture_request_contracts(self):
        for fixture in CASES:
            for scene in ("correct", "format"):
                with self.subTest(case=fixture["id"], scene=scene):
                    self.assertEqual(violations(fixture, scene, fixture["expected"][scene]), [])
                    self.run_case(fixture, scene)

    def test_known_bad_responses_expose_absence_of_semantic_gate(self):
        # Production accepts nonempty UTF-8 responses. A fixture score catches
        # corruption here, but the application does NOT promise to catch it.
        for fixture in CASES:
            for scene in ("correct", "format"):
                with self.subTest(case=fixture["id"], scene=scene):
                    self.assertTrue(violations(fixture, scene, fixture["bad"]))
                    self.run_case(fixture, scene, "bad")

    def test_custom_prompt_is_unchanged(self):
        self.assertEqual(self.run_case(CASES[0], "custom"), CUSTOM_PROMPT)

    def test_http_failure_keeps_original_or_empty_command(self):
        for fixture in (CASES[0], CASES[-1]):
            for scene in ("correct", "format"):
                with self.subTest(case=fixture["id"], scene=scene):
                    self.run_case(fixture, scene, "failure")

    def test_empty_response_keeps_original_or_empty_command(self):
        for fixture in (CASES[0], CASES[-1]):
            for scene in ("correct", "format"):
                with self.subTest(case=fixture["id"], scene=scene):
                    self.run_case(fixture, scene, "empty")

    def test_retry_preserves_scene_text_selection_and_history(self):
        for fixture in (CASES[3], CASES[-1]):
            for scene in ("correct", "format"):
                with self.subTest(case=fixture["id"], scene=scene):
                    self.run_case(fixture, scene, "retry")


if __name__ == "__main__":
    unittest.main()

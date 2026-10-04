#!/usr/bin/env python3
"""Test-only VAD fault injection with real ASR and public human speech.

Requires the optional QA dependency `onnx`. The generated constant-zero
classifier is deliberately a test double and must never be installed.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import onnx
from onnx import helper, TensorProto

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("config", type=Path)
parser.add_argument("speech", type=Path)
parser.add_argument("output", type=Path)
parser.add_argument("--binary", type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
config = json.loads(args.config.read_text())
model_path = Path(os.path.expanduser(config["vad_model"]))
source = onnx.load(str(model_path))
# Preserve only the public I/O signature. No production weights or graph nodes
# are copied. This classifier deliberately returns zero for every audio frame.
graph = helper.make_graph([
    helper.make_node("Constant", [], ["output"],
                     value=helper.make_tensor("no_speech", TensorProto.FLOAT,
                                              [1, 1], [0.])),
    helper.make_node("Identity", ["state"], ["stateN"]),
], "TEST_ONLY_REJECT_ALL_SPEECH", list(source.graph.input), list(source.graph.output))
mock = helper.make_model(graph, opset_imports=list(source.opset_import))
mock.ir_version = source.ir_version
onnx.checker.check_model(mock)
reject = out / "TEST_ONLY_reject_speech.onnx"
onnx.save(mock, str(reject))
config["vad_model"] = str(reject)
config["asr"] = {"backend": "x-asr"}
config["context"] = {"enabled": False, "max_chars": 1024}
config["auto_commit"] = True
config["scene"] = "raw"
config_path = out / "config.json"
config_path.write_text(json.dumps(config, ensure_ascii=False, indent=2))
reply = subprocess.run([str((args.binary or root / "build/hyprvoice").resolve()),
                        "transcribe", str(args.speech.resolve())],
                       env=dict(os.environ, HYPRVOICE_CONFIG=str(config_path)),
                       check=True, capture_output=True, text=True, timeout=60)
result = json.loads(reply.stdout)
assert not result["speech"] and result["warning"] and result["auto_commit_blocked"]
for marker in ("因为远离大陆", "哺乳动物", "亚马逊河", "宽度可达"):
    assert marker in result["text"], "VAD rejection erased " + marker
(out / "result.json").write_text(json.dumps(result, ensure_ascii=False, indent=2))
print("PASS test-only VAD rejection retains real recognized speech for confirmation")

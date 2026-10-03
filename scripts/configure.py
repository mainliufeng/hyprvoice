#!/usr/bin/env python3
"""Configure real model files; never overwrite an existing configuration."""
import argparse
import json
import os
from pathlib import Path

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--streaming", type=Path, required=True)
parser.add_argument("--offline", type=Path, required=True)
parser.add_argument("--vad", type=Path, required=True)
parser.add_argument("--hotwords", type=Path)
parser.add_argument("--output", type=Path, default=Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config")) / "hyprvoice/config.json")
args = parser.parse_args()
streaming = args.streaming.expanduser().resolve()
offline = args.offline.expanduser().resolve()
vad = args.vad.expanduser().resolve()
for directory, names in ((streaming, ("encoder.int8.onnx", "decoder.onnx", "joiner.int8.onnx", "tokens.txt", "bpe.vocab")),
                         (offline, ("encoder-epoch-99-avg-1.int8.onnx", "decoder-epoch-99-avg-1.onnx", "joiner-epoch-99-avg-1.int8.onnx", "tokens.txt", "bpe.vocab"))):
    for name in names:
        if not (directory / name).is_file():
            parser.error(f"missing model file: {directory / name}")
if not vad.is_file():
    parser.error(f"missing VAD file: {vad}")
config = json.loads((root / "config/config.example.json").read_text())
config.update(streaming_model=str(streaming), offline_model=str(offline), vad_model=str(vad))
if args.hotwords:
    hotwords = args.hotwords.expanduser().resolve()
    hotwords.read_text(encoding="utf-8")
    config["hotwords"] = str(hotwords)
output = args.output.expanduser()
output.parent.mkdir(parents=True, exist_ok=True)
with output.open("x", encoding="utf-8") as f:
    f.write(json.dumps(config, ensure_ascii=False, indent=2) + "\n")
output.chmod(0o600)
print(output)

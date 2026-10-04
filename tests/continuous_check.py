#!/usr/bin/env python3
"""Real-model regression for quiet speech outside the VAD envelope.

Derive public human-speech cases from hash-pinned FLEURS fixtures. No private
recordings, synthetic speech, or model test doubles are used.
"""
import argparse
import array
import hashlib
import json
import os
from pathlib import Path
import subprocess
import wave

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("config", type=Path)
parser.add_argument("output", type=Path)
parser.add_argument("--binary", type=Path)
parser.add_argument("--prepare-only", action="store_true")
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
fixtures = root / "tests/fixtures/manifest.jsonl"
rows = {c["id"]: c for c in map(json.loads, fixtures.read_text().splitlines())}
source = []
for index in (1, 2):
    case = rows[f"fleurs-cmn_hans_cn-validation-{index}"]
    path = (fixtures.parent / case["audio"]).resolve()
    if hashlib.sha256(path.read_bytes()).hexdigest() != case["sha256"]:
        raise ValueError("Public source hash mismatch: " + case["id"])
    with wave.open(str(path)) as audio:
        if (audio.getnchannels(), audio.getsampwidth(), audio.getframerate()) != (1, 2, 16000):
            raise ValueError("Expected mono 16 kHz PCM16 fixture")
        pcm = array.array("h", audio.readframes(audio.getnframes()))
        if os.sys.byteorder != "little":
            pcm.byteswap()
    source.append((case, pcm))
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
cases = []
for name, order, gains in (("normal-continuous", (0, 1), (1, 1)),
                           ("level-matched", (0, 1), (1, 3.8)),
                           ("quiet-prefix", (0, 1), (.01, 1)),
                           ("quiet-suffix", (1, 0), (1, .01)),
                           ("quiet-whole", (0, 1), (.01, .05))):
    ordered = [source[index] for index in order]
    combined = array.array("h")
    for (_, pcm), gain in zip(ordered, gains):
        combined.extend(int(s * gain) for s in pcm)
        combined.extend([0] * 16000)
    if os.sys.byteorder != "little":
        combined.byteswap()
    path = out / (name + ".wav")
    with wave.open(str(path), "wb") as audio:
        audio.setparams((1, 2, 16000, 0, "NONE", "not compressed"))
        audio.writeframes(combined.tobytes())
    cases.append({"id": name, "audio": str(path),
                  "reference": " ".join(c["reference"] for c, _ in ordered),
                  "sources": [c["id"] for c, _ in ordered], "gains": gains,
                  "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
manifest = out / "manifest.jsonl"
manifest.write_text("".join(json.dumps(c, ensure_ascii=False) + "\n" for c in cases))
if not args.prepare_only:
    binary = (args.binary or root / "build/hyprvoice").resolve()
    with (out / "replay.jsonl").open("w") as result, (out / "replay.log").open("w") as log:
        subprocess.run([str(binary), "replay", str(manifest)], check=True,
                       stdout=result, stderr=log, timeout=120,
                       env=dict(os.environ, HYPRVOICE_CONFIG=str(args.config.resolve())))
    results = [json.loads(line) for line in (out / "replay.jsonl").read_text().splitlines()]
    if [r["id"] for r in results] != [c["id"] for c in cases]:
        raise AssertionError("Incomplete continuity replay")
    for result in results:
        for marker in ("因为远离大陆", "哺乳动物", "亚马逊河", "河段"):
            if marker not in result["text"]:
                raise AssertionError(result["id"] + " lost " + marker)
        print("PASS", result["id"], "retains landmarks in both utterances")

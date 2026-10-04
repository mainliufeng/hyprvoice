#!/usr/bin/env python3
"""Prepare a fixed ASCEND test subset and the complete official test split."""
import argparse
import hashlib
import io
import json
import random
import re
import wave
from pathlib import Path

import numpy as np
import pyarrow.parquet as pq
import soundfile as sf


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("assets", type=Path)
    p.add_argument("regression", type=Path)
    args = p.parse_args()
    base = args.assets.resolve()
    rows = pq.read_table(base / "ascend/main/test-00000-of-00001.parquet").to_pylist()
    # Fix selection before inspecting model output; do not select by correctness.
    rng = random.Random(20261004)
    chosen = set()
    for language in ("zh", "mixed", "en"):
        pool = [r["id"] for r in rows if r["language"] == language and 2 <= r["duration"] <= 20]
        chosen.update(rng.sample(sorted(pool), 40))
    corpus = base / "corpus"
    corpus.mkdir(exist_ok=True)
    full, screen = [], []
    prior = {}
    for row in rows:
        # Only preceding turns by this speaker in this session; never current/future text.
        match = re.search(r"_(\d+(?:\.\d+)?)_(\d+(?:\.\d+)?)\.wav$", row["audio"]["path"])
        start = float(match.group(1)) if match else None
        row["start_seconds"] = start
    # Chronological order is needed for a legitimate prefix, not parquet row order.
    ordered = sorted(rows, key=lambda r: (r["session_id"], r["original_speaker_id"],
                                         r["start_seconds"] if r["start_seconds"] is not None else float("inf")))
    prefixes = {}
    for row in ordered:
        key = (row["session_id"], row["original_speaker_id"])
        prefixes[row["id"]] = prior.get(key, "")[-1024:] if row["start_seconds"] is not None else ""
        if row["start_seconds"] is not None:
            prior[key] = (prior.get(key, "") + row["transcription"] + "\n")[-1024:]
    for row in rows:
        name = "ascend-test-" + row["id"]
        audio, rate = sf.read(io.BytesIO(row["audio"]["bytes"]), dtype="float32")
        assert rate == 16000 and audio.ndim == 1
        path = corpus / (name + ".wav")
        sf.write(path, audio, rate, subtype="PCM_16")
        case = {"id": name, "base_id": name, "audio": str(path), "reference": row["transcription"],
                "before": prefixes[row["id"]], "context_source": "preceding reference turns by same speaker/session",
                "source": "CAiRE/ASCEND", "revision": "737e9800ae31be9932ba8464c80366559bd28424",
                "source_split": "test", "source_id": row["id"], "fold": "test",
                "language": row["language"], "category": "ascend/" + row["language"] + "/clean",
                "duration_seconds": len(audio) / rate, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
        full.append(case)
        if row["id"] in chosen:
            screen.append(case)
    # Identical white-noise transform across engines, not real-room evidence.
    noise_ids = rng.sample(sorted(chosen), 30)
    for row in full:
        if row["source_id"] not in noise_ids:
            continue
        audio, rate = sf.read(row["audio"], dtype="float32")
        noise = np.random.default_rng(int(row["source_id"]) + 20261004).standard_normal(len(audio))
        power = float(np.mean(audio.astype("float64") ** 2))
        noise *= np.sqrt(power / (10 ** (10 / 10) * np.mean(noise ** 2)))
        mixed = audio + noise
        mixed /= max(1.0, float(np.max(np.abs(mixed))) / 0.98)
        path = corpus / (row["id"] + "-white-10db.wav")
        sf.write(path, mixed, rate, subtype="PCM_16")
        screen.append({**row, "id": row["id"] + "-white-10db", "audio": str(path),
                       "category": "ascend/" + row["language"] + "/white-10db",
                       "sha256": hashlib.sha256(path.read_bytes()).hexdigest(), "snr_db": 10})
    regression = []
    for line in args.regression.read_text().splitlines():
        row = json.loads(line)
        row["audio"] = str((args.regression.parent / row["audio"]).resolve())
        row["base_id"] = "fleurs/" + str(row.get("source_id", row["id"]))
        row["category"] = "regression/" + row["category"]
        row["before"] = ""
        with wave.open(row["audio"]) as audio:
            row["duration_seconds"] = audio.getnframes() / audio.getframerate()
        regression.append(row)
    for name, data in [("ascend-full.jsonl", full), ("screen.jsonl", screen + regression)]:
        (base / name).write_text("".join(json.dumps(r, ensure_ascii=False) + "\n" for r in data))
    print(json.dumps({"ascend_test": len(full), "new_clean_screen": len(chosen),
                      "new_noise_screen": len(noise_ids), "regression": len(regression),
                      "screen_total": len(screen) + len(regression)}))


if __name__ == "__main__":
    main()
